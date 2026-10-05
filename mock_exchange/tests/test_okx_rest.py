"""OKX REST 七端点契约测试（TDD：先红后绿）。

驱动方式同 T5–T7：同步测试函数 + `asyncio.run`，不引 pytest-asyncio；端到端用
`aiohttp.test_utils.TestServer/TestClient` 把 `OkxRest.routes()` 逐条 `app.router.add_route`
挂上（T10 的装配方式），请求走真实 HTTP。

守的契约（逐字对照 `doc_ai/spec/mock_exchange/引擎OKX契约.md` 的「REST」小节与「字段类型
硬约束」）：7 个 GET 全部 `code:"0"`；`/account/instruments` 的 16 个键里 `instIdCode` 必须是
JSON **number**、其余 15 键必须是字符串（引擎 `GetInt64` / `GetString`，Debug 构建直接 abort）；
`/market/books` 的顶层 `ts` 必须是字符串；`/account/balance` 的 `data[0].details[]` 四字段齐全；
单笔/未成交/历史委托的 11 个引擎读取键齐全且全字符串；引擎实际发出的**空值 query**
（`instId=&ordType=&state=&end=`）一律 200，不得回 400/422。
"""

import asyncio
import inspect
from contextlib import asynccontextmanager
from types import SimpleNamespace

from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

from mockex.config import AccountConfig, InstrumentConfig
from mockex.core.account import AccountManager
from mockex.core.book import OrderBook
from mockex.core.events import EventBus
from mockex.core.matching import MatchingEngine
from mockex.core.models import SIDE_BUY, SIDE_SELL, Order
from mockex.exchanges.okx.rest import OkxRest
from mockex.exchanges.okx.ws_private import PrivateChannels
from mockex.protocol.ws import WsRegistry

ACCOUNT, TS_MS, BTC, MM = "mock-okx-key", 1_700_000_000_123, "BTC-USDT", "mock_mm"
SPECS = [InstrumentConfig(inst_id=BTC, inst_type="SPOT", inst_id_code=3, base_ccy="BTC",
                          quote_ccy="USDT", settle_ccy="USDT", ct_val="1", ct_mult="1",
                          ct_val_ccy="BTC", list_time="0", tick_sz="0.1",
                          lot_sz="0.00000001", min_sz="0.00001")]
# 契约逐字：16 个键中 instIdCode 是 number，其余 15 个是字符串
INSTRUMENT_ROW = {"instType": "SPOT", "instId": BTC, "baseCcy": "BTC", "quoteCcy": "USDT",
                  "settleCcy": "USDT", "ctVal": "1", "ctMult": "1", "ctValCcy": "BTC",
                  "listTime": "0", "expTime": "", "lever": "", "tickSz": "0.1",
                  "lotSz": "0.00000001", "minSz": "0.00001", "state": "live", "instIdCode": 3}
# 委托行里引擎实际逐字段 GetString() 读的 11 个键（契约 #5–#7）
ORDER_KEYS = ("state", "uTime", "cTime", "fillTime", "instType", "instId", "ordId", "clOrdId",
              "px", "sz", "accFillSz")
PENDING, HISTORY = "/api/v5/trade/orders-pending", "/api/v5/trade/orders-history"


def make_kernel():
    """内核依赖打包：订单簿 + 撮合 + 账户 + 私有频道（委托快照的唯一真相）。"""
    book, bus = OrderBook(), EventBus()
    accounts = AccountManager(
        [AccountConfig(api_key=ACCOUNT, balances={"USDT": 1_000_000.0, "BTC": 100.0})],
        instruments=SPECS)
    engine = MatchingEngine(book, clock=lambda: TS_MS)
    channels = PrivateChannels(engine, accounts, bus, instruments=SPECS, account=ACCOUNT,
                               now_ms=lambda: TS_MS)
    return SimpleNamespace(book=book, bus=bus, accounts=accounts, engine=engine, channels=channels)


def make_rest(kernel, **over):
    """按 T10 的方式装配 REST 业务；时钟固定，`ts` 与时间字段可精确断言。"""
    kwargs = dict(instruments=SPECS, account=ACCOUNT, trading=kernel.channels, now_ms=lambda: TS_MS)
    kwargs.update(over)
    return OkxRest(kernel.book, kernel.accounts, **kwargs)


@asynccontextmanager
async def okx_client(rest):
    """把路由表按 T10 的方式挂到 aiohttp app 上（临时端口，走真实 HTTP）。"""
    app = web.Application()
    for method, path, handler in rest.routes():
        app.router.add_route(method, path, handler)
    async with TestClient(TestServer(app)) as client:
        yield client


async def get_json(client, path):
    """GET 一个路径并解析 JSON；**先断言 HTTP 200** —— 空值 query 回 400/422 正是验收点。"""
    response = await client.get(path)
    assert response.status == 200, f"{path} 期望 200，实得 {response.status}"
    return await response.json()


def engine_order(kernel, cl_ord_id, px=60001.0, sz=0.1):
    """走引擎的真实路径下单（T7 的私有频道），委托落进 `_orders` 快照（REST 的唯一数据源）。"""
    reply = kernel.channels.handle({"id": "1", "op": "order", "args": [
        {"instId": BTC, "tdMode": "cash", "clOrdId": cl_ord_id, "side": SIDE_BUY, "posSide": "net",
         "ordType": "limit", "sz": sz, "px": px}]}, None, WsRegistry())
    assert reply["data"][0]["sCode"] == "0"
    return [order for order in kernel.channels.orders() if order.cl_ord_id == cl_ord_id][0]


def injected_order(kernel, cl_ord_id, ord_id, side, px, sz):
    """管理面注入的挂单（T9 的路径）：进订单簿 + `track()` 登记，REST 才查得到。"""
    order = Order(cl_ord_id=cl_ord_id, ord_id=ord_id, inst_id=BTC, side=side, px=px, sz=sz,
                  owner=MM, c_time=TS_MS, u_time=TS_MS)
    kernel.book.place(order)
    kernel.channels.track(order)
    return order


def pick(row, keys=ORDER_KEYS):
    """只挑引擎实际读的键（多余字段不影响引擎，缺键或类型错会 abort）。"""
    return {key: row[key] for key in keys}


def test_routes_cover_the_seven_engine_endpoints():
    """路由表：7 个 GET、路径逐字对齐引擎（`/account/instruments` 是引擎自造路径，非官方）。"""
    routes = make_rest(make_kernel()).routes()
    assert [(method, path) for method, path, _ in routes] == [
        ("GET", "/api/v5/account/instruments"),
        ("GET", "/api/v5/market/books"),
        ("GET", "/api/v5/market/candles"),
        ("GET", "/api/v5/account/balance"),
        ("GET", "/api/v5/trade/order"),
        ("GET", "/api/v5/trade/orders-pending"),
        ("GET", "/api/v5/trade/orders-history"),
    ]
    assert all(inspect.iscoroutinefunction(handler) for _, _, handler in routes)


def test_empty_query_sweep_is_never_400():
    """七个端点各带一套空值 query（引擎 JoinUrl 的实际形态）—— 一律 200 且回 OKX 外壳。"""
    async def scenario():
        kernel = make_kernel()
        injected_order(kernel, "MM1", "91", SIDE_BUY, 60000.0, 1.0)
        rest = make_rest(kernel)
        empty = "?instType=&instId=&ordType=&state=&end=&begin=&before=&after=&limit=&ccy=&bar=" \
                "&clOrdId=&ordId="
        async with okx_client(rest) as client:
            for _, path, _ in rest.routes():
                body = await get_json(client, path + empty)
                assert set(body) >= {"code", "msg", "data"}, f"{path} 应答缺外壳字段"
                assert isinstance(body["data"], list)
    asyncio.run(scenario())


def test_instruments_row_types_and_inst_type_filter():
    """逐键对照契约：`instIdCode` 是 number，其余 15 键是字符串；instType 过滤 + 空值容忍。"""
    async def scenario():
        async with okx_client(make_rest(make_kernel())) as client:
            body = await get_json(client, "/api/v5/account/instruments?instType=SPOT")
            assert (body["code"], body["msg"]) == ("0", "") and len(body["data"]) == 1
            row = body["data"][0]
            assert row == INSTRUMENT_ROW
            assert isinstance(row["instIdCode"], int) and not isinstance(row["instIdCode"], bool)
            assert all(isinstance(value, str) for key, value in row.items() if key != "instIdCode")
            # 引擎按 SPOT/MARGIN/SWAP/FUTURES/OPTION 各拉一次；mock 只有 SPOT，其余回空数组
            for inst_type in ("SWAP", "FUTURES", "OPTION", "NOPE"):
                other = await get_json(client, f"/api/v5/account/instruments?instType={inst_type}")
                assert (other["code"], other["data"]) == ("0", [])
            # `instType=`（空串）与整个参数缺失都按"没给"处理 —— 引擎会拼出空串
            assert len((await get_json(client, "/api/v5/account/instruments?instType="))["data"]) == 1
            assert len((await get_json(client, "/api/v5/account/instruments"))["data"]) == 1
    asyncio.run(scenario())


def test_books_from_orderbook_string_ts_and_level_cap():
    """盘口从订单簿 `depth` 构造；顶层 `ts` 是**字符串**；单侧最多 10 档（引擎数组固定 10 长）。"""
    async def scenario():
        kernel = make_kernel()
        for index in range(12):        # 12 档 → 只准发 10 档：引擎 GetDepth 写定长数组且无边界检查
            injected_order(kernel, f"B{index}", str(100 + index), SIDE_BUY, 60000.0 - index, 1.0)
            injected_order(kernel, f"S{index}", str(200 + index), SIDE_SELL, 60001.0 + index, 1.0)
        async with okx_client(make_rest(kernel)) as client:
            body = await get_json(client, "/api/v5/market/books?instId=BTC-USDT")
            assert (body["code"], body["msg"], body["ts"]) == ("0", "", str(TS_MS))
            row = body["data"][0]
            assert (len(row["bids"]), len(row["asks"])) == (10, 10)
            assert row["bids"][0] == ["60000", "1"] and row["bids"][-1] == ["59991", "1"]  # 买降序
            assert row["asks"][0] == ["60001", "1"] and row["asks"][-1] == ["60010", "1"]  # 卖升序
            # 空 instId 是引擎的常态（DepthQryReq 的 instrument 为空时）——回空数组，不是 400
            empty = await get_json(client, "/api/v5/market/books?instId=")
            assert (empty["code"], empty["data"]) == ("0", [])
            assert isinstance(empty["ts"], str)      # 空盘口也必须带字符串 ts

    asyncio.run(scenario())


def test_candles_empty_data_and_empty_query():
    """K 线无数据源 → 空 `data`（引擎"不足 100 条即结束分页"），`after`/`before` 空值照收。"""
    async def scenario():
        async with okx_client(make_rest(make_kernel())) as client:
            path = "/api/v5/market/candles?instId=BTC-USDT&bar=1m&after=&before=&limit=100"
            body = await get_json(client, path)
            assert (body["code"], body["msg"], body["data"]) == ("0", "", [])
    asyncio.run(scenario())


def test_balance_details_from_ledger_and_ccy_filter():
    """`data[0].details[]` 的 ccy/availEq/frozenBal/eq 全字符串；空 ccy 回全部币种。"""
    async def scenario():
        async with okx_client(make_rest(make_kernel())) as client:
            body = await get_json(client, "/api/v5/account/balance?ccy=")
            assert body["code"] == "0" and len(body["data"]) == 1   # 长度≠1 引擎会告警但仍索引 [0]
            details = body["data"][0]["details"]
            assert details == [{"ccy": "BTC", "availEq": "100", "frozenBal": "0", "eq": "100"},
                               {"ccy": "USDT", "availEq": "1000000", "frozenBal": "0",
                                "eq": "1000000"}]
            assert all(isinstance(value, str) for row in details for value in row.values())
            only_btc = await get_json(client, "/api/v5/account/balance?ccy=BTC")
            assert [row["ccy"] for row in only_btc["data"][0]["details"]] == ["BTC"]
        # 配置的账户不在账本里：回失败码 + 空数组（**绝不** code "0" + 空数组 —— 引擎会索引 [0]）
        async with okx_client(make_rest(make_kernel(), account="nope")) as client:
            body = await get_json(client, "/api/v5/account/balance?ccy=")
            assert body["code"] != "0" and body["data"] == []
    asyncio.run(scenario())


def test_order_endpoint_serves_engine_fields_and_unknown_code():
    """单笔查单：11 个键齐全且全字符串（撤单后按 clOrdId 轮询走的就是这里）；查不到 51603。"""
    async def scenario():
        kernel = make_kernel()
        injected_order(kernel, "MM1", "91", SIDE_SELL, 60001.0, 0.5)     # 无关挂单（引擎成交不吃它）
        engine_order(kernel, "T1")                                       # 立即全成 → filled
        async with okx_client(make_rest(kernel)) as client:
            body = await get_json(client, "/api/v5/trade/order?instId=BTC-USDT&clOrdId=T1")
            assert body["code"] == "0" and len(body["data"]) == 1
            row = body["data"][0]
            assert pick(row) == {"state": "filled", "uTime": str(TS_MS), "cTime": str(TS_MS),
                                 "fillTime": "", "instType": "SPOT", "instId": BTC, "ordId": "1",
                                 "clOrdId": "T1", "px": "60001", "sz": "0.1", "accFillSz": "0.1"}
            assert all(isinstance(value, str) for value in row.values())
            # 引擎发 `ordId=` / `clOrdId=` 两种形态（谁非空发谁），mock 两种都要认
            by_ord_id = await get_json(client, "/api/v5/trade/order?instId=BTC-USDT&ordId=1")
            assert by_ord_id["data"] == body["data"]
            missing = await get_json(client, "/api/v5/trade/order?instId=BTC-USDT&clOrdId=NOPE")
            assert (missing["code"], missing["data"]) == ("51603", [])
    asyncio.run(scenario())


def test_pending_live_states_filters_and_ordering():
    """未成交 = live + partially_filled（filled/canceled 不算）；引擎的空值 query 形态照收。"""
    async def scenario():
        kernel = make_kernel()
        injected_order(kernel, "MM1", "91", SIDE_BUY, 60000.0, 1.0)         # live
        injected_order(kernel, "MM2", "92", SIDE_SELL, 60001.0, 1.0)        # live（引擎单不吃簿，2026-09-25 语义）
        engine_order(kernel, "T1", sz=0.5)                                  # filled → 不算未成交
        injected_order(kernel, "MM3", "93", SIDE_BUY, 59999.0, 1.0)
        assert kernel.book.cancel("MM3")                                    # canceled → 不算
        async with okx_client(make_rest(kernel)) as client:
            body = await get_json(client, f"{PENDING}?limit=100&instId=&ordType=&state=&before=")
            assert body["code"] == "0"
            assert [(row["clOrdId"], row["state"]) for row in body["data"]] == \
                [("MM2", "live"), ("MM1", "live")]              # ordId 降序：新在前；引擎单不吃簿，无 partial 来源
            assert all(isinstance(value, str) for row in body["data"] for value in row.values())
            assert (await get_json(client, f"{PENDING}?instType=SWAP"))["data"] == []
            assert len((await get_json(client, f"{PENDING}?limit=1"))["data"]) == 1
            assert (await get_json(client, f"{PENDING}?state=canceled"))["data"] == []
    asyncio.run(scenario())


def test_history_terminal_only_pagination_and_window():
    """历史委托 = 已终态；`limit`/`before` 取更老的一页；`begin`/`end` 是毫秒 epoch 窗口。"""
    async def scenario():
        kernel = make_kernel()
        injected_order(kernel, "MM1", "91", SIDE_BUY, 60000.0, 1.0)         # live → 不进历史
        engine_order(kernel, "T1")                                          # filled（隐含流动性，不吃 MM1）
        engine_order(kernel, "T2")                                          # filled
        injected_order(kernel, "MM2", "92", SIDE_BUY, 59999.0, 1.0)
        assert kernel.book.cancel("MM2")                                    # canceled → 进历史
        async with okx_client(make_rest(kernel)) as client:
            base = f"{HISTORY}?instType=SPOT&begin=&limit=100&before="      # 引擎的实际 query 形态
            body = await get_json(client, base)
            assert body["code"] == "0"
            assert [row["clOrdId"] for row in body["data"]] == ["MM2", "T2", "T1"]
            assert all(isinstance(value, str) for row in body["data"] for value in row.values())
            page1 = await get_json(client, f"{HISTORY}?limit=2")
            assert [row["clOrdId"] for row in page1["data"]] == ["MM2", "T2"]
            # 引擎拿上一页最后一行的 ordId 当 before 翻页（QryHisEntrusts 的 for(;;)）
            page2 = await get_json(client, f"{HISTORY}?limit=2&before=2")
            assert [row["clOrdId"] for row in page2["data"]] == ["T1"]
            # begin/end：引擎重连补查传 begin=now-N天（毫秒），窗口外的委托不返回
            assert len((await get_json(client, f"{HISTORY}?begin={TS_MS}"))["data"]) == 3
            assert (await get_json(client, f"{HISTORY}?begin={TS_MS + 1}"))["data"] == []
            assert (await get_json(client, f"{HISTORY}?end={TS_MS - 1}"))["data"] == []
            assert len((await get_json(client, f"{HISTORY}?end="))["data"]) == 3  # 空 end = 不设上界
    asyncio.run(scenario())
