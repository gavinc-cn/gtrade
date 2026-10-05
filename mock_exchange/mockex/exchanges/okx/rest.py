"""OKX v5 REST 七端点（全部只读）—— 引擎查合约 / 盘口 / K 线 / 余额 / 委托的那七个 GET。

逐端点的字段契约见 `doc_ai/spec/mock_exchange/引擎OKX契约.md` 的「REST」小节。本模块只做两件事：
**从内存状态取数**（订单簿 / 账户账本 / 私有频道的订单快照）与**交给 codec 编码**（`codec` 是字段
类型的唯一出口，本模块不自己拼数值字符串 —— 一个 `str(float)` 就能把 `1e-08` 漏进报文）。

四条刻意的取舍：

1. **路径照抄引擎**：`/api/v5/account/instruments` 不是官方路径（官方对应 `/public/instruments`），
   是引擎实际发出的那一条（日志实证），mock 必须挂在同一个路径上。
2. **空值 query 一律当"没给"**：引擎的 `JoinUrl` 会把建出来的空串原样拼上
   （`instId=&ordType=&state=&end=`），故参数解析统一走 `_q()`；且**任何情况都回 HTTP 200 + OKX
   外壳**（失败也是 `code != "0"`，不是 400/422）—— 引擎对非 200 只看得到"http get failed"。
3. **数据源只有三处**：盘口取 `OrderBook`、余额取 `AccountManager`、委托取
   `PrivateChannels.orders()` 的只读快照。不存在第二份状态，所以 REST 的答案与 WS 推送永不矛盾。
4. **路由以表交出**：`routes()` 返回 `(method, path, handler)` 三元组（`base.RestRoute`），T10 逐条
   `app.router.add_route` —— 本模块不认识 aiohttp app，也不知道自己挂在哪个端口。

两个由数据源决定的"空"：`/market/candles` 没有数据源（M1 不回放 K 线），按 M1 裁定返回**空 `data`
数组** —— 引擎的 `QryKLine` 只要结果数不足 `limit` 就结束分页，空数组合法且终止；`/trade/order`
查不到委托则回 OKX 的 `51603`（真实 OKX 对不存在的委托正是这个码），而不是 `code:"0"` + 空数组 ——
后者会让引擎静默当成"这张单不在交易所"，错误码才能让它显式回 `kQueryOrderErr`。
"""

import logging
import time

from aiohttp import web

from mockex.core.account import AccountManager
from mockex.core.book import OrderBook
from mockex.core.models import STATE_CANCELED, STATE_FILLED, STATE_LIVE, STATE_PARTIALLY_FILLED
from mockex.exchanges.base import RestRoute
from mockex.exchanges.okx import codec
from mockex.exchanges.okx.ws_private import PrivateChannels

__all__ = ["OkxRest"]

logger = logging.getLogger(__name__)

DEFAULT_LIMIT = 100       # OKX 的分页上限；引擎恒发 `limit=100`（见契约的 REST 表）
MAX_LEVELS = 10           # 单侧最多发 10 档：引擎 `Depth` 的档位数组固定 10 长且解析无边界检查
_PENDING_STATES = (STATE_LIVE, STATE_PARTIALLY_FILLED)     # 未成交：在簿 or 被吃了一半
_TERMINAL_STATES = (STATE_FILLED, STATE_CANCELED)          # 已终态 → 历史委托
_S_CODE_NO_ORDER = "51603"        # Order does not exist（真实 OKX 对不存在委托的码）
_S_CODE_NO_ACCOUNT = "59000"      # System error：配置的账户不在账本里（装配问题，不是客户端错）

# query 参数 → `Order` 字段：非空才作为过滤条件，空串一律视为"没这个条件"
_FILTERS = (("instType", "inst_type"), ("instId", "inst_id"), ("ordType", "ord_type"),
            ("state", "state"), ("clOrdId", "cl_ord_id"), ("ordId", "ord_id"))


def _now_ms() -> int:
    return int(time.time() * 1000)  # 当前毫秒 epoch（调用方未注入时钟时的兜底）


def _q(request: web.Request, key: str) -> str:
    """取 query 参数；缺失与空串一视同仁（引擎 `JoinUrl` 会拼出 `instId=`）—— 均返回空串。"""
    return (request.query.get(key) or "").strip()


def _int_or_none(text: str) -> int | None:
    """整数字符串 → int；空串 / 非数字 → None（`limit`、`begin`/`end`、分页游标共用）。"""
    try:
        return int(text)
    except ValueError:
        return None


def _limit(request: web.Request) -> int:
    """`limit`：缺失 / 空串 / 非数字 / 非正一律回落 100，上限也是 100（OKX 的取值空间）。"""
    value = _int_or_none(_q(request, "limit"))
    return DEFAULT_LIMIT if value is None or value < 1 else min(value, DEFAULT_LIMIT)


def _seq(order) -> int:
    """委托序号（`ordId` 数值化）：mock 自产的 ordId 递增整数；非数字排最后（-1）。"""
    value = _int_or_none(str(order.ord_id))
    return -1 if value is None else value


def _filter(orders, request: web.Request) -> list:
    """按 `_FILTERS` 的六组条件过滤（大小写敏感：OKX 的取值就是 "SPOT"/"buy"/"filled"）。"""
    for param, attr in _FILTERS:
        value = _q(request, param)
        if value:
            orders = [order for order in orders if getattr(order, attr) == value]
    return list(orders)


def _cursor(rows: list, cursor: str, newer: bool) -> list | None:
    """`before` / `after` 游标切片；游标不可用返回 None（调用方回空页）。

    主路径是**命中行本身** —— 引擎翻页时把上一页最后一行的 `ordId` 原样回传
    （`QryOpenEntrusts` / `QryHisEntrusts` 的 `for(;;)` 里 `params["before"] = ex_entno`）。
    命中不了（那一页的委托已被撤/成交）再退化为按数字 ordId 比较。两者都不成立时必须回空页，
    否则引擎的分页循环会永远收到同一页 —— 那是一个不终止的请求风暴。
    """
    if not cursor:
        return rows
    for index, order in enumerate(rows):
        if order.ord_id == cursor:
            return rows[:index] if newer else rows[index + 1:]
    bound = _int_or_none(cursor)
    if bound is None:
        return None
    return [o for o in rows if _seq(o) > bound] if newer else [o for o in rows if _seq(o) < bound]


def _page(orders, request: web.Request) -> list:
    """按 `after` / `before` 游标 + `limit` 切一页；结果按 ordId 降序（新的在前，OKX 惯例）。"""
    rows = sorted(orders, key=_seq, reverse=True)
    for cursor, newer in ((_q(request, "after"), True), (_q(request, "before"), False)):
        if not cursor:
            continue
        rows = _cursor(rows, cursor, newer)
        if rows is None:
            return []
    return rows[:_limit(request)]


def _fail(code: str, msg: str) -> dict:
    """失败应答外壳：引擎只看 `code != "0"` 就回错误，`data` 仍给空数组（不是 null）。"""
    return {"code": str(code), "msg": str(msg), "data": []}


def _json(body: dict) -> web.Response:
    """应答出口（HTTP 200 + JSON）：报文全由 `codec` 构造，本模块不自己拼字符串数值。"""
    return web.json_response(body)


class OkxRest:
    """OKX REST 业务（只读七端点）；一个进程一个实例，由 T10 装配后挂进 aiohttp app。

    装配顺序：先 `OkxAdapter.bind_trading()`，再把返回的 `PrivateChannels` 传进来 —— 委托查询的
    数据源在它手上（订单只有那一份真相，REST 不另存一份）。
    """

    def __init__(self, book: OrderBook, accounts: AccountManager, *, trading: PrivateChannels,
                 instruments=(), account: str = "", now_ms=None) -> None:
        """Args:
            book: 订单簿 —— 盘口的唯一真相。
            accounts: 账户账本 —— 余额的唯一真相。
            trading: 私有频道业务；**只读**它的 `orders()` 快照，REST 不另存一份委托。
            instruments: 合约规格序列（来自配置 `instruments[]`），`/account/instruments` 的内容。
            account: 余额查询落的账户名（配置里的默认 api_key）。
            now_ms: 时钟（毫秒 epoch 取值函数）；缺省系统时间，测试注入固定值。

        Raises:
            ValueError: `trading` 缺失 —— `/trade/*` 三个端点就没有数据源，装配漏了要当场可见。
        """
        if trading is None:
            raise ValueError("trading 不能为空：/trade/* 以私有频道的订单快照为唯一数据源")
        self._book = book
        self._accounts = accounts
        self._trading = trading
        self._instruments = list(instruments)
        self._account = account
        self._now_ms = now_ms or _now_ms

    def routes(self) -> list[RestRoute]:
        """7 个只读 GET 的路由表；T10 逐条 `app.router.add_route(method, path, handler)`。"""
        return [
            ("GET", "/api/v5/account/instruments", self.instruments),
            ("GET", "/api/v5/market/books", self.books),
            ("GET", "/api/v5/market/candles", self.candles),
            ("GET", "/api/v5/account/balance", self.balance),
            ("GET", "/api/v5/trade/order", self.order),
            ("GET", "/api/v5/trade/orders-pending", self.orders_pending),
            ("GET", "/api/v5/trade/orders-history", self.orders_history),
        ]

    async def instruments(self, request: web.Request) -> web.Response:
        """#1 `GET /api/v5/account/instruments?instType=SPOT`：合约规格（路径是引擎自造的）。

        引擎按 SPOT/SWAP/FUTURES/OPTION 各拉一次（2026-10-03 起不再拉 MARGIN：币币杠杆不是独立
        品种，本系统不做杠杆）；mock 出厂清单为 SPOT/SWAP/FUTURES，OPTION 回空 `data`
        （引擎只是逐个遍历，空数组是正常结果）。`instIdCode` 由 `instrument_row` 保证
        是 JSON number —— 引擎 `GetInt64` 读它，类型错在 Debug 构建直接 abort。
        """
        inst_type = _q(request, "instType")
        rows = [codec.instrument_row(spec) for spec in self._instruments
                if not inst_type or spec.inst_type == inst_type]
        return _json(codec.rest_ok(rows))

    async def books(self, request: web.Request) -> web.Response:
        """#2 `GET /api/v5/market/books?instId=`：盘口快照；顶层 `ts` 是**字符串**（引擎 GetString）。

        `instId` 为空回 `data: []`（调用方没指定合约，引擎的循环不会追加也不读 `ts`）；未知 instId
        回一行空档位。`ts` 恒为字符串。单侧最多 `MAX_LEVELS` 档 —— 引擎 `GetDepth` 把 `asks`/`bids`
        逐条写进固定 10 长的数组且没有边界检查，多发的档位是**越界写**，不是"多给点数据"。
        """
        inst_id = _q(request, "instId")
        rows = []
        if inst_id:
            depth = self._book.depth(inst_id, MAX_LEVELS)
            rows = [{"bids": [codec.level(*level) for level in depth["bids"]],
                     "asks": [codec.level(*level) for level in depth["asks"]]}]
        return _json(codec.rest_ok(rows, ts_ms=self._now_ms()))

    async def candles(self, request: web.Request) -> web.Response:
        """#3 `GET /api/v5/market/candles?instId&bar&after&before&limit=100`：K 线。

        M1 没有 K 线数据源 → 恒回空 `data` 数组。引擎 `QryKLine` 的翻页条件是"本页结果数 ≥ limit"，
        空数组立刻终止分页（它只写 `code:"0"` 判失败，不看 K 线内容），故这是合法应答。
        """
        return _json(codec.rest_ok([]))

    async def balance(self, request: web.Request) -> web.Response:
        """#4 `GET /api/v5/account/balance?ccy=`：`data[0].details[]` 的 ccy/availEq/frozenBal/eq。

        mock 没有冻结概念 → `frozenBal` 恒 `"0"`、`availEq` = `eq` = 账本余额（可为负）。`ccy` 为空
        = 全部币种。账户不在账本里回失败码 + **空** `data`：引擎遇 `data` 长度 ≠1 只告警，随后照样
        索引 `data[0]`，所以绝不能在 `code:"0"` 下回空数组（那才是真越界）。
        """
        ccy = _q(request, "ccy")
        try:
            element = self._accounts.snapshot(self._account)
        except KeyError:  # 配置的 api_key 不在账本里：装配问题，留痕并回错误码
            logger.warning("余额查询的账户不在账本里: %r", self._account)
            return _json(_fail(_S_CODE_NO_ACCOUNT, f"account not found: {self._account!r}"))
        details = [{"ccy": row["ccy"], "availEq": row["cashBal"], "frozenBal": "0",
                    "eq": row["cashBal"]}
                   for row in element["balData"] if not ccy or row["ccy"] == ccy]
        return _json(codec.rest_ok([{"uTime": element["uTime"], "details": details}]))

    async def order(self, request: web.Request) -> web.Response:
        """#5 `GET /api/v5/trade/order?instId&clOrdId`：单笔查单（策略撤单后按 `entrust_wait_ms` 轮询）。

        按 `clOrdId` / `ordId`（引擎谁非空发谁）从订单快照里找；查不到回 `51603`。
        """
        rows = _page(_filter(self._trading.orders(), request), request)
        if not rows:
            return _json(_fail(_S_CODE_NO_ORDER, "Order does not exist"))
        return _json(codec.rest_ok([codec.order_row(order) for order in rows]))

    async def orders_pending(self, request: web.Request) -> web.Response:
        """#6 `GET /api/v5/trade/orders-pending?limit=100&before=`：未成交委托（WS 登录/重连后补查）。

        未成交 = `live` + `partially_filled`。immediate 模式下引擎自己的单立即全成，故这里通常是
        管理面注入的挂单或被吃了一半的挂单。
        """
        rows = [o for o in _filter(self._trading.orders(), request) if o.state in _PENDING_STATES]
        return _json(codec.rest_ok([codec.order_row(order) for order in _page(rows, request)]))

    async def orders_history(self, request: web.Request) -> web.Response:
        """#7 `GET /api/v5/trade/orders-history?instType&begin&limit=100&before=`：历史委托。

        已终态 = `filled` + `canceled`。`begin` / `end` 是毫秒 epoch 窗口，按委托的最后更新时间过
        滤 —— 与引擎重连时"拉最近 N 天"的用法一致（`src/websocket/okx_trade.cpp:687`）。引擎对 5 种
        instType 各查一遍，这里全部按同一条路径回答。
        """
        begin, end = _int_or_none(_q(request, "begin")), _int_or_none(_q(request, "end"))
        rows = [o for o in _filter(self._trading.orders(), request) if o.state in _TERMINAL_STATES]
        if begin is not None:
            rows = [order for order in rows if order.u_time >= begin]
        if end is not None:
            rows = [order for order in rows if order.u_time <= end]
        return _json(codec.rest_ok([codec.order_row(order) for order in _page(rows, request)]))
