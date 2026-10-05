"""OKX 适配器装配：把 `codec`（报文）、`ws_public`（公共频道）与内核接到一起。

M1 的契约面只实现引擎真正用到的那部分（7 个 REST GET + `bbo-tbt` + 私有频道），
本文件是装配点，也是 T7/T8 落位的接口面：

| 接口 | 现状 |
|---|---|
| `name` | `"okx"` |
| `rest_routes()` | 交 T8 的 `rest.py` 的 7 个 GET；`bind_rest` 之前为空表（装配未接 REST 面） |
| `ws_public_handler()` | 转发 `ws_public.public_handler`（订阅回执 + 订阅表登记）+ 订阅成功后补推一次当前盘口 |
| `ws_private_handler()` | 转发 `ws_private.PrivateChannels.handle`（未 `bind_trading` 前响亮抛） |
| `bind_market_data()` | 装 bbo 推送桥（内核 `EventBus` → 协议帧 → WS 广播） |
| `bind_trading()` | 装私有频道业务（下单/撤单落地 + 私有频道推送） |
| `bind_rest()` | 装 REST 业务（7 个只读 GET；**必须在 `bind_trading` 之后**） |
"""

from collections.abc import Callable, Iterable

from aiohttp import web

from mockex.core.account import AccountManager
from mockex.core.book import OrderBook
from mockex.core.events import EventBus
from mockex.core.matching import MatchingEngine
from mockex.exchanges.base import ExchangeAdapter, Reply, RestRoute, frames_of
from mockex.exchanges.okx.rest import OkxRest
from mockex.exchanges.okx.ws_private import PrivateChannels
from mockex.exchanges.okx.ws_public import BBO_CHANNEL, BboBroadcaster, public_handler
from mockex.protocol.ws import WsRegistry

__all__ = ["OkxAdapter"]


class OkxAdapter(ExchangeAdapter):
    """OKX v5 适配器（一个进程一个实例，由 T10 装配）。"""

    name = "okx"

    def __init__(self) -> None:
        self._bbo: BboBroadcaster | None = None
        self._private: PrivateChannels | None = None
        self._rest: OkxRest | None = None

    def rest_routes(self) -> list[RestRoute]:
        """REST 路由表：`bind_rest` 之前为空表（装配还没接 REST 面），之后是 T8 的 7 个 GET。"""
        return [] if self._rest is None else self._rest.routes()

    def bind_market_data(self, bus: EventBus, book: OrderBook, registry: WsRegistry,
                         inst_ids: Iterable[str], *,
                         now_ms: Callable[[], int] | None = None) -> BboBroadcaster:
        """装 bbo 推送桥：`book.<instId>` 事件 → `bbo_frame` → `broadcast`（T10 调一次）。

        Args:
            bus: 内核事件总线。
            book: 订单簿（行情的唯一真相）。
            registry: 会话表（广播出口）。
            inst_ids: 需要监听的合约，来自配置 `instruments[]`。
            now_ms: 时钟（毫秒 epoch 取值函数）；缺省取系统时间，测试注入固定值。

        Returns:
            BboBroadcaster：桥对象（`stop()` 可整体退订）。

        Raises:
            ValueError: 某个 `inst_id` 为空（`BboBroadcaster.attach` 报）—— 半装不留残留：
                已 attach 的回调全部退订，适配器**不进入"已装配"状态**，可修正入参后重来。
            RuntimeError: 重复装配 —— 会装出第二条订阅，盘口每次变化推两帧（引擎双计）。
        """
        if self._bbo is not None:
            raise RuntimeError("OKX 行情桥已装配：重复 bind_market_data 会双推 bbo 帧")
        bridge = BboBroadcaster(bus, book, registry, now_ms=now_ms)
        try:
            for inst_id in inst_ids:
                bridge.attach(inst_id)
        except Exception:
            # 半装状态必须清干净：残留的订阅没人持有句柄能退订，`publish` 却照样命中它
            # （内核白做功、进程再也回不到干净状态），故要么全成、要么全不留。
            bridge.stop()
            raise
        self._bbo = bridge
        return bridge

    def bind_trading(self, engine: MatchingEngine, accounts: AccountManager, bus: EventBus, *,
                     instruments: Iterable, account: str,
                     now_ms: Callable[[], int] | None = None) -> PrivateChannels:
        """装私有频道业务：`op:order` / `op:cancel-order` 落地到撮合与账户（T10 调一次）。

        Args:
            engine: 撮合引擎。
            accounts: 账户账本（结算成交、出余额/持仓快照）。
            bus: 事件总线 —— 下单/撤单改变盘口后由本适配器发布 `book.<instId>`（T6 只做桥接）。
            instruments: 合约规格序列，来自配置 `instruments[]`（**必填**：旧空默认值下漏传 =
                每笔下单都撞"未知合约"，故障点离装配现场很远）。
            account: 私有连接落的账户名（配置里的默认 `api_key`；**必填**，同上）。
            now_ms: 时钟（毫秒 epoch 取值函数）；缺省取系统时间，测试注入固定值。

        Returns:
            PrivateChannels：私有频道业务对象（`track()` 登记管理面注入的挂单）。

        Raises:
            RuntimeError: 重复装配 —— 会接出第二套下单通道（同一笔委托被结算两次）。
            ValueError: `instruments` 为空或 `account` 为空（见 `PrivateChannels.__init__`）。
        """
        if self._private is not None:
            raise RuntimeError("OKX 私有频道已装配：重复 bind_trading 会接出第二套下单通道")
        channels = PrivateChannels(engine, accounts, bus, instruments=instruments,
                                   account=account, now_ms=now_ms)
        self._private = channels
        return channels

    def bind_rest(self, book: OrderBook, accounts: AccountManager, *, instruments: Iterable,
                  account: str, now_ms: Callable[[], int] | None = None) -> OkxRest:
        """装 REST 业务：T8 的 7 个只读 GET（T10 调一次，必须在 `bind_trading` 之后）。

        Args:
            book: 订单簿 —— 盘口查询的唯一真相。
            accounts: 账户账本 —— 余额查询的唯一真相。
            instruments: 合约规格序列，来自配置 `instruments[]`（`/account/instruments` 的内容；
                **必填**：漏传时该端点恒回空 `data`，引擎 `RefreshAllMarketInfo` 重试 5 次后 terminate）。
            account: 余额查询落的账户名（配置里的默认 `api_key`；**必填**，同上）。
            now_ms: 时钟（毫秒 epoch 取值函数）；缺省取系统时间，测试注入固定值。

        Returns:
            OkxRest：REST 业务对象（`routes()` 即 `rest_routes()` 交出的表）。

        Raises:
            RuntimeError: 尚未 `bind_trading` —— `/trade/*` 三个端点以私有频道的订单快照为唯一
                数据源，顺序反了装出来的 REST 面查不到任何委托，装配错误必须当场可见。
            RuntimeError: 重复装配 —— 会挂出第二套同路径路由（aiohttp 随后直接报重复注册）。
        """
        if self._private is None:
            raise RuntimeError("REST 面需要先 bind_trading：/trade/* 以私有频道的订单快照为唯一数据源")
        if self._rest is not None:
            raise RuntimeError("OKX REST 面已装配：重复 bind_rest 会挂出第二套同路径路由")
        self._rest = OkxRest(book, accounts, trading=self._private, instruments=instruments,
                             account=account, now_ms=now_ms)
        return self._rest

    def ws_public_handler(self, payload: dict, ws: web.WebSocketResponse, registry: WsRegistry) -> Reply:
        """公共频道（`subscribe` / `unsubscribe`）；语义见 `ws_public.public_handler`。

        订阅**成功**后按回执帧补推一次当前盘口（`_push_snapshots`）：引擎重连/重启时盘口若已有
        挂单，只靠"下一次盘口变化"驱动就是永远收不到行情。空盘口由 `push` 自己判空吞掉。
        """
        reply = public_handler(payload, ws, registry)
        self._push_snapshots(reply)
        return reply

    def _push_snapshots(self, reply: Reply) -> None:
        """逐条 `bbo-tbt` 订阅回执补一次 `BboBroadcaster.push`（未装配 bbo 桥时整段跳过）。

        以**回执帧**为准而不是重解析报文：回执只对真登记成功的 arg 产生，坏 arg 被 `public_handler`
        跳过时这里不会白推。只认 `bbo-tbt` —— 别的频道（`candles` 之类）订阅成功不该顺手推一帧行情。
        `unsubscribe` 与订阅失败同样不推。回执帧先写进传输层、快照帧随后（`registry.broadcast` 只把
        发送排成任务，本轮不插队），对客户端顺序稳定。
        """
        if self._bbo is None:
            return
        for frame in frames_of(reply):
            arg = frame.get("arg")
            if frame.get("event") != "subscribe" or not isinstance(arg, dict):
                continue
            inst_id = arg.get("instId")
            if arg.get("channel") != BBO_CHANNEL or not isinstance(inst_id, str) or not inst_id:
                continue
            self._bbo.push(inst_id)     # 空盘口：push 返回 None，一条帧都不发

    def ws_private_handler(self, payload: dict, ws: web.WebSocketResponse, registry: WsRegistry) -> Reply:
        """私有频道（`login` / `order` / `cancel-order` / 订阅）；语义见 `ws_private`。

        Raises:
            NotImplementedError: 尚未 `bind_trading`（未装配）—— 装配期被提前接上时当场可见；
                胶水会兜住（记 warning），连接不断。
        """
        if self._private is None:
            raise NotImplementedError("OKX 私有频道未装配：先 bind_trading(engine, accounts, bus, ...)")
        return self._private.handle(payload, ws, registry)
