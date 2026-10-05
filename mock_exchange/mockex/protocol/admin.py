"""管理面 API（`/admin/*`）：外部用一条 curl 造盘口 / 注入行情 / 重置环境。

管理面**不是 OKX 协议面**：引擎永远不访问它，只有人、测试脚本（T11）与 M3 的回放客户端会用。
它与协议面分成两个监听（T10 只把本路由表挂到 `127.0.0.1:<admin_port>`），因为 `/reset` 与
`/depth` 有重置与注盘能力，绝不能对外暴露。

六个端点：`POST /admin/reset`（回干净环境）、`POST /admin/order`（`{instId, side, px, sz, owner?,
clOrdId?}` 造盘口）、`POST /admin/cancel`（`{clOrdId}` 撤单）、`POST /admin/depth`（`{instId,
bids, asks}` 注入多档，落成 `mock_mm` 挂单并替换上一批）、`GET /admin/state`（全量快照）、
`GET /admin/events?limit=N`（最近 N 条管理面调用与盘口事件）。

四条定死的语义（前两条漏了的表现是"引擎静默收不到帧"，测试逐条守着）：

1. **凡改订单簿都要发 `book.<instId>`**（四个写端点全发，一次调用一条）：T6 的 bbo 桥只订阅不
   发布，漏了它引擎永远收不到这帧行情，而 mock 侧日志上什么都看不出来；
2. **挂单必须 `trading.track()`**：T7 的 `cancel-order` 靠 `cl_ord_id → Order` 索引推
   `state:"canceled"`，漏登记的表现是"撤单 ack 正常但引擎收不到撤单回报"（只留一条 warning）；
3. **注入是替换式的**（spec「注入归一化」）：同 instId 再次注入先撤上一批 `mock_mm` 再挂新的；
4. **报错回 4xx + 一句说明**：参数错 400、委托找不到 404（抛 HTTP 异常），不回 500、不用 OKX 外壳。

**id 必须是十进制整数串且落在 int64 内**（自产 id 从 `_ID_BASE` 起算）：引擎把 `clOrdId`/`ordId`
都按 `int64` 读（`OkexClient.cpp:360` → `boost::lexical_cast`），非数字串与溢出值都会被静默吞成 0
（注入单互相覆盖）并刷错误日志；而注入的挂单又会被 REST 的 `orders-pending`/`orders-history` 带走。
"""

import json
import math
import time
from collections import deque
from functools import partial

from aiohttp import web

from mockex.core.account import AccountManager
from mockex.core.book import OrderBook
from mockex.core.events import EventBus
from mockex.core.instrument_index import InstrumentSpecIndex, SpecLookupError
from mockex.core.models import SIDE_BUY, SIDE_SELL, Order
from mockex.exchanges.base import RestRoute
from mockex.exchanges.okx import codec
from mockex.exchanges.okx.ws_private import PrivateChannels
from mockex.protocol.ws import KIND_PRIVATE, KIND_PUBLIC, WsRegistry

__all__ = ["MAX_EVENTS", "MOCK_MM_OWNER", "AdminApi"]

MOCK_MM_OWNER = "mock_mm"   # 注入行情的归属（spec「注入归一化」）：`/admin/depth` 整批按它撤换
MAX_EVENTS = 1000           # 事件环容量（`/admin/events` 最多回这么多条）
DEFAULT_EVENT_LIMIT = 100   # `limit` 缺省值
_ID_BASE = 900_000_001      # 自产 id 起点：避开引擎的 entno 与 T7 的小整数 ordId 计数器
_MAX_INT64 = 2 ** 63 - 1    # 引擎按 int64 读 clOrdId/ordId：越界的号会被 lexical_cast 吞成 0


def _fail(message: str, status: int = 400):
    """失败应答（**抛**给 aiohttp）：`{"ok": false, "error": 说明}`，说明要能定位到肇事字段，一律 4xx。"""
    body = json.dumps({"ok": False, "error": message}, ensure_ascii=False)
    factory = web.HTTPNotFound if status == 404 else web.HTTPBadRequest
    return factory(text=body, content_type="application/json")


def _now_ms() -> int:
    return int(time.time() * 1000)      # 当前毫秒 epoch（调用方未注入时钟时的兜底）


def _positive(value):
    """JSON 里的价 / 量 → float；非数值 / 布尔 / 非有限 / 非正一律 None（消息里回原值）。"""
    if isinstance(value, bool):
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) and number > 0 else None


def _text(value):
    """可选文本字段：去空白后非空才采信，其余（含非字符串）→ None。"""
    return value.strip() if isinstance(value, str) and value.strip() else None


def _positive_int(text: str, ceiling: int):
    """十进制整数串 → int，仅当 `1 <= 值 <= ceiling`；非 ASCII 数字 / 越界 / 超长一律 None。"""
    digits = text.lstrip("0")   # 先按位数筛再 int()：>4300 位的串会让 CPython 3.10.7+ 抛 ValueError
    if not digits.isascii() or not digits.isdigit() or len(digits) > len(str(ceiling)):
        return None
    value = int(digits)
    return value if 1 <= value <= ceiling else None


def _levels(raw, default_sz: float, field: str) -> list[tuple[float, float]]:
    """`bids` / `asks` → `[(px, sz)]`；一档写 `[px, sz]` 或 `[px]`（量取 `default_sz`），坏形状 400。"""
    if raw is None:
        return []
    if not isinstance(raw, list):
        raise _fail(f"{field} 必须是数组: {raw!r}")
    levels = []
    for item in raw:
        if not isinstance(item, list) or not 1 <= len(item) <= 2:
            raise _fail(f"档位必须是 [px, sz] 或 [px]: {item!r}")
        px, sz = _positive(item[0]), _positive(default_sz if len(item) == 1 else item[1])
        if px is None or sz is None:
            raise _fail(f"档位价量必须是正的有限数值: {item!r}")
        levels.append((px, sz))
    return levels


async def _json_body(request: web.Request) -> dict:
    """请求体 → JSON 对象；空 body / 坏 JSON / 非对象一律 400（`curl -d` 不带 header 也认）。"""
    try:
        payload = await request.json()
    except ValueError as exc:       # json.JSONDecodeError 与编码错都归这里
        raise _fail(f"请求体必须是 JSON 对象: {exc}") from exc
    if not isinstance(payload, dict):
        raise _fail(f"请求体必须是 JSON 对象，实得 {type(payload).__name__}")
    return payload


class AdminApi:
    """管理面六个端点；一个进程一个实例，由 T10 挂到 `127.0.0.1:<admin_port>`（应答是原生 JSON 数值）。"""

    def __init__(self, book: OrderBook, accounts: AccountManager, bus: EventBus,
                 registry: WsRegistry, *, trading: PrivateChannels, instruments=(),
                 default_sz: float = 1.0, fill_mode: str = "immediate", now_ms=None) -> None:
        """Args:
            book / accounts / bus / registry: 订单簿、账户账本、事件总线、WS 会话表。
            trading: T7 的私有频道业务（`track()` 登记注入挂单、`orders()` 查委托、`reset()` 清索引）。
            instruments: 合约规格序列（配置 `instruments[]`）：校验 instId、决定订阅哪些盘口 topic。
            default_sz / fill_mode: 注入默认量（`injection.default_sz`）、撮合模式（只供 state 回报）。
            now_ms: 时钟（毫秒 epoch 取值函数）；缺省系统时间，测试注入固定值。

        Raises:
            ValueError: `trading` 缺失 / `instruments` 为空 / `default_sz` 非正 —— 都是装配错误。"""
        if trading is None:
            raise ValueError("trading 不能为空：管理面的挂单登记与查单以 T7 的委托索引为唯一真相")
        specs = InstrumentSpecIndex(instruments)
        if _positive(default_sz) is None:
            raise ValueError(f"default_sz 必须是正的有限数值: {default_sz!r}")
        self._book, self._accounts = book, accounts
        self._bus, self._registry = bus, registry
        self._trading, self._specs = trading, specs
        self._default_sz, self._fill_mode = float(default_sz), fill_mode
        self._now_ms = _now_ms if now_ms is None else now_ms
        self._next_id = _ID_BASE
        self._events: deque = deque(maxlen=MAX_EVENTS)
        for inst_id in specs.inst_ids():   # 谁改的簿都记：盘口事件是 `/admin/events` 的第二类记录
            bus.subscribe(f"book.{inst_id}", partial(self._on_book, inst_id))

    def routes(self) -> list[RestRoute]:
        """六条路由；T10 逐条 `app.router.add_route(method, path, handler)`。"""
        return [("POST", "/admin/reset", self.reset), ("POST", "/admin/order", self.order),
                ("POST", "/admin/cancel", self.cancel), ("POST", "/admin/depth", self.depth),
                ("GET", "/admin/state", self.state), ("GET", "/admin/events", self.events)]

    async def reset(self, request: web.Request) -> web.Response:
        """`POST /admin/reset`：订单簿 / 委托 / 账户 / 事件环全清，并重发每个合约的盘口事件。"""
        self._record("admin.reset", {})
        return web.json_response(self._reset())

    async def order(self, request: web.Request) -> web.Response:
        """`POST /admin/order`：以指定 owner 造盘口（`owner` 缺省即注入方 `mock_mm`）。"""
        return await self._mutating("admin.order", request, self._place)

    async def cancel(self, request: web.Request) -> web.Response:
        """`POST /admin/cancel`：撤指定委托；找不到 / 不在簿上都回 404（附原因）。"""
        return await self._mutating("admin.cancel", request, self._cancel)

    async def depth(self, request: web.Request) -> web.Response:
        """`POST /admin/depth`：注入盘口；同 instId 先撤上一批 `mock_mm` 挂单再挂新的。"""
        return await self._mutating("admin.depth", request, self._inject)

    async def state(self, request: web.Request) -> web.Response:
        """`GET /admin/state`：全量快照；委托行在 OKX 行上补 `owner`（分辨自营与注入）。"""
        self._record("admin.state", {})
        return web.json_response({
            "book": self._book.snapshot(),
            "orders": [dict(codec.order_row(one), owner=one.owner) for one in self._trading.orders()],
            "accounts": self._accounts.snapshot(),
            "ws": {"public": self._registry.count(KIND_PUBLIC), "private": self._registry.count(KIND_PRIVATE)},
            "fill_mode": self._fill_mode,
        })

    async def events(self, request: web.Request) -> web.Response:
        """`GET /admin/events?limit=N`：最近 N 条事件，最老在前（环形缓冲的天然顺序）。"""
        raw = request.query.get("limit")
        text = "" if raw is None else str(raw).strip()
        value = DEFAULT_EVENT_LIMIT if not text else _positive_int(text, _MAX_INT64)
        if value is None:   # 空串回落默认值；非 ASCII 数字 / 非正 / 超长（int() 会炸）一律 400
            raise _fail(f"limit 必须是正整数: {raw!r}")
        limit = min(value, MAX_EVENTS)      # 超过环容量按容量回（钳位），不报错
        body = web.json_response(list(self._events)[-limit:])
        self._record("admin.events", {"limit": limit})      # 自身这条在应答快照之后才入环
        return body

    # ── 业务（同步；4xx 靠 `_fail` 抛 aiohttp 异常冒泡） ──────────────────────

    async def _mutating(self, kind: str, request: web.Request, action) -> web.Response:
        """写端点的统一壳：解析 JSON → 记参数摘要 → 执行（被拒的 body 由 `_fail` 直接回 4xx）。"""
        payload = await _json_body(request)
        self._record(kind, payload)
        return web.json_response(action(payload))

    def _place(self, payload: dict) -> dict:
        """`/admin/order`：校验 → 挂进订单簿 → `track()` 登记 → 发盘口事件。"""
        spec = self._spec_of(payload)
        inst_id = spec.inst_id
        side = payload.get("side")
        if side not in (SIDE_BUY, SIDE_SELL):
            raise _fail(f"side 必须是 buy / sell: {side!r}")
        px, sz = _positive(payload.get("px")), _positive(payload.get("sz"))
        if px is None or sz is None:    # 两个字段都回原值：一眼看出是哪个写坏了
            raise _fail(f"px/sz 必须是正的有限数值: px={payload.get('px')!r} sz={payload.get('sz')!r}")
        owner = _text(payload.get("owner")) or MOCK_MM_OWNER
        order = self._make_order(spec, side, px, sz, owner, self._requested_id(payload))
        self._attach(order)
        self._publish(inst_id)
        return {"ok": True, "clOrdId": order.cl_ord_id, "ordId": order.ord_id, "instId": inst_id,
                "side": side, "px": px, "sz": sz, "owner": owner}

    def _inject(self, payload: dict) -> dict:
        """`/admin/depth`：先撤同源上一批再挂新的，全套落成 `mock_mm` 挂单（替换语义）。"""
        spec = self._spec_of(payload)
        inst_id = spec.inst_id
        bids = _levels(payload.get("bids"), self._default_sz, "bids")
        asks = _levels(payload.get("asks"), self._default_sz, "asks")
        if not bids and not asks:
            raise _fail("bids/asks 至少要给一档（两边都空等于什么都没注入）")
        replaced = self._book.remove_owner(MOCK_MM_OWNER, inst_id)
        for side, levels in ((SIDE_BUY, bids), (SIDE_SELL, asks)):
            for px, sz in levels:
                self._attach(self._make_order(spec, side, px, sz, MOCK_MM_OWNER))
        self._publish(inst_id)      # 整批挂完只发一次：引擎收到的那一帧就是注入后的最终盘口
        return {"ok": True, "instId": inst_id, "bids": len(bids), "asks": len(asks),
                "replaced": replaced}

    def _cancel(self, payload: dict) -> dict:
        """`/admin/cancel`：按 clOrdId 撤簿上挂单并补发盘口事件。

        簿上可能有管理面自造的挂单（`owner=mock_mm`），`orderbook` 模式下也有引擎委托的余量；
        两者的取消帧推送不同：管理面挂单由本接口撤（无 `orders` 帧），引擎委托由 T7 的
        `cancel-order` 撤（引擎收 `state:"canceled"` 帧）。"""
        cl_ord_id = _text(payload.get("clOrdId"))
        if cl_ord_id is None:
            raise _fail("clOrdId 必填且非空")
        order = next((one for one in self._trading.orders() if one.cl_ord_id == cl_ord_id), None)
        if order is None:
            raise _fail(f"委托不存在: {cl_ord_id!r}", status=404)
        if not self._book.cancel(cl_ord_id):
            raise _fail(f"委托不在簿上（已成交或已撤）: {cl_ord_id!r}", status=404)
        self._publish(order.inst_id)
        return {"ok": True, "clOrdId": cl_ord_id, "instId": order.inst_id, "owner": order.owner}

    def _reset(self) -> dict:
        """清四份状态并重发每个合约的盘口事件；事件环**最后**清（连本次记录一起抹掉）。"""
        self._book.clear()
        self._accounts.reset()
        orders = len(self._trading.orders())    # `reset()` 只清索引，张数得在清之前取
        self._trading.reset()
        for inst_id in self._specs.inst_ids():
            self._publish(inst_id)              # 空盘口也要发：引擎据此知道"行情没了"
        self._events.clear()
        return {"ok": True, "orders": orders}

    def _spec_of(self, payload: dict):
        """按请求体的 `instId` + 可选 `instType`/`tdMode` 取规格。

        `instType` 可选：出厂清单里每个 instId 只有一种类型（MARGIN 已移除），不写即直取；
        若人工补抓了 MARGIN 导致同 instId 多类型，则需 `instType` 或 `tdMode` 消歧。
        `tdMode` 只参与消歧与合法性校验（现货只收 cash，合约只收 cross/isolated）。

        Raises:
            _fail: instId 缺失，或规格定位失败（报错文案来自 `SpecLookupError.reason`）。
        """
        inst_id = _text(payload.get("instId"))
        if inst_id is None:
            raise _fail("instId 必填且非空")
        inst_type = _text(payload.get("instType")) or ""
        td_mode = _text(payload.get("tdMode")) or ""
        try:
            return self._specs.resolve(inst_id, inst_type, td_mode)
        except SpecLookupError as exc:
            raise _fail(exc.reason) from exc

    def _requested_id(self, payload: dict):
        """调用方给的 clOrdId（可选）：必须是**没被占用**且落在 int64 内的十进制整数串。"""
        value = payload.get("clOrdId")
        if value is None:
            return None
        cl_ord_id = _text(value)
        if cl_ord_id is None or _positive_int(cl_ord_id, _MAX_INT64) is None:
            raise _fail(f"clOrdId 必须是 1..2^63-1 的十进制整数串（引擎按 int64 读）: {value!r}")
        if any(one.cl_ord_id == cl_ord_id for one in self._trading.orders()):
            raise _fail(f"clOrdId 已被占用: {cl_ord_id!r}")
        return cl_ord_id

    def _make_order(self, spec, side: str, px: float, sz: float, owner: str,
                    cl_ord_id=None) -> Order:
        """造一张即将挂上簿的委托；clOrdId 未给时与 ordId 同号（自产，见 `_ID_BASE`）。

        `instType` 取自规格（双键命中的那条），保证委托帧的 `instType` 与请求指定的合约类型一致。
        """
        ordinal = str(self._next_id)
        self._next_id += 1
        ts = self._now_ms()
        return Order(cl_ord_id=cl_ord_id or ordinal, ord_id=ordinal, inst_id=spec.inst_id, side=side,
                     px=px, sz=sz, owner=owner, c_time=ts, u_time=ts,
                     inst_type=spec.inst_type)

    def _attach(self, order: Order) -> None:
        """挂进订单簿 + 登记进 T7 的委托索引（两件事必须同进同退，故收在一处）。"""
        try:
            self._book.place(order)
        except ValueError as exc:       # clOrdId 已在簿上之类：管理面回 400，不留半挂状态
            raise _fail(f"挂单被拒: {exc}") from exc
        self._trading.track(order)

    def _publish(self, inst_id: str) -> None:
        """盘口变化的唯一信号（T6 的 bbo 桥与 `/admin/events` 都订它，payload 不用）。"""
        self._bus.publish(f"book.{inst_id}", None)

    def _record(self, kind: str, detail) -> None:
        """记一条事件（管理面调用 / 盘口变化），供 `/admin/events` 与排障使用。"""
        self._events.append({"ts": self._now_ms(), "kind": kind, "detail": detail})

    def _on_book(self, inst_id: str, payload) -> None:
        """`book.<instId>` 订阅回调（payload 由发布方定；本模块只用 topic 里的 instId）。"""
        self._record("book", {"instId": inst_id})
