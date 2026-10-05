"""OKX 公共频道 + 适配器抽象契约测试（TDD：先红后绿）。

驱动方式同 T5：同步测试函数 + `asyncio.run`，不引 pytest-asyncio；真实 WS 通路由
`aiohttp.test_utils.TestServer/TestClient` 起在临时端口，把 `handle_ws` 与
`base.make_json_handler` 按 T10 的方式挂在 `/ws/v5/public`（引擎实际连的路径）。

守的契约（逐字对照 `doc_ai/spec/mock_exchange/引擎OKX契约.md`）：订阅回执
`{"event":"subscribe","arg":{...}}`；推送帧 `{"arg":{...},"data":[{ts,seqId,asks,bids}]}`，
`ts` **必须是字符串**（数字会命中引擎 `GetString` 断言）；空盘口一条帧都不推（spec：初始
空盘口、无推送），单边盘口照推、缺的一侧为空数组；`EventBus` 的 `book.<instId>` 事件经
`bbo_frame` → `broadcast` 按合约投递；handler 抛异常只记 warning，绝不断连接（T5 复审遗留）。
"""

import asyncio
import functools
import json
import logging
from contextlib import asynccontextmanager

import pytest
from aiohttp import WSMsgType, web
from aiohttp.test_utils import TestClient, TestServer

from mockex.config import InstrumentConfig
from mockex.core.book import OrderBook
from mockex.core.events import EventBus
from mockex.core.models import SIDE_BUY, SIDE_SELL, Order
from mockex.exchanges.base import ExchangeAdapter, make_json_handler
from mockex.exchanges.okx.adapter import OkxAdapter
from mockex.protocol.ws import KIND_PRIVATE, KIND_PUBLIC, WsRegistry, handle_ws

PUBLIC_PATH, PRIVATE_PATH = "/ws/v5/public", "/ws/v5/private"
BBO, BTC, ETH = "bbo-tbt", "BTC-USDT", "ETH-USDT"
TS_MS = 1_700_000_000_123
# 私有 login 帧的凭据原文（I1：整条正文不进日志，测试拿真实形态的值当探针）
CREDS = {"apiKey": "REAL-KEY-abc", "passphrase": "REAL-PASS-xyz", "sign": "REAL-SIGN-123"}
# 装配 `bind_trading` / `bind_rest` 必填的合约清单与账户名（M1 起没有空默认值）
ACCOUNT = "mock-okx-key"
SPECS = [InstrumentConfig(inst_id=BTC, inst_type="SPOT", inst_id_code=3,
                          base_ccy="BTC", quote_ccy="USDT")]


def make_order(cl_ord_id, side, px, sz, inst_id=BTC):
    """一张 mock 自营挂单（注入行情造盘口走的就是这条路）。"""
    return Order(cl_ord_id=cl_ord_id, ord_id=cl_ord_id, inst_id=inst_id, side=side, px=px, sz=sz,
                 owner="mock_mm", c_time=TS_MS, u_time=TS_MS)


def sub_frame(op, *inst_ids, channel=BBO):
    """订阅/退订报文（契约逐字：`{"op":...,"args":[{"channel":...,"instId":...}]}`）。"""
    return json.dumps({"op": op, "args": [{"channel": channel, "instId": i} for i in inst_ids]})


def ack(op, inst_id, channel=BBO):
    """回执帧（契约逐字）。"""
    return {"event": op, "arg": {"channel": channel, "instId": inst_id}}


class FakeWs:
    """只实现订阅表用到的两个成员（`send_str` / `closed`），让 handler 契约能脱离网络单测。"""

    def __init__(self):
        self.sent, self.closed = [], False

    async def send_str(self, text):
        self.sent.append(text)


@asynccontextmanager
async def running_server(adapter, *, private=False):
    """按 T10 的方式挂一条 WS 路由；yield 出 `(client, registry, bus, book)`。"""
    registry, bus, book = WsRegistry(), EventBus(), OrderBook()
    path, kind = (PRIVATE_PATH, KIND_PRIVATE) if private else (PUBLIC_PATH, KIND_PUBLIC)
    app = web.Application()
    app.router.add_get(path, functools.partial(handle_ws, registry=registry, kind=kind,
                                               on_json=make_json_handler(adapter, registry,
                                                                         private=private)))
    async with TestClient(TestServer(app)) as client:
        yield client, registry, bus, book


async def next_text(ws, timeout=2.0):
    """读下一条文本帧；超时即失败（测试挂死比断言失败难查一个数量级）。"""
    msg = await asyncio.wait_for(ws.receive(), timeout)
    assert msg.type is WSMsgType.TEXT, f"期望 TEXT 帧，实得 {msg.type}: {msg.data!r}"
    return msg.data


async def next_json(ws):
    return json.loads(await next_text(ws))


async def assert_no_push(ws):
    """负例断言：发 ping，收到的下一条必须是 pong（顺序断言，不等墙钟）。"""
    await ws.send_str("ping")
    assert await next_text(ws) == "pong"


def mockex_warnings(caplog):
    """只挑本项目的 warning —— aiohttp 自身噪声（未关连接等）不算证据。"""
    return [r for r in caplog.records if r.name.startswith("mockex")]


# ── 契约 1：订阅回执 + 盘口变化推送帧 ────────────────────────────────────────

def test_subscribe_push_unsubscribe_lifecycle_contract():
    """验收 1 + 退订：订阅回执逐字 → 挂单后收到契约推送帧（`ts` 是字符串）→ 退订后不再收帧。"""
    async def scenario():
        adapter = OkxAdapter()
        async with running_server(adapter) as (client, registry, bus, book):
            ws = await client.ws_connect(PUBLIC_PATH)
            await ws.send_str(sub_frame("subscribe", BTC))
            assert await next_json(ws) == ack("subscribe", BTC)

            bridge = adapter.bind_market_data(bus, book, registry, [BTC], now_ms=lambda: TS_MS)
            book.place(make_order("E1", SIDE_BUY, 60000.0, 1.0))
            book.place(make_order("E2", SIDE_SELL, 60001.5, 2.5))
            assert bus.publish(f"book.{BTC}", None) == 1

            frame = json.loads(await next_text(ws))     # 真实 JSON 往返：float/Decimal 漏不进来
            assert frame == {"arg": {"channel": BBO, "instId": BTC},
                             "data": [{"ts": str(TS_MS), "seqId": 1,
                                       "asks": [["60001.5", "2.5"]], "bids": [["60000", "1"]]}]}
            item = frame["data"][0]
            assert isinstance(item["ts"], str) and isinstance(item["seqId"], int)   # GetString/GetInt64
            assert bridge.push(BTC)["data"][0]["seqId"] == 2    # 序号每次推送递增
            assert (await next_json(ws))["data"][0]["seqId"] == 2   # 这一帧也真的发出去了

            await ws.send_str(sub_frame("unsubscribe", BTC))
            assert await next_json(ws) == ack("unsubscribe", BTC)
            assert bus.publish(f"book.{BTC}", None) == 1        # 事件照发（别的订阅者仍可能用）
            await assert_no_push(ws)                            # 但这条连接不再收帧
            await ws.close()

    asyncio.run(scenario())


def test_empty_book_pushes_nothing_and_one_sided_book_pushes():
    """验收 2：空盘口一条帧都不推；只有单侧的盘口照推，缺的一侧编码成空数组。"""
    async def scenario():
        adapter = OkxAdapter()
        async with running_server(adapter) as (client, registry, bus, book):
            ws = await client.ws_connect(PUBLIC_PATH)
            await ws.send_str(sub_frame("subscribe", BTC))
            assert await next_json(ws) == ack("subscribe", BTC)

            bridge = adapter.bind_market_data(bus, book, registry, [BTC], now_ms=lambda: TS_MS)
            assert bus.publish(f"book.{BTC}", None) == 1    # 事件到了，但簿是空的
            assert bridge.push(BTC) is None                 # 直推同样一粒帧都不产生
            await assert_no_push(ws)                        # 连接活着，靠 pong 保鲜

            book.place(make_order("E1", SIDE_BUY, 60000.0, 1.0))    # 只剩买一
            assert bus.publish(f"book.{BTC}", None) == 1
            item = (await next_json(ws))["data"][0]
            assert (item["bids"], item["asks"]) == ([["60000", "1"]], [])
            assert item["seqId"] == 1                       # 空盘口不推帧，也就不占序号
            await ws.close()

    asyncio.run(scenario())


# ── I3：订阅成功补首帧快照（重连/重启时盘口已有挂单的场景）─────────────────────

def test_subscribe_pushes_the_current_book_snapshot():
    """订阅回执之后紧跟一帧**当前**盘口：重连的引擎不必等下一次盘口变化才拿到行情。"""
    async def scenario():
        adapter = OkxAdapter()
        async with running_server(adapter) as (client, registry, bus, book):
            bridge = adapter.bind_market_data(bus, book, registry, [BTC], now_ms=lambda: TS_MS)
            book.place(make_order("E1", SIDE_BUY, 60000.0, 1.0))    # 盘口先立起来（引擎重连的场景）
            ws = await client.ws_connect(PUBLIC_PATH)
            await ws.send_str(sub_frame("subscribe", BTC))
            assert await next_json(ws) == ack("subscribe", BTC)     # 回执在前，顺序对客户端稳定
            assert await next_json(ws) == {                         # 快照紧跟其后，内容取自订单簿
                "arg": {"channel": BBO, "instId": BTC},
                "data": [{"ts": str(TS_MS), "seqId": 1,
                          "asks": [], "bids": [["60000", "1"]]}]}
            assert bridge.push(BTC)["data"][0]["seqId"] == 2        # 快照占一个序号，后续推号接得上
            assert (await next_json(ws))["data"][0]["seqId"] == 2
            # 只认 bbo-tbt：订阅别的频道（同一条连接也在盯 bbo）不该顺手推一帧行情
            await ws.send_str(sub_frame("subscribe", BTC, channel="candles"))
            assert await next_json(ws) == ack("subscribe", BTC, channel="candles")
            await assert_no_push(ws)
            await ws.close()

    asyncio.run(scenario())


def test_subscribe_on_empty_book_pushes_nothing():
    """负例：补的那一帧也走 `push`，空盘口照旧一条都不发（"初始空盘口无推送"不变）。"""
    async def scenario():
        adapter = OkxAdapter()
        async with running_server(adapter) as (client, registry, bus, book):
            adapter.bind_market_data(bus, book, registry, [BTC], now_ms=lambda: TS_MS)
            ws = await client.ws_connect(PUBLIC_PATH)
            await ws.send_str(sub_frame("subscribe", BTC))
            assert await next_json(ws) == ack("subscribe", BTC)
            await assert_no_push(ws)                                # 只有回执，没有帧
            await ws.close()

    asyncio.run(scenario())


# ── 契约 2：回执形状（胶水与 T7 依赖的返回值语义）────────────────────────────

def test_reply_shape_and_glue_sends_every_frame_without_network():
    """单 arg → 单帧 dict；多 arg → 逐 arg 一帧的 list；非法 arg 跳过；退订真的摘订阅。"""
    async def scenario():
        adapter, registry, ws = OkxAdapter(), WsRegistry(), FakeWs()
        registry.add(ws, KIND_PUBLIC)
        on_json = make_json_handler(adapter, registry)

        await on_json(ws, sub_frame("subscribe", BTC))
        await on_json(ws, json.dumps({"op": "subscribe", "args": [{"channel": BBO, "instId": ETH},
                                                                 {"instId": BTC}]}))
        await on_json(ws, sub_frame("unsubscribe", BTC, ETH))
        assert [json.loads(text) for text in ws.sent] == [ack("subscribe", BTC),
                                                          ack("subscribe", ETH),
                                                          ack("unsubscribe", BTC),
                                                          ack("unsubscribe", ETH)]
        assert registry.broadcast(BBO, BTC, "frame") == 0       # 已全部退订
        assert adapter.ws_public_handler({"args": [{"channel": BBO, "instId": BTC}], "op": "subscribe"},
                                         ws, registry) == ack("subscribe", BTC)   # 单 arg → 单帧 dict

    asyncio.run(scenario())


def test_subscription_without_inst_id_is_rejected(caplog):
    """缺 / 空 / 拼错的 `instId` 按坏 arg 处理：**不落频道级订阅**（那会静默收全市场行情），记 warning。"""
    async def scenario():
        adapter, registry, ws = OkxAdapter(), WsRegistry(), FakeWs()
        registry.add(ws, KIND_PUBLIC)
        args = [{"channel": BBO}, {"channel": BBO, "instId": ""}, {"channel": BBO, "InstId": BTC}]
        assert adapter.ws_public_handler({"op": "subscribe", "args": args}, ws, registry) is None
        assert ws.sent == []                                    # 一条回执都没回
        assert registry.broadcast(BBO, None, "frame") == 0       # 最直接的证据：没有频道级订阅
        assert registry.broadcast(BBO, BTC, "frame") == 0        # 也没落成任何合约订阅

    with caplog.at_level(logging.WARNING):
        asyncio.run(scenario())
    assert len(mockex_warnings(caplog)) == 3


# ── 契约 3：解析失败 / handler 异常都不影响连接 ───────────────────────────────

def test_malformed_frames_are_logged_and_never_kill_connection(caplog):
    """解析不了的报文记 warning 后忽略：一帧不回、连接不断（handler 不抛异常）。"""
    async def scenario():
        async with running_server(OkxAdapter()) as (client, *_):
            ws = await client.ws_connect(PUBLIC_PATH)
            for payload in ({"op": "login"},                       # 公共频道不认的 op
                            {"op": "subscribe"},                   # 缺 args
                            {"op": "subscribe", "args": []},        # 空 args
                            {"op": "subscribe", "args": "nope"},    # args 不是数组
                            {"op": "subscribe", "args": [{"channel": BBO, "instId": 7}]}):
                await ws.send_str(json.dumps(payload))
            await assert_no_push(ws)
            await ws.close()

    with caplog.at_level(logging.WARNING):
        asyncio.run(scenario())
    assert len(mockex_warnings(caplog)) == 5      # 每条坏帧都留了痕，不是静默丢弃


class BoomAdapter(ExchangeAdapter):
    """handler 必抛异常的坏适配器：抛出去就是会话层静默断连，胶水必须兜住。"""

    name = "boom"

    def rest_routes(self):
        return []

    def ws_public_handler(self, payload, ws, registry):
        raise RuntimeError("boom 报文处理炸了")

    def ws_private_handler(self, payload, ws, registry):
        raise RuntimeError("boom 报文处理炸了")


def test_handler_exceptions_are_swallowed_on_both_paths(caplog):
    """公共路径的坏适配器 + 私有路径的 T7 占位：异常记 warning、连接都不掉（T5 复审遗留）。

    私有路径那条框里放的是**真实形态的凭据**（I1）：兜底 warning 只留 `op` + 异常摘要，
    `apiKey/passphrase/sign` 的值一个字符都不许出现在日志里。
    """
    async def public_path():
        async with running_server(BoomAdapter()) as (client, *_):
            ws = await client.ws_connect(PUBLIC_PATH)
            await ws.send_str(sub_frame("subscribe", BTC))
            await assert_no_push(ws)                # 异常被兜住：不回帧、不中断服务
            await ws.close()

    async def private_path():
        async with running_server(OkxAdapter(), private=True) as (client, registry, *_):
            ws = await client.ws_connect(PRIVATE_PATH)
            await ws.send_str(json.dumps({"op": "login", "args": [dict(CREDS)]}))
            await assert_no_push(ws)
            assert registry.count(KIND_PRIVATE) == 1
            await ws.close()

    with caplog.at_level(logging.WARNING):
        asyncio.run(public_path())
        asyncio.run(private_path())
    broken = mockex_warnings(caplog)                # 各 1 条：原始异常留在日志里可排障
    assert [type(r.exc_info[1]) for r in broken] == [RuntimeError, NotImplementedError]
    assert "op='login'" in broken[1].getMessage()   # 私有路径留痕：知道是哪条报文炸的
    for secret in CREDS.values():
        assert secret not in caplog.text            # I1 硬约束：凭据原文（含调用栈）不进日志
    assert sub_frame("subscribe", BTC) in caplog.text   # 公共路径照记原文（排障价值保留）


def test_adapter_surface():
    """抽象面：`name`；REST 路由表未绑定时为空（绑定后的七条路由见 tests/test_tls.py 的端到端）。"""
    adapter = OkxAdapter()
    assert isinstance(adapter, ExchangeAdapter) and adapter.name == "okx"
    assert adapter.rest_routes() == []              # 未 bind_rest：装配还没接 REST 面
    with pytest.raises(RuntimeError, match="bind_trading"):
        # instruments/account 是必填（M1 去掉了空默认值）：顺序反了要当场报，而不是装出个查不到委托的面
        adapter.bind_rest(OrderBook(), None, instruments=SPECS, account=ACCOUNT)


def test_binding_requires_instruments_and_account_explicitly():
    """M1：`instruments` / `account` 必须显式传 —— 漏传的失败形态是运行时才炸（`/account/instruments`
    恒回空 `data` → 引擎 `RefreshAllMarketInfo` terminate；私有频道则是每笔下单撞未知合约）。"""
    adapter = OkxAdapter()
    with pytest.raises(TypeError):
        adapter.bind_trading(None, None, None, account=ACCOUNT)         # 缺 instruments
    with pytest.raises(TypeError):
        adapter.bind_trading(None, None, None, instruments=SPECS)       # 缺 account
    with pytest.raises(TypeError):
        adapter.bind_rest(None, None, instruments=SPECS)                # 缺 account（REST 面同理）


def test_bind_market_data_is_single_shot_and_isolates_instruments():
    """桥装配：attach 幂等、按合约隔离、重复 bind 报错（双桥 = 每次变化推两帧）。"""
    async def scenario():
        adapter, registry, bus, book = OkxAdapter(), WsRegistry(), EventBus(), OrderBook()
        btc, eth = FakeWs(), FakeWs()
        for ws, inst_id in ((btc, BTC), (eth, ETH)):
            registry.add(ws, KIND_PUBLIC)
            registry.subscribe(ws, BBO, inst_id)

        bridge = adapter.bind_market_data(bus, book, registry, [BTC, ETH], now_ms=lambda: TS_MS)
        assert bridge.attach(BTC) is False                  # 已监听：幂等，不重复订阅
        with pytest.raises(RuntimeError):
            adapter.bind_market_data(bus, book, registry, [BTC])

        book.place(make_order("E1", SIDE_BUY, 60000.0, 1.0))
        assert bus.publish(f"book.{BTC}", None) == 1        # 一次事件只一个订阅者
        assert bus.publish(f"book.{ETH}", None) == 1        # ETH 空盘口：回调照调，帧不发
        await asyncio.sleep(0)                              # 让 broadcast 排出的发送任务跑完
        assert len(btc.sent) == 1 and eth.sent == []
        assert json.loads(btc.sent[0])["arg"]["instId"] == BTC   # 只投给盯着该合约的连接

        bridge.stop()
        assert bus.publish(f"book.{BTC}", None) == 0        # 退订后内核事件不再进桥
    asyncio.run(scenario())


def test_bind_failure_leaves_adapter_usable_without_residue():
    """中途 attach 失败（空 instId）：不留残留订阅、适配器仍可再装配（T10 起不来也不脏进程）。"""
    async def scenario():
        adapter, registry, bus, book = OkxAdapter(), WsRegistry(), EventBus(), OrderBook()
        with pytest.raises(ValueError):
            adapter.bind_market_data(bus, book, registry, [ETH, ""])
        assert bus.publish(f"book.{ETH}", None) == 0              # 半装的那条订阅也退掉了
        bridge = adapter.bind_market_data(bus, book, registry, [ETH])   # 失败后仍可正常装配
        assert bridge.attach(ETH) is False and bus.publish(f"book.{ETH}", None) == 1
    asyncio.run(scenario())
