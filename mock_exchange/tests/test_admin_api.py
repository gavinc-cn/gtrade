"""管理面 `/admin/*` 契约测试（TDD：先红后绿）。

驱动方式同 T5–T8：同步测试函数 + `asyncio.run`，不引 pytest-asyncio；端到端用
`aiohttp.test_utils.TestServer/TestClient` 把 `AdminApi.routes()` 逐条 `app.router.add_route`
挂上（T10 的装配方式），请求走真实 HTTP。

守的契约（spec《模拟交易所》「接口清单 → 管理面」）：`/admin/order` 挂的单立刻出现在
`/admin/state` 的盘口里，且**必须被 T7 的 `track()` 登记**（漏登记 = 引擎撤单收不到
`state:"canceled"` 帧，只留一条 warning）；`/admin/depth` 连续注入是**替换语义**（同 instId
只剩后一批）且未给量时用 `injection.default_sz`；凡改盘口的端点都 `publish("book.<instId>")`；
`/admin/reset` 清订单簿/账户/委托索引/事件环；`/admin/events` 按发生顺序回管理面调用与盘口事件；
参数错 400、委托找不到 404（**绝不 500** —— 这是给人用的接口）。"""

import asyncio
import inspect
import json
from contextlib import asynccontextmanager
from types import SimpleNamespace

from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

from mockex.config import AccountConfig, InstrumentConfig
from mockex.core.account import AccountManager
from mockex.core.book import OrderBook
from mockex.core.events import EventBus
from mockex.core.matching import MatchingEngine
from mockex.exchanges.okx.ws_private import ORDER_CHANNEL, PrivateChannels
from mockex.protocol.admin import AdminApi
from mockex.protocol.ws import KIND_PRIVATE, WsRegistry

ACCOUNT, TS_MS, MM = "mock-okx-key", 1_700_000_000_123, "mock_mm"
BTC, ETH, DEFAULT_SZ = "BTC-USDT", "ETH-USDT", 0.25
SPECS = [
    InstrumentConfig(inst_id=BTC, inst_type="SPOT", inst_id_code=3, base_ccy="BTC",
                     quote_ccy="USDT", settle_ccy="USDT", ct_val="1", ct_mult="1",
                     ct_val_ccy="BTC", list_time="0", tick_sz="0.1", lot_sz="0.00000001",
                     min_sz="0.00001"),
    InstrumentConfig(inst_id=ETH, inst_type="SPOT", inst_id_code=4, base_ccy="ETH", quote_ccy="USDT")]
# 初始余额快照（`/admin/state` 与 `/admin/reset` 都断言它，账户与 T7/T8 的测试同源）
SEED = [{"ccy": "BTC", "cashBal": "100", "uTime": "0"}, {"ccy": "USDT", "cashBal": "1000000", "uTime": "0"}]


class FakeWs:
    """假装一条 WS 连接：`WsRegistry.broadcast` 只用 `send_str` 与 `closed` 两个成员。"""

    def __init__(self):
        self.sent, self.closed = [], False

    async def send_str(self, text):
        self.sent.append(text)


def make_kernel():
    """内核依赖打包（按 T10 的方式装配）：订单簿 + 撮合 + 账户 + 私有频道 + 会话表。"""
    book, bus = OrderBook(), EventBus()
    accounts = AccountManager([AccountConfig(api_key=ACCOUNT, balances={"USDT": 1_000_000.0,
                                                                      "BTC": 100.0})],
                              instruments=SPECS)
    engine = MatchingEngine(book, clock=lambda: TS_MS)
    channels = PrivateChannels(engine, accounts, bus, instruments=SPECS, account=ACCOUNT,
                               now_ms=lambda: TS_MS)
    return SimpleNamespace(book=book, bus=bus, accounts=accounts, engine=engine,
                           channels=channels, registry=WsRegistry())


def make_admin(kernel, **over):
    """按 T10 的方式装配管理面；时钟固定，`/admin/events` 的 ts 可精确断言。"""
    kwargs = dict(trading=kernel.channels, instruments=SPECS, default_sz=DEFAULT_SZ,
                  fill_mode="immediate", now_ms=lambda: TS_MS)
    kwargs.update(over)
    return AdminApi(kernel.book, kernel.accounts, kernel.bus, kernel.registry, **kwargs)


@asynccontextmanager
async def admin_client(api):
    """把路由表按 T10 的方式挂到 aiohttp app 上（临时端口，走真实 HTTP）。"""
    app = web.Application()
    for method, path, handler in api.routes():
        app.router.add_route(method, path, handler)
    async with TestClient(TestServer(app)) as client:
        yield client


async def post_json(client, path, body=None):
    """POST 一个管理面端点 → `(状态码, 应答 JSON)`；200/400/404 都是断言点，不预先断言。"""
    response = await client.post(path) if body is None else await client.post(path, json=body)
    return response.status, await response.json()


async def get_json(client, path):
    """GET 一个管理面端点并解析 JSON（这两个读端点只有 200 与 400 两种结果）。"""
    response = await client.get(path)
    assert response.status == 200, f"{path} 期望 200，实得 {response.status}"
    return await response.json()


def test_routes_shape_and_async_handlers():
    """六个端点、方法与路径逐字对齐 spec；handler 全是协程（aiohttp 的挂载要求）。"""
    routes = make_admin(make_kernel()).routes()
    assert [(method, path) for method, path, _ in routes] == [
        ("POST", "/admin/reset"),
        ("POST", "/admin/order"),
        ("POST", "/admin/cancel"),
        ("POST", "/admin/depth"),
        ("GET", "/admin/state"),
        ("GET", "/admin/events")]
    assert all(inspect.iscoroutinefunction(handler) for _, _, handler in routes)


def test_admin_order_places_level_and_state_reports_it():
    """`/admin/order` 造盘口：立刻进订单簿、被 `track()` 登记、`/admin/state` 五块齐全。"""
    async def scenario():
        kernel = make_kernel()
        published = []
        kernel.bus.subscribe(f"book.{BTC}", lambda payload: published.append(payload))
        kernel.registry.add(object(), KIND_PRIVATE)      # 一条私有连接：状态里的连接计数
        async with admin_client(make_admin(kernel)) as client:
            status, body = await post_json(client, "/admin/order",
                                           {"instId": BTC, "side": "sell", "px": 60001, "sz": 2})
            assert status == 200 and body["ok"] is True
            assert body["clOrdId"].isdigit() and body["ordId"] == body["clOrdId"]   # 引擎按 int64 读
            assert (body["instId"], body["side"], body["px"], body["sz"], body["owner"]) == \
                (BTC, "sell", 60001.0, 2.0, MM)              # owner 缺省即注入方 mock_mm

            state = await get_json(client, "/admin/state")
            assert set(state) == {"book", "orders", "accounts", "ws", "fill_mode"}
            assert state["book"] == {BTC: {"bids": [], "asks": [[60001.0, 2.0]]}}
            assert state["fill_mode"] == "immediate"
            assert state["ws"] == {"public": 0, "private": 1}
            row = state["orders"][0]
            assert (row["clOrdId"], row["instId"], row["state"], row["owner"]) == \
                (body["clOrdId"], BTC, "live", MM)           # 活单 + owner 可分辨自营/注入
            assert all(isinstance(value, str) for key, value in row.items() if key != "owner")
            assert state["accounts"][ACCOUNT]["balData"] == SEED
            assert kernel.channels.orders()[0].cl_ord_id == body["clOrdId"]   # 登记过（撤单靠它）
        assert published == [None]      # 改盘口必须发事件（T6 的桥靠它推 bbo 帧）
    asyncio.run(scenario())


def test_depth_injection_replaces_previous_batch_and_uses_default_size():
    """`/admin/depth` 多档注入：未给量取 default_sz；同 instId 再注入 = 先撤上一批再挂新的。"""
    async def scenario():
        kernel = make_kernel()
        published = []
        kernel.bus.subscribe(f"book.{BTC}", lambda payload: published.append(payload))
        async with admin_client(make_admin(kernel)) as client:
            status, first = await post_json(client, "/admin/depth", {
                "instId": BTC, "bids": [[59999], [59998, 3]], "asks": [[60001, 2]]})
            assert (status, first["ok"], first["replaced"]) == (200, True, 0)
            assert (first["bids"], first["asks"]) == (2, 1)
            state = await get_json(client, "/admin/state")
            assert state["book"][BTC] == {"bids": [[59999.0, DEFAULT_SZ], [59998.0, 3.0]],
                                          "asks": [[60001.0, 2.0]]}
            assert {row["owner"] for row in state["orders"]} == {MM}   # 注入归一化成 mock_mm 挂单

            status, second = await post_json(client, "/admin/depth", {"instId": BTC,
                                                                      "asks": [[60005, 1]]})
            assert (status, second["ok"], second["replaced"]) == (200, True, 3)  # 撤掉上一批 3 张
            after = await get_json(client, "/admin/state")
            assert after["book"] == {BTC: {"bids": [], "asks": [[60005.0, 1.0]]}}
            live = [row for row in after["orders"] if row["state"] == "live"]
            assert [row["px"] for row in live] == ["60005"]      # 旧批已在簿上撤掉（索引留着供历史查）
            # 替换按 instId 限定：注入别的合约不影响这一档
            assert (await post_json(client, "/admin/depth", {"instId": ETH, "bids": [[3000, 1]]}))[0] == 200
            assert (await get_json(client, "/admin/state"))["book"][BTC]["asks"] == [[60005.0, 1.0]]
        assert published == [None, None]     # 两次 BTC 注入各发一次（ETH 那次订的是别的 topic）
    asyncio.run(scenario())


def test_events_ring_keeps_admin_calls_and_book_events_in_order():
    """`/admin/events` 按发生顺序回调用与盘口事件；`limit` 取最新 N 条；坏 limit 回 400。"""
    async def scenario():
        kernel = make_kernel()
        async with admin_client(make_admin(kernel)) as client:
            await post_json(client, "/admin/order",
                            {"instId": BTC, "side": "sell", "px": 60001, "sz": 1})
            await post_json(client, "/admin/depth", {"instId": BTC, "bids": [[60000, 1]]})
            first = await get_json(client, "/admin/events")
            assert [(entry["kind"], entry["ts"]) for entry in first] == \
                [("admin.order", TS_MS), ("book", TS_MS), ("admin.depth", TS_MS), ("book", TS_MS)]
            assert first[0]["detail"] == {"instId": BTC, "side": "sell", "px": 60001, "sz": 1}
            assert first[1]["detail"] == {"instId": BTC}
            # 查询自身也留痕，但**在应答快照之后**入环：查"最近 N 条"不会先看到自己
            assert [entry["kind"] for entry in await get_json(client, "/admin/events?limit=3")] == \
                ["admin.depth", "book", "admin.events"]
            assert await get_json(client, "/admin/events?limit=1") == \
                [{"ts": TS_MS, "kind": "admin.events", "detail": {"limit": 3}}]
            # `1.5`/`abc` 非数字、`0`/`-1` 非正；`"9"*5000` 超长 —— CPython 的 int() 对 >4300 位会抛错
            for bad in ("abc", "0", "-1", "1.5", "9" * 5000):
                response = await client.get(f"/admin/events?limit={bad}")
                assert response.status == 400, bad
                assert "limit" in (await response.json())["error"]
    asyncio.run(scenario())


def test_reset_clears_book_orders_accounts_and_event_ring():
    """`/admin/reset` 回干净环境：盘口/委托/账户/事件环全清，并重发每个合约的盘口事件。"""
    async def scenario():
        kernel = make_kernel()
        published = []
        kernel.bus.subscribe(f"book.{BTC}", lambda payload: published.append(payload))
        kernel.bus.subscribe(f"book.{ETH}", lambda payload: published.append("eth"))
        async with admin_client(make_admin(kernel)) as client:
            await post_json(client, "/admin/order",
                            {"instId": BTC, "side": "sell", "px": 60001, "sz": 1})
            await post_json(client, "/admin/depth", {"instId": ETH, "bids": [[3000, 1]]})
            kernel.channels.handle({"id": "1", "op": "order", "args": [     # 引擎单全成 → 账变了（盘口不动）
                {"instId": BTC, "tdMode": "cash", "clOrdId": "7", "side": "buy", "posSide": "net",
                 "ordType": "limit", "sz": 0.5, "px": 60001}]}, None, kernel.registry)
            assert kernel.accounts.snapshot(ACCOUNT)["balData"][0]["cashBal"] == "100.5"

            status, body = await post_json(client, "/admin/reset")
            assert (status, body["ok"], body["orders"]) == (200, True, 3)   # 两张注入 + 一张引擎单
            assert await get_json(client, "/admin/events") == []            # 事件环也清空
            state = await get_json(client, "/admin/state")
            assert (state["book"], state["orders"]) == ({}, [])
            assert state["accounts"][ACCOUNT]["balData"] == SEED
        assert published.count(None) == 2 and published[-1] == "eth"   # admin 注入 1 次 + reset 重发 1 次（引擎单不动盘口、不发事件）
    asyncio.run(scenario())


def test_cancel_removes_resting_order_and_404s_when_missing():
    """撤管理面挂单：档位消失 + 发盘口事件；找不到 / 已撤回 404（不静默成功、不 500）。"""
    async def scenario():
        kernel = make_kernel()
        published = []
        kernel.bus.subscribe(f"book.{BTC}", lambda payload: published.append(payload))
        async with admin_client(make_admin(kernel)) as client:
            _, placed = await post_json(client, "/admin/order",
                                        {"instId": BTC, "side": "buy", "px": 59999, "sz": 1})
            status, body = await post_json(client, "/admin/cancel", {"clOrdId": placed["clOrdId"]})
            assert (status, body["ok"]) == (200, True)
            assert (body["clOrdId"], body["instId"], body["owner"]) == (placed["clOrdId"], BTC, MM)
            state = await get_json(client, "/admin/state")
            assert kernel.book.best(BTC) == (None, None) and state["orders"][0]["state"] == "canceled"
            assert (await post_json(client, "/admin/cancel", {"clOrdId": placed["clOrdId"]}))[0] == 404
            assert (await post_json(client, "/admin/cancel", {"clOrdId": "4040404"}))[0] == 404
            assert (await post_json(client, "/admin/cancel", {}))[0] == 400
        assert published == [None, None]        # 挂单 + 撤单各发一次
    asyncio.run(scenario())


def test_admin_order_is_tracked_so_cancel_order_pushes_canceled_frame():
    """管理面挂单必须进 T7 的委托索引：撤单 ack 正常却收不到 `canceled` 帧是最难查的失败。"""
    async def scenario():
        kernel = make_kernel()
        ws = FakeWs()
        kernel.registry.add(ws, KIND_PRIVATE)
        kernel.registry.subscribe(ws, ORDER_CHANNEL, None)
        async with admin_client(make_admin(kernel)) as client:
            _, placed = await post_json(client, "/admin/order",
                                        {"instId": BTC, "side": "sell", "px": 60001, "sz": 2})
        reply = kernel.channels.handle({"id": "1", "op": "cancel-order", "args": [
            {"instId": BTC, "clOrdId": placed["clOrdId"]}]}, ws, kernel.registry)
        assert reply["data"][0]["sCode"] == "0"          # 引擎侧撤单 ack 正常
        await asyncio.sleep(0)                           # 让广播的发送任务跑完
        frame = json.loads(ws.sent[0])
        assert (frame["arg"]["channel"], frame["data"][0]["clOrdId"], frame["data"][0]["state"]) == \
            (ORDER_CHANNEL, placed["clOrdId"], "canceled")
        assert all(isinstance(value, str) for value in frame["data"][0].values())
    asyncio.run(scenario())


def test_bad_payloads_are_400_with_reason_and_never_touch_the_book():
    """非法 payload：400 + 点名的说明（「给人用的接口」不许 500、不许静默成功），盘口零改动。"""
    async def scenario():
        kernel = make_kernel()
        async with admin_client(make_admin(kernel)) as client:
            before = kernel.book.snapshot()
            bads = [
                ("/admin/order", None, "JSON"),                                        # 空 body
                ("/admin/order", {"side": "buy", "px": 1, "sz": 1}, "instId"),         # 缺 instId
                ("/admin/order", {"instId": "DOGE-USDT", "side": "buy", "px": 1, "sz": 1},
                 "DOGE-USDT"),                                                         # 未知合约
                ("/admin/order", {"instId": BTC, "side": "long", "px": 1, "sz": 1}, "long"),
                ("/admin/order", {"instId": BTC, "side": "buy", "sz": 1}, "px"),       # 缺 px
                ("/admin/order", {"instId": BTC, "side": "buy", "px": 0, "sz": 1}, "px"),
                ("/admin/order", {"instId": BTC, "side": "buy", "px": 1, "sz": "abc"}, "sz"),
                ("/admin/order", {"instId": BTC, "side": "buy", "px": 1, "sz": 1,
                                  "clOrdId": "MM1"}, "clOrdId"),                       # 引擎按 int64 读
                ("/admin/order", {"instId": BTC, "side": "buy", "px": 1, "sz": 1, "clOrdId": "9" * 25},
                 "clOrdId"),                                                           # int64 溢出
                ("/admin/depth", {"instId": BTC}, "bids/asks"),                        # 一档都没给
                ("/admin/depth", {"instId": BTC, "bids": [[60000, 1, 2]]}, "档位"),
                ("/admin/depth", {"instId": BTC, "bids": "60000"}, "数组"),
                ("/admin/depth", {"instId": BTC, "asks": [[-1, 1]]}, "档位"),
                ("/admin/cancel", {"clOrdId": ""}, "clOrdId")]
            for path, payload, token in bads:
                status, body = await post_json(client, path, payload)
                assert status == 400, (path, payload)
                assert body["ok"] is False and token in body["error"], (path, payload, body)
            for raw in ("[1, 2]", "{oops", ""):    # 非对象 JSON / 坏 JSON / 空 body
                assert (await client.post("/admin/depth", data=raw)).status == 400
            assert kernel.book.snapshot() == before and kernel.channels.orders() == []
            # `curl -d '{...}'` 不带 Content-Type 的形态（T11 就这么发）照样能用；owner/clOrdId 可自定
            raw = await client.post("/admin/order", data=json.dumps(
                {"instId": BTC, "side": "buy", "px": 59999, "sz": 1, "owner": "algo1", "clOrdId": "12345"}))
            assert raw.status == 200
            state = await get_json(client, "/admin/state")
            assert state["book"][BTC]["bids"] == [[59999.0, 1.0]]
            assert [(r["owner"], r["clOrdId"], r["state"]) for r in state["orders"]] == \
                [("algo1", "12345", "live")]
            # 同一个 clOrdId 再挂一次 → 400（否则 track() 会覆盖索引里那份委托对象）
            assert (await post_json(client, "/admin/order", {"instId": BTC, "side": "buy", "px": 1,
                                                             "sz": 1, "clOrdId": "12345"}))[0] == 400
    asyncio.run(scenario())
