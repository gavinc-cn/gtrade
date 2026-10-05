"""OKX 私有频道：`login` / `order` / `cancel-order` + `orders`/`positions`/`balance_and_position` 推送。

M1 最关键的一环 —— 引擎下单走的就是这条通道。三块职责：

1. **指令面**（同步 handler，回执由 `base.make_json_handler` 逐帧发出）：`login` 宽容放行
   （不校验签名/timestamp，凭据不进日志）；`order` → `MatchingEngine.place`；`cancel-order`
   → `MatchingEngine.cancel`；`subscribe` 登记 `orders`/`positions`/`balance_and_position`。
2. **推送面**：成交推 `orders` + `balance_and_position` + `positions` 帧（**主动方与被吃的挂单方
   各推一帧** —— 挂单被吃同样要回报，见 `_push_filled_makers`），撤单推 `orders` 帧
   （`state:"canceled"`）。私有频道不吃 instrument 约束，故一律 `broadcast(channel, None, …)`。
3. **事件面**：改动了订单簿就 `publish("book.<instId>")` —— `orderbook` 模式下挂单与成交都
   改簿（下单必发），撤单命中簿上挂单也发；`immediate` 下单是隐含流动性，不动盘口、不发事件
   （T6 的 bbo 桥只订阅不发布）。判定走 `MatchingEngine.uses_book`，协议层不自己判模式。

`orders` 行由 `codec.order_update` 构造：引擎对它**逐字段 `GetString()`**，缺键或类型错在 Debug
构建直接 `abort()`。`positions` 帧由本模块自建 —— T4 的 `snapshot()` 元素只有 `balData`/`posData`
且 `posData` 缺 `upl/uplRatio/notionalUsd`，而引擎无条件读 `upl`/`uplRatio`
（`src/websocket/okx_trade.cpp:611`）。

四处刻意的取舍：① **单账户** —— `owner` 恒为构造时的 `account`（登录报文的 apiKey 不参与归属
判定，一进程两连接属 M2），空则 `apply_fill` 拒收主动方；② **撤单要委托对象** —— `OrderBook.cancel`
只回 bool 且成功后委托已摘出索引，故维护 `cl_ord_id → Order` 索引（`op:"order"` 自动入，管理面注入
的挂单由 `track()` 登记）；③ **失败不抛异常也不静默** —— 参数错/未知合约/非限价单/撤单未命中回失败
ack（`sCode` 非 "0"）并记 warning，坏帧留痕后忽略；④ **账本帧只报本账户** —— 帧里没有账户字段，
被动方 `mock_mm` 的账属于订单簿侧。
"""

import logging
import math
import time
from decimal import Decimal

from mockex.core.account import AccountManager
from mockex.core.events import EventBus
from mockex.core.instrument_index import InstrumentSpecIndex, SpecLookupError
from mockex.core.matching import MatchingEngine
from mockex.core.models import SIDE_BUY, SIDE_SELL, Fill, Order
from mockex.exchanges.base import Reply
from mockex.exchanges.okx import codec
from mockex.protocol.ws import WsRegistry

__all__ = ["BALANCE_CHANNEL", "ORDER_CHANNEL", "POSITIONS_CHANNEL", "PrivateChannels", "positions_frame"]

# 引擎登录后固定订阅的三条私有频道（契约「WS 私有频道」小节）
ORDER_CHANNEL = "orders"
POSITIONS_CHANNEL = "positions"
BALANCE_CHANNEL = "balance_and_position"
_SUBSCRIBABLE = (ORDER_CHANNEL, POSITIONS_CHANNEL, BALANCE_CHANNEL)

_SUPPORTED_ORD_TYPE = "limit"    # 引擎只下限价单（dict_mapping.cpp 的 PriceType）
_DEFAULT_TD_MODE = "cash"        # SPOT 现货：引擎下单固定带 tdMode
_DEFAULT_POS_SIDE = "net"

_S_CODE_OK = "0"                  # sCode：非 "0" 即失败；取值对齐 OKX 错误码，便于对照官方文档排障
_S_CODE_PARAM = "51000"           # Parameter error（缺字段 / 类型错 / 非限价单）
_S_CODE_UNKNOWN_INST = "51001"    # Instrument ID does not exist
_S_CODE_NOT_FOUND = "51603"       # Order does not exist（撤单未命中）
_S_CODE_UNSUPPORTED = "50013"     # 服务端不支持（非限价单等未实现路径）

logger = logging.getLogger(__name__)


def _now_ms() -> int:
    return int(time.time() * 1000)  # 当前毫秒 epoch（调用方未注入时钟时的兜底）


def _positive(value):
    """下单报文里的 px / sz → float；非数值 / 非有限 / 非正一律 None（OKX 的价量字符串数字都认）。"""
    if isinstance(value, bool) or not isinstance(value, (int, float, str)):
        return None
    try:
        number = float(value)
    except ValueError:
        return None
    return number if math.isfinite(number) and number > 0 else None


def _text_or(value, default: str) -> str:
    """报文里的可选文本字段：非空字符串才采信，否则用默认值。"""
    return value if isinstance(value, str) and value else default


def _requested_cl_ord_id(arg) -> str:
    """失败 ack 里回显的 clOrdId：字符串原样用它，否则空串（引擎靠它认领失败单）。

    兼容引擎 WS 报文把整型 entno 直接当 clOrdId 发的历史行为（OKX 官方规范里该字段是
    字符串，引擎侧待修）：整型按十进制规范化成字符串回显，保证失败单仍可被引擎认领。"""
    value = arg.get("clOrdId") if isinstance(arg, dict) else None
    if isinstance(value, str):
        return value
    if isinstance(value, int) and not isinstance(value, bool):
        return str(value)
    return ""


def positions_frame(row: dict) -> dict:
    """`AccountManager.snapshot()` 的 posData 行 → `positions` 推送帧（补齐契约字段）。

    `upl`/`uplRatio` 恒为 `"0"`（mock 没有标记价编不出浮盈，但引擎**无条件读这两个键**）；
    `notionalUsd` 取 `|pos × avgPx|`（报价币即 USDT，空头同样报正数）。源行不做就地改写。"""
    filled = dict(row, upl="0", uplRatio="0")
    filled["notionalUsd"] = codec.num(abs(Decimal(str(row["pos"])) * Decimal(str(row["avgPx"]))))
    return {"arg": {"channel": POSITIONS_CHANNEL}, "data": [filled]}


class PrivateChannels:
    """OKX 私有频道业务（一个进程一个实例，由 `OkxAdapter.bind_trading` 装配）。

    方法全是同步的：入参是已解析的 JSON 对象，返回值是要回给客户端的帧（`base.Reply`），推送帧
    经 `registry.broadcast` 排给订阅者 —— 与 T6 的 `public_handler` 同一套约定。"""

    def __init__(self, engine: MatchingEngine, accounts: AccountManager, bus: EventBus, *,
                 instruments=(), account: str = "", now_ms=None) -> None:
        """Args:
            engine / accounts: 撮合引擎与账户账本（结算成交、出余额与持仓快照）。
            bus: 事件总线（盘口变化时发布 `book.<instId>`；M1 只有撤簿上外部挂单这一种）。
            instruments: 合约规格序列 —— 下单靠它判"合约是否存在"并取 `instType` 填委托与帧；
                索引为 `(instId, instType)` 双键（MARGIN 与 SPOT 共用 instId，单键会互相覆盖），
                见 `mockex/core/instrument_index.py`。
            account: 本实例落的账户名（配置里的默认 api_key）。
            now_ms: 时钟（毫秒 epoch 取值函数）；缺省系统时间，测试注入固定值。

        Raises:
            ValueError: `instruments` 为空（每笔下单都会撞"未知合约"）或 `account` 为空。
        """
        specs = InstrumentSpecIndex(instruments)
        if not account:
            raise ValueError("account 不能为空：成交必须有账户承接（apply_fill 的空 owner 守卫）")
        self._engine = engine
        self._accounts = accounts
        self._bus = bus
        self._specs = specs
        self._account = account
        self._now_ms = _now_ms if now_ms is None else now_ms
        self._next_ord_id = 1
        self._orders: dict[str, Order] = {}

    def track(self, order: Order) -> None:
        """登记一张**已挂进订单簿**的委托，使 `cancel-order` 能撤它并推 `state:"canceled"`。

        `op:"order"` 下的单自动入索引；管理面注入的挂单（T9）由 T9 调本方法 —— 漏登记的表现是
        "撤成功了但引擎收不到 canceled 帧"，只留 warning。
        """
        self._orders[order.cl_ord_id] = order

    def orders(self) -> list[Order]:
        """订单快照（只读，dict 序 = 下单先后）—— REST 的单笔/未成交/历史查询都从这里取。"""
        return list(self._orders.values())

    def reset(self) -> None:
        """清空委托索引（`/admin/reset`）；委托号计数器不回退：复用 ordId 会让引擎映射指回旧单。"""
        self._orders.clear()

    def handle(self, payload: dict, ws, registry: WsRegistry) -> Reply:
        """私有频道总入口（`ExchangeAdapter.ws_private_handler` 的实现）。

        `payload` 是已解析的 JSON 对象，`ws` / `registry` 是订阅登记的键与推送出口。返回单 arg →
        单帧 dict、多 arg → 逐 arg 一帧的 list、未知 op / 无有效 arg → None（推送帧不经返回值）。"""
        op = payload.get("op")
        if op == "login":
            return self._login()
        if op in ("subscribe", "unsubscribe"):
            return self._subscriptions(payload, ws, registry, op)
        if op == "order":
            return self._place(payload, registry)
        if op == "cancel-order":
            return self._cancel(payload, registry)
        logger.warning("忽略不支持的私有频道 op: %r", op)
        return None

    @staticmethod
    def _login() -> dict:
        """登录回执：**宽容放行**（M1 不校验签名/timestamp）；凭据一个字段都不进日志。"""
        return {"event": "login", "code": _S_CODE_OK}

    def _subscriptions(self, payload: dict, ws, registry: WsRegistry, op: str) -> Reply:
        """`subscribe` / `unsubscribe`：登记 / 摘除频道级订阅（私有频道不吃 instId 约束）。"""
        args = payload.get("args")
        if not isinstance(args, list) or not args:
            logger.warning("忽略无有效 args 的私有频道 %s 报文: %r", op, payload)
            return None
        replies: list[dict] = []
        for arg in args:
            channel = arg.get("channel") if isinstance(arg, dict) else None
            if channel not in _SUBSCRIBABLE:
                logger.warning("忽略不支持的私有频道订阅: %r", arg)
                continue
            if op == "subscribe":
                registry.subscribe(ws, channel, None)
            else:
                registry.unsubscribe(ws, channel, None)
            echoed = {"channel": channel}   # 回执原样回显客户端给的定位字段（真实 OKX 也这么回）
            echoed.update({k: arg[k] for k in ("instType", "instId") if isinstance(arg.get(k), str) and arg[k]})
            replies.append({"event": op, "arg": echoed})
        if not replies:
            return None
        return replies[0] if len(replies) == 1 else replies

    def _place(self, payload: dict, registry: WsRegistry) -> Reply:
        """`op:"order"`：逐 arg 落进撮合 → 回 ack →（成交时）推帧 + 发布盘口事件。"""
        args = payload.get("args")
        if not isinstance(args, list) or not args:
            logger.warning("忽略无有效 args 的私有频道 order 报文: %r", payload)
            return None
        replies = [self._place_one(payload.get("id"), arg, registry) for arg in args]
        return replies[0] if len(replies) == 1 else replies

    def _place_one(self, req_id, arg, registry: WsRegistry) -> dict:
        """一张委托：校验 → 撮合 → 结算 → 推送 → 回 ack（失败也回 ack，`sCode` 非 "0"）。"""
        order, s_code, s_msg = self._build_order(arg)
        if order is None:
            return self._reject(req_id, _requested_cl_ord_id(arg), s_code, s_msg, "order")
        before = self._account_orders_snapshot()    # 撮合前快照：撮合后对比找出被动成交的挂单
        try:
            fills = self._engine.place(order)
            for fill in fills:      # 结算两侧；被动方（注入方）的账本不推给引擎，见 _push_account
                self._accounts.apply_fill(fill)
        except NotImplementedError as exc:      # 非限价单等未实现路径
            return self._reject(req_id, order.cl_ord_id, _S_CODE_UNSUPPORTED, str(exc), "order")
        except ValueError as exc:               # 撮合/结算的守卫（未知合约、价量非法…）
            return self._reject(req_id, order.cl_ord_id, _S_CODE_PARAM, str(exc), "order")
        self._orders[order.cl_ord_id] = order
        self._push_order(order, fills, registry)
        self._push_filled_makers(before, order, registry)   # 被吃的挂单同样要成交回报
        self._push_account(registry)
        # orderbook 模式：挂单/吃单都改了盘口，必须让 bbo 桥推一帧；immediate 不动盘口、不发
        if self._engine.uses_book:
            self._publish_book(order.inst_id)
        return codec.order_ack("order", req_id, order.cl_ord_id, order.ord_id, ts_ms=self._now_ms())

    def _cancel(self, payload: dict, registry: WsRegistry) -> Reply:
        """`op:"cancel-order"`：撤簿上的挂单 → 回 ack →（命中时）推 canceled 帧 + 发盘口事件。

        未命中（已成交 / 从未挂过 / 已撤）回 `sCode:51603`；`immediate` 下引擎自己的单从没
        进过簿（只有管理面注入的挂单可撤），`orderbook` 下引擎挂单的余量就在簿上。
        """
        args = payload.get("args")
        if not isinstance(args, list) or not args:
            logger.warning("忽略无有效 args 的私有频道 cancel-order 报文: %r", payload)
            return None
        replies = [self._cancel_one(payload.get("id"), arg, registry) for arg in args]
        return replies[0] if len(replies) == 1 else replies

    def _cancel_one(self, req_id, arg, registry: WsRegistry) -> dict:
        """一张撤单：按 clOrdId 命中订单簿即撤、推帧、发事件；否则回失败 ack。"""
        cl_ord_id = _requested_cl_ord_id(arg)
        if not cl_ord_id:
            return self._reject(req_id, "", _S_CODE_PARAM, "clOrdId 必填且非空", "cancel-order")
        order = self._orders.get(cl_ord_id)
        if not self._engine.cancel(cl_ord_id):
            logger.warning("私有频道撤单未命中 clOrdId=%s（已成交 / 不在簿上）", cl_ord_id)
            ord_id = "" if order is None else order.ord_id
            return codec.order_ack("cancel-order", req_id, cl_ord_id, ord_id, _S_CODE_NOT_FOUND,
                                   "order does not exist", ts_ms=self._now_ms())
        if order is None:
            # 簿上真撤掉了但没对象可编码（外部挂单漏调 track）：ack 必须报成功，只留 warning
            logger.warning("已撤单 %s 但无委托对象可推送：外部挂单请先 track()", cl_ord_id)
            return codec.order_ack("cancel-order", req_id, cl_ord_id, "", ts_ms=self._now_ms())
        self._push_order(order, (), registry)
        self._publish_book(order.inst_id)
        return codec.order_ack("cancel-order", req_id, cl_ord_id, order.ord_id, ts_ms=self._now_ms())

    def _reject(self, req_id, cl_ord_id: str, s_code: str, s_msg: str, op: str) -> dict:
        """失败 ack（`sCode` 用 OKX 码、`sMsg` 带上原因）+ 一条 warning 留痕。"""
        logger.warning("拒绝私有频道 %s clOrdId=%s: %s(%s)", op, cl_ord_id, s_msg, s_code)
        return codec.order_ack(op, req_id, cl_ord_id, "", s_code, s_msg, ts_ms=self._now_ms())

    def _build_order(self, arg):
        """下单报文的一个 arg → `Order`；解析不了返回 `(None, sCode, sMsg)`。

        只支持限价单（市价/PostOnly 一律回失败 ack）；`instIdCode` 不校验：mock 只认 `instId`。

        合约规格按 `(instId, instType)` 精确取：`instType` 是标的唯一性的一部分（币币杠杆复用
        现货的 instId）。而引擎的下单报文**不带 `instType`**，故交给
        `InstrumentSpecIndex.resolve` 解析（instId 唯一即直取；多种候选才用 `tdMode` 消歧），
        并顺带校验 `tdMode` 与合约类型自洽 —— `cash` 配合约、`cross`/`isolated` 配现货都会
        被拒（后者表示币币杠杆，本模拟盘不提供）。"""
        if not isinstance(arg, dict):
            return None, _S_CODE_PARAM, "args 元素必须是对象"
        cl_ord_id = arg.get("clOrdId")
        # 引擎 WS 下单把整型 entno 直接作为 clOrdId 发出（OKX 规范里该字段是字符串，引擎侧待修，
        # 见 doc_ai/bug_report）——这里在协议边界做数字→字符串规范化：簿、撤单、查单、推送全都按
        # 字符串 clOrdId 索引，规范化必须发生在入簿之前，否则撤单/查单按 to_string 对不上键。
        if isinstance(cl_ord_id, int) and not isinstance(cl_ord_id, bool):
            cl_ord_id = str(cl_ord_id)
        if not isinstance(cl_ord_id, str) or not cl_ord_id:
            return None, _S_CODE_PARAM, "clOrdId 必填且非空"
        inst_id = arg.get("instId")
        if not isinstance(inst_id, str):
            return None, _S_CODE_UNKNOWN_INST, f"未知合约: {inst_id!r}"
        inst_type = arg.get("instType")
        if not isinstance(inst_type, str):
            inst_type = ""
        try:
            # 委托帧不带 instType 是合法缺省；tdMode 只用于消歧与合法性校验，不参与类型推导
            spec = self._specs.resolve(inst_id, inst_type, _text_or(arg.get("tdMode"), ""))
        except SpecLookupError as exc:
            code = _S_CODE_UNKNOWN_INST if exc.unknown_inst else _S_CODE_PARAM
            return None, code, exc.reason
        side = arg.get("side")
        if side not in (SIDE_BUY, SIDE_SELL):
            return None, _S_CODE_PARAM, f"side 必须是 buy / sell: {side!r}"
        if arg.get("ordType") != _SUPPORTED_ORD_TYPE:
            return None, _S_CODE_PARAM, f"ordType 只支持 limit: {arg.get('ordType')!r}"
        px, sz = _positive(arg.get("px")), _positive(arg.get("sz"))
        if px is None or sz is None:
            return None, _S_CODE_PARAM, f"px/sz 必须是正的有限数值: px={arg.get('px')!r} sz={arg.get('sz')!r}"
        ts = self._now_ms()
        order = Order(cl_ord_id=cl_ord_id, ord_id=str(self._next_ord_id), inst_id=spec.inst_id,
                      side=side, px=px, sz=sz, owner=self._account, c_time=ts, u_time=ts,
                      inst_type=spec.inst_type, ord_type=_SUPPORTED_ORD_TYPE,
                      td_mode=_text_or(arg.get("tdMode"), _DEFAULT_TD_MODE),
                      pos_side=_text_or(arg.get("posSide"), _DEFAULT_POS_SIDE))
        self._next_ord_id += 1
        return order, _S_CODE_OK, ""

    def _push_order(self, order: Order, fills, registry: WsRegistry) -> None:
        """`orders` 帧（不分合约：`inst_id=None` 命中所有订了 orders 的私有连接）。"""
        registry.broadcast(ORDER_CHANNEL, None, codec.order_update(order, fills))

    def _account_orders_snapshot(self) -> dict[str, tuple[str, float]]:
        """本账户在册委托的 `(state, 累计成交量)` 快照 —— 撮合前后对比用。

        只收本账户（引擎自己下的单）：管理面注入的 `mock_mm` 挂单也登记在册，但它们不属于任何
        客户端，给它们推 `orders` 帧只会让引擎按未知 clOrdId 空转。
        """
        return {cl_ord_id: (order.state, order.acc_fill_sz)
                for cl_ord_id, order in self._orders.items() if order.owner == self._account}

    def _push_filled_makers(self, before: dict[str, tuple[str, float]], taker: Order,
                            registry: WsRegistry) -> None:
        """被动方（簿上被吃掉的挂单）也推 `orders` 帧 —— 真实 OKX 双方都收成交回报。

        `orderbook` 模式下引擎自己的挂单可能被后下的单吃掉，只推主动方会让引擎的委托列表停在
        `已报 / filled=0`（挂单实际已部分成交），故这里按"撮合前后状态差"补推被动方帧。

        帧里携带的成交按"该挂单本次被吃的量"合成：价格-时间优先下成交价恒等于**被动方挂单价**
        （`OrderBook.consume_best` 取的就是该档价），故无需回查撮合明细即可还原 `fillPx`/`fillSz`。
        """
        for cl_ord_id, (state, acc_fill_sz) in before.items():
            maker = self._orders.get(cl_ord_id)
            if maker is None or maker is taker:
                continue
            delta = maker.acc_fill_sz - acc_fill_sz
            if delta <= 0 or (maker.state, maker.acc_fill_sz) == (state, acc_fill_sz):
                continue    # 本次撮合没碰它
            fill = Fill(ord_id=maker.ord_id, inst_id=maker.inst_id, px=maker.px, sz=delta,
                        ts=maker.u_time, taker_owner=taker.owner, maker_owner=maker.owner,
                        side=SIDE_BUY if maker.side == SIDE_SELL else SIDE_SELL)   # 被动方是反向
            self._push_order(maker, [fill], registry)

    def _push_account(self, registry: WsRegistry) -> None:
        """推本账户的 `balance_and_position` 帧与逐行的 `positions` 帧。

        帧里都没有账户标识，故只推 `self._account`；被动方（`mock_mm`）的账只记在订单簿侧。"""
        element = self._accounts.snapshot(self._account)
        registry.broadcast(BALANCE_CHANNEL, None,
                           {"arg": {"channel": BALANCE_CHANNEL}, "data": [element]})
        for row in element["posData"]:
            registry.broadcast(POSITIONS_CHANNEL, None, positions_frame(row))

    def _publish_book(self, inst_id: str) -> None:
        """盘口变化的唯一信号：T6 的 bbo 桥与 T9 的注入路径都订这个 topic（payload 不使用）。"""
        self._bus.publish(f"book.{inst_id}", None)
