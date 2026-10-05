"""OKX 私有频道契约测试（TDD：先红后绿）。

驱动方式同 T5/T6：同步测试函数 + `asyncio.run`，不引 pytest-asyncio；端到端场景用
`aiohttp.test_utils.TestServer/TestClient` 把 `handle_ws` 与 `base.make_json_handler` 按 T10
的方式挂在 `/ws/v5/private` 与 `/ws/v5/public`（引擎实际连的两条路径）。

守的契约（逐字对照 `doc_ai/spec/mock_exchange/引擎OKX契约.md` 的「WS 私有频道」小节）：
`login` 宽容放行回 `{"event":"login","code":"0"}`；`order` ack 的 `data[0].sCode` 是 `"0"`
字符串；`orders` 推送行字段齐全且**全是字符串**（引擎逐字段 `GetString()`，类型错在 Debug
构建直接 abort）；`positions` 帧必须补 `upl/uplRatio/notionalUsd`（T4 的 `snapshot()` 元素里
没有，而引擎无条件读前两个）；`cancel-order` 对订单簿里的挂单生效并推 `state:"canceled"`；
只有撤单命中簿上挂单才改变盘口，此时必须 `publish("book.<instId>")` —— T6 的 bbo 桥只订阅
不发布；immediate 下单是隐含流动性，不动盘口、不发事件（2026-09-25 语义修订）。
"""

import asyncio
import functools
import json
import logging
from contextlib import asynccontextmanager
from types import SimpleNamespace

import pytest
from aiohttp import WSMsgType, web
from aiohttp.test_utils import TestClient, TestServer

from mockex.config import AccountConfig, InstrumentConfig
from mockex.core.account import AccountManager
from mockex.core.book import OrderBook
from mockex.core.events import EventBus
from mockex.core.matching import FILL_MODE_ORDERBOOK, MatchingEngine
from mockex.core.models import SIDE_BUY, SIDE_SELL, Order
from mockex.exchanges.base import make_json_handler
from mockex.exchanges.okx.adapter import OkxAdapter
from mockex.exchanges.okx.ws_private import (BALANCE_CHANNEL, ORDER_CHANNEL, POSITIONS_CHANNEL,
                                             PrivateChannels, positions_frame)
from mockex.protocol.ws import KIND_PRIVATE, KIND_PUBLIC, WsRegistry, handle_ws

PRIVATE_PATH, PUBLIC_PATH = "/ws/v5/private", "/ws/v5/public"
BBO, BTC = "bbo-tbt", "BTC-USDT"
ACCOUNT, TS_MS = "mock-okx-key", 1_700_000_000_123
# 私有 login 帧的凭据原文（I1：整条正文不进日志，测试拿真实形态的值当探针）
CREDS = {"apiKey": "REAL-KEY-abc", "passphrase": "REAL-PASS-xyz", "sign": "REAL-SIGN-123"}
SPECS = [InstrumentConfig(inst_id=BTC, inst_type="SPOT", inst_id_code=3, base_ccy="BTC", quote_ccy="USDT")]


def make_kernel():
    """内核依赖打包：订单簿 + 撮合 + 账户 + 私有频道；时钟固定，时间字段可精确断言。"""
    book, bus = OrderBook(), EventBus()
    accounts = AccountManager(
        [AccountConfig(api_key=ACCOUNT, balances={"USDT": 1_000_000.0, "BTC": 100.0})], instruments=SPECS)
    engine = MatchingEngine(book, clock=lambda: TS_MS)
    channels = PrivateChannels(engine, accounts, bus, instruments=SPECS, account=ACCOUNT,
                               now_ms=lambda: TS_MS)
    return SimpleNamespace(book=book, bus=bus, accounts=accounts, engine=engine, channels=channels)


def bind_trading(adapter, kernel):
    """按 T10 的方式装配，并把**同一个**私有频道实例挂回 kernel（两套实例各有各的委托索引）。"""
    kernel.channels = adapter.bind_trading(kernel.engine, kernel.accounts, kernel.bus,
                                           instruments=SPECS, account=ACCOUNT, now_ms=lambda: TS_MS)
    return adapter


def make_resting(cl_ord_id, side, px, sz, owner="mock_mm"):
    """一张真实挂进订单簿的委托（管理面注入造盘口走的就是这条路）。"""
    return Order(cl_ord_id=cl_ord_id, ord_id=cl_ord_id, inst_id=BTC, side=side, px=px, sz=sz,
                 owner=owner, c_time=TS_MS, u_time=TS_MS)


def order_row(**over):
    """`orders` 推送行的完整契约字段（引擎逐字段 `GetString`，缺一个键 Debug 构建就 abort）。"""
    row = {"instType": "SPOT", "instId": BTC, "clOrdId": "T1", "ordId": "1", "px": "60001",
           "sz": "0.1", "side": "buy", "tdMode": "cash", "ordType": "limit", "posSide": "net",
           "state": "filled", "accFillSz": "0.1", "avgPx": "60001", "fillPx": "60001",
           "fillSz": "0.1", "fillTime": str(TS_MS), "cTime": str(TS_MS), "uTime": str(TS_MS),
           "code": "0", "msg": ""}
    row.update(over)
    return row


def sub_op(op, *args):
    """订阅 / 退订 / 登录报文（契约逐字：引擎登录后固定订三条私有频道）。"""
    return json.dumps({"op": op, "args": list(args)})


class FakeWs:
    """只实现订阅表用到的两个成员（`send_str` / `closed`），让 handler 契约能脱离网络单测。"""

    def __init__(self):
        self.sent, self.closed = [], False

    async def send_str(self, text):
        self.sent.append(text)


@asynccontextmanager
async def okx_server(adapter, kernel):
    """按 T10 的方式挂 public / private 两条 WS 路由并装好 bbo 桥；yield `(client, registry)`。"""
    registry = WsRegistry()
    adapter.bind_market_data(kernel.bus, kernel.book, registry, [BTC], now_ms=lambda: TS_MS)
    app = web.Application()
    app.router.add_get(PUBLIC_PATH, functools.partial(
        handle_ws, registry=registry, kind=KIND_PUBLIC,
        on_json=make_json_handler(adapter, registry)))
    app.router.add_get(PRIVATE_PATH, functools.partial(
        handle_ws, registry=registry, kind=KIND_PRIVATE,
        on_json=make_json_handler(adapter, registry, private=True)))
    async with TestClient(TestServer(app)) as client:
        yield client, registry


async def next_json(ws, timeout=2.0):
    """读下一条文本帧并解析成 JSON；超时即失败（测试挂死比断言失败难查一个数量级）。"""
    msg = await asyncio.wait_for(ws.receive(), timeout)
    assert msg.type is WSMsgType.TEXT, f"期望 TEXT 帧，实得 {msg.type}: {msg.data!r}"
    return json.loads(msg.data)


def mockex_warnings(caplog):
    """只挑本项目的 warning —— aiohttp 自身噪声（未关连接等）不算证据。"""
    return [r for r in caplog.records if r.name.startswith("mockex")]


def test_login_subscriptions_multi_arg_and_malformed_frames(caplog):
    """login 宽容放行；三条私有频道落成频道级订阅（退订即停）；多 arg 逐帧回执；坏帧留痕后忽略。"""
    async def scenario():
        kernel, registry, ws = make_kernel(), WsRegistry(), FakeWs()
        registry.add(ws, KIND_PRIVATE)
        assert kernel.channels.handle(json.loads(sub_op("login", {"apiKey": "k"})), ws, registry) == \
            {"event": "login", "code": "0"}
        orders, balance = {"channel": ORDER_CHANNEL, "instType": "ANY"}, {"channel": BALANCE_CHANNEL}
        positions = {"channel": POSITIONS_CHANNEL, "instType": "ANY"}
        assert kernel.channels.handle(json.loads(sub_op("subscribe", orders, balance)), ws, registry) == \
            [{"event": "subscribe", "arg": orders}, {"event": "subscribe", "arg": balance}]
        assert kernel.channels.handle(json.loads(sub_op("subscribe", positions)), ws, registry) == \
            {"event": "subscribe", "arg": positions}
        assert kernel.channels.handle(json.loads(sub_op("unsubscribe", orders)), ws, registry) == \
            {"event": "unsubscribe", "arg": orders}
        assert kernel.channels.handle(json.loads(sub_op("subscribe", {"channel": "fills"})), ws, registry) is None

        assert registry.broadcast(ORDER_CHANNEL, None, {"ch": ORDER_CHANNEL}) == 0      # 已退订
        assert registry.broadcast(BALANCE_CHANNEL, None, {"ch": BALANCE_CHANNEL}) == 1
        assert registry.broadcast(POSITIONS_CHANNEL, None, {"ch": POSITIONS_CHANNEL}) == 1
        assert registry.broadcast(BBO, BTC, {"ch": BBO}) == 0                           # 没误订公共频道
        await asyncio.sleep(0)                                                          # 让发送任务跑完
        assert [json.loads(text)["ch"] for text in ws.sent] == [BALANCE_CHANNEL, POSITIONS_CHANNEL]

        args = [{"instId": BTC, "clOrdId": f"T{i}", "side": "buy", "ordType": "limit", "sz": 0.1,
                 "px": 60000} for i in (1, 2)]
        reply = kernel.channels.handle({"id": "9", "op": "order", "args": args}, ws, registry)
        assert [item["data"][0]["clOrdId"] for item in reply] == ["T1", "T2"]    # 多 arg → 逐 arg 一帧
        await asyncio.sleep(0)                                                   # 让推送任务先跑完
        sent = len(ws.sent)
        for payload in ({"op": "no-such-op"}, {"op": "order"}, {"op": "order", "args": []},
                        {"op": "order", "args": "nope"}, {"op": "cancel-order"},
                        {"op": "subscribe", "args": [{"channel": "fills"}]}):
            assert kernel.channels.handle(payload, ws, registry) is None        # 坏帧绝不抛异常
        await asyncio.sleep(0)
        assert len(ws.sent) == sent                                             # 坏帧一帧都不回
    with caplog.at_level(logging.WARNING):
        asyncio.run(scenario())
    assert len(mockex_warnings(caplog)) == 7       # 1 条未支持频道 + 6 条坏帧（都留痕，不静默）


class BrokenSendWs(FakeWs):
    """发送必失败的连接：兜底分支正是为"连接刚断 / 发不出去"准备的（I1 的触发路径）。"""

    async def send_str(self, text):
        raise RuntimeError("连接已断")


def test_private_glue_logs_op_only_and_never_the_credentials(caplog):
    """I1：私有报文的兜底 warning 只留 `op` + 异常摘要，凭据原文一个字都不落日志。

    `login` 帧的正文就是 `apiKey/passphrase/sign`，而"发送失败"恰恰是登录帧最可能踩到的兜底
    路径 —— 旧实现把整条原文打进 warning，等于把凭据写进日志（spec 硬约束「凭据不落盘」）。
    """
    async def scenario():
        adapter, registry, ws = OkxAdapter(), WsRegistry(), BrokenSendWs()
        bind_trading(adapter, make_kernel())        # 装配好才造得出 login 回执，才走得到发送
        registry.add(ws, KIND_PRIVATE)
        on_json = make_json_handler(adapter, registry, private=True)
        await on_json(ws, json.dumps({"op": "login", "args": [dict(CREDS)]}))

    with caplog.at_level(logging.WARNING):
        asyncio.run(scenario())
    logged = mockex_warnings(caplog)
    assert len(logged) == 1 and logged[0].name == "mockex.exchanges.base"
    assert type(logged[0].exc_info[1]) is RuntimeError      # 异常摘要留下（排障要的是原因）
    message = logged[0].getMessage()
    assert "op='login'" in message                          # 留痕：知道炸的是哪条报文
    for secret in CREDS.values():
        assert secret not in message and secret not in caplog.text   # 正文（含调用栈）都不许有


def test_order_ack_and_all_private_pushes_end_to_end():
    """下单 → ack（sCode:"0"）→ 私有帧，盘口原样不动、无 bbo 帧（M1 验收核心）。"""
    async def scenario():
        kernel, adapter = make_kernel(), OkxAdapter()
        bind_trading(adapter, kernel)
        published = []
        kernel.bus.subscribe(f"book.{BTC}", lambda payload: published.append(payload))
        async with okx_server(adapter, kernel) as (client, registry):
            ws = await client.ws_connect(PRIVATE_PATH)
            await ws.send_str(sub_op("login", {"apiKey": "k"}))
            assert await next_json(ws) == {"event": "login", "code": "0"}
            await ws.send_str(sub_op("subscribe", {"channel": ORDER_CHANNEL, "instType": "ANY"},
                                     {"channel": BALANCE_CHANNEL},
                                     {"channel": POSITIONS_CHANNEL, "instType": "ANY"}))
            assert len([await next_json(ws) for _ in range(3)]) == 3      # 逐 arg 一帧回执
            bbo_ws = await client.ws_connect(PUBLIC_PATH)                 # 引擎的行情连接是另一条
            await bbo_ws.send_str(sub_op("subscribe", {"channel": BBO, "instId": BTC}))
            assert await next_json(bbo_ws) == {"event": "subscribe", "arg": {"channel": BBO, "instId": BTC}}

            kernel.book.place(make_resting("MM1", SIDE_BUY, 60000.0, 1.0))
            kernel.book.place(make_resting("MM2", SIDE_SELL, 60001.0, 0.5))
            # 引擎实际发的报文：px/sz 是 JSON number，instIdCode 缺失也放行（宽容处理）
            await ws.send_str(json.dumps({"id": "1001", "op": "order", "args": [
                {"instId": BTC, "tdMode": "cash", "clOrdId": "T1", "side": "buy", "posSide": "net",
                 "ordType": "limit", "sz": 0.1, "px": 60001}]}))
            assert await next_json(ws) == {"id": "1001", "op": "order", "code": "0", "msg": "", "data": [
                {"clOrdId": "T1", "ordId": "1", "ts": str(TS_MS), "sCode": "0", "sMsg": ""}]}

            by_channel = {frame["arg"]["channel"]: frame
                          for frame in [await next_json(ws) for _ in range(3)]}   # orders/balance/positions
            assert set(by_channel) == {ORDER_CHANNEL, BALANCE_CHANNEL, POSITIONS_CHANNEL}
            assert by_channel[ORDER_CHANNEL] == {"arg": {"channel": ORDER_CHANNEL, "instType": "SPOT",
                                                         "instId": BTC}, "data": [order_row()]}
            assert all(isinstance(value, str) for value in by_channel[ORDER_CHANNEL]["data"][0].values())
            element = {"uTime": str(TS_MS),
                       "balData": [{"ccy": "BTC", "cashBal": "100.1", "uTime": str(TS_MS)},
                                   {"ccy": "USDT", "cashBal": "993999.9", "uTime": str(TS_MS)}],
                       "posData": [{"instId": BTC, "instType": "SPOT", "mgnMode": "cash", "posSide": "net",
                                    "pos": "0.1", "uTime": str(TS_MS), "avgPx": "60001"}]}
            assert by_channel[BALANCE_CHANNEL] == {"arg": {"channel": BALANCE_CHANNEL}, "data": [element]}
            assert by_channel[POSITIONS_CHANNEL] == {"arg": {"channel": POSITIONS_CHANNEL}, "data": [
                {"instId": BTC, "instType": "SPOT", "mgnMode": "cash", "posSide": "net", "pos": "0.1",
                 "uTime": str(TS_MS), "avgPx": "60001", "upl": "0", "uplRatio": "0",
                 "notionalUsd": "6000.1"}]}
            # 2026-09-25 语义修订：immediate 成交是隐含流动性（"下单瞬间新对手单吃掉本单"），
            # 不吃簿上挂单 → 盘口原样、不发 book 事件，行情连接没有 bbo 帧 —— 行情只由注入驱动
            assert kernel.book.best(BTC) == ((60000.0, 1.0), (60001.0, 0.5))
            assert published == []
            await ws.close()
            await bbo_ws.close()

    asyncio.run(scenario())


def test_cancel_order_hits_resting_order_and_pushes_canceled():
    """撤掉簿上的挂单：ack `sCode:"0"` → `state:"canceled"` 帧 → 档位消失 + 发布盘口事件。"""
    async def scenario():
        kernel, adapter = make_kernel(), OkxAdapter()
        bind_trading(adapter, kernel)
        published = []
        kernel.bus.subscribe(f"book.{BTC}", lambda payload: published.append(payload))
        async with okx_server(adapter, kernel) as (client, registry):
            ws = await client.ws_connect(PRIVATE_PATH)
            await ws.send_str(sub_op("login", {"apiKey": "k"}))
            assert await next_json(ws) == {"event": "login", "code": "0"}
            await ws.send_str(sub_op("subscribe", {"channel": ORDER_CHANNEL, "instType": "ANY"}))
            assert await next_json(ws) == {"event": "subscribe",
                                           "arg": {"channel": ORDER_CHANNEL, "instType": "ANY"}}

            resting = make_resting("MM1", SIDE_SELL, 60001.5, 2.0)
            kernel.book.place(resting)
            kernel.channels.track(resting)      # 管理面注入的挂单要先登记，撤单才能推出帧
            assert kernel.book.best(BTC) == (None, (60001.5, 2.0))

            await ws.send_str(json.dumps({"id": "1002", "op": "cancel-order",
                                          "args": [{"instId": BTC, "clOrdId": "MM1"}]}))
            assert await next_json(ws) == {"id": "1002", "op": "cancel-order", "code": "0", "msg": "",
                                           "data": [{"clOrdId": "MM1", "ordId": "MM1", "ts": str(TS_MS),
                                                     "sCode": "0", "sMsg": ""}]}
            assert await next_json(ws) == {
                "arg": {"channel": ORDER_CHANNEL, "instType": "SPOT", "instId": BTC},
                "data": [order_row(clOrdId="MM1", ordId="MM1", side="sell", px="60001.5", sz="2",
                                   state="canceled", accFillSz="0", avgPx="0",
                                   fillPx="", fillSz="", fillTime="")]}
            assert kernel.book.best(BTC) == (None, None)      # 档位真的摘掉了
            assert published == [None]
            await ws.close()

    asyncio.run(scenario())


def test_rejections_never_touch_book_and_adapter_binding_guard(caplog):
    """参数错 / 未知合约 / 非限价单 / 撤单未命中回失败 ack（带原因、不动盘口、不推帧）；装配守卫。"""
    async def scenario():
        kernel, registry, ws = make_kernel(), WsRegistry(), FakeWs()
        registry.add(ws, KIND_PRIVATE)
        kernel.book.place(make_resting("MM1", SIDE_BUY, 60000.0, 1.0))
        before = kernel.book.snapshot()
        base = {"instId": BTC, "clOrdId": "T1", "side": "buy", "ordType": "limit", "sz": 1, "px": 60000}
        for bad, s_code in ((dict(base, ordType="market"), "51000"),     # 只支持限价单
                            (dict(base, instId="DOGE-USDT"), "51001"),   # 未知合约
                            (dict(base, clOrdId=""), "51000"),           # 缺 clOrdId
                            (dict(base, px="abc"), "51000"),             # 价不是数值
                            (dict(base, sz=0), "51000"),                 # 量非正
                            (dict(base, side="long"), "51000")):         # 方向非法
            reply = kernel.channels.handle({"id": "1", "op": "order", "args": [bad]}, ws, registry)
            assert (reply["code"], reply["data"][0]["sCode"], bool(reply["data"][0]["sMsg"])) == \
                ("1", s_code, True)                                      # 失败原因要带出来，否则无从排障
        reply = kernel.channels.handle({"id": "2", "op": "cancel-order",
                                        "args": [{"instId": BTC, "clOrdId": "NOPE"}]}, ws, registry)
        assert (reply["code"], reply["data"][0]["sCode"]) == ("1", "51603")
        assert kernel.book.snapshot() == before and ws.sent == []        # 盘口没动过、一条推送都没有
    with caplog.at_level(logging.WARNING):
        asyncio.run(scenario())
    assert len(mockex_warnings(caplog)) == 7        # 每条被拒都留了痕（不静默）

    adapter, kernel = OkxAdapter(), make_kernel()
    with pytest.raises(NotImplementedError):        # 未装配：T6 已有测试守着这个异常类型
        adapter.ws_private_handler({"op": "login"}, None, None)
    bind_trading(adapter, kernel)
    with pytest.raises(RuntimeError):               # 重复装配 = 第二套下单通道
        bind_trading(adapter, kernel)
    assert adapter.ws_private_handler({"op": "login", "args": []}, None, None) == {"event": "login", "code": "0"}


def test_order_td_mode_must_match_instrument_type():
    """`tdMode` 必须与合约类型自洽：现货收 cash，合约收 cross/isolated。

    这是一条**曾经错标的路径**：旧实现把 `tdMode` 直接映射成 instType
    （`cross`/`isolated`→MARGIN），于是合约策略发的 `tdMode=isolated` 被当成币币杠杆
    （`strategy_future_arbi_v3.cpp:112` 给 SWAP 单设的就是 Isolated），只因当时清单里
    没有 MARGIN 行、单键回落兜住了才没炸。现在改为**先按 instId 定候选、tdMode 只做
    校验**：现货配 cross/isolated 会被明确拒掉（那表示币币杠杆，本模拟盘不提供），
    而不是静默当现货成交 —— 否则"以为在下杠杆单、其实下的是现货"。
    """
    swap = "BTC-USDT-SWAP"
    specs = SPECS + [InstrumentConfig(inst_id=swap, inst_type="SWAP", inst_id_code=10459,
                                      base_ccy="BTC", quote_ccy="USDT", ct_val="0.01",
                                      ct_val_ccy="BTC")]
    book, bus = OrderBook(), EventBus()
    accounts = AccountManager(
        [AccountConfig(api_key=ACCOUNT, balances={"USDT": 1_000_000.0, "BTC": 100.0})], instruments=specs)
    channels = PrivateChannels(MatchingEngine(book, clock=lambda: TS_MS), accounts, bus,
                               instruments=specs, account=ACCOUNT, now_ms=lambda: TS_MS)
    ws, registry = FakeWs(), WsRegistry()
    registry.add(ws, KIND_PRIVATE)
    base = {"clOrdId": "T1", "side": "buy", "ordType": "limit", "sz": 1, "px": 60000}

    def order(inst_id, td_mode):
        arg = dict(base, instId=inst_id, tdMode=td_mode)
        reply = channels.handle({"id": "1", "op": "order", "args": [arg]}, ws, registry)
        return reply["code"], reply["data"][0]["sCode"], reply["data"][0]["sMsg"]

    async def scenario():
        # 现货 + 杠杆模式 → 参数错（51000，不是"未知合约"），原因要点明不支持杠杆
        for td_mode in ("cross", "isolated"):
            code, s_code, s_msg = order(BTC, td_mode)
            assert (code, s_code) == ("1", "51000")
            assert "币币杠杆" in s_msg
        # 现货 + cash → 放行
        assert order(BTC, "cash")[1] == "0"
        # 合约 + cross/isolated 是保证金模式，合法；cash 才不自洽
        assert order(swap, "isolated")[1] == "0"
        assert order(swap, "cross")[1] == "0"
        code, s_code, s_msg = order(swap, "cash")
        assert (code, s_code) == ("1", "51000") and "cross" in s_msg

    asyncio.run(scenario())


def test_positions_frame_fills_contract_fields_and_keeps_source_intact():
    """`positions` 帧补齐引擎无条件读的 upl/uplRatio/notionalUsd（绝对值口径），且不改调用方的快照行。"""
    row = {"instId": BTC, "instType": "SPOT", "mgnMode": "cash", "posSide": "net",
           "pos": "-0.5", "uTime": str(TS_MS), "avgPx": "60000"}
    data = positions_frame(row)["data"][0]
    assert (data["upl"], data["uplRatio"], data["notionalUsd"]) == ("0", "0", "30000")
    assert all(isinstance(value, str) for value in data.values()) and "upl" not in row


# ── orderbook 模式（M2）：挂单进簿 + 改簿即推 bbo ─────────────────────────────

def make_orderbook_kernel():
    """orderbook 模式的同款内核：账户/委托索引照旧，只把撮合模式换成进簿撮合。"""
    kernel = make_kernel()
    kernel.engine = MatchingEngine(kernel.book, FILL_MODE_ORDERBOOK, clock=lambda: TS_MS)
    kernel.channels = PrivateChannels(kernel.engine, kernel.accounts, kernel.bus,
                                      instruments=SPECS, account=ACCOUNT, now_ms=lambda: TS_MS)
    return kernel


def test_orderbook_order_rests_pushes_live_frame_and_book_event():
    """无对手时整单挂进簿：ack `sCode:"0"`、`orders` 帧 `state:"live"`、发布盘口事件。"""
    async def scenario():
        kernel, adapter = make_orderbook_kernel(), OkxAdapter()
        bind_trading(adapter, kernel)
        published = []
        kernel.bus.subscribe(f"book.{BTC}", lambda payload: published.append(payload))
        async with okx_server(adapter, kernel) as (client, registry):
            ws = await client.ws_connect(PRIVATE_PATH)
            await ws.send_str(sub_op("login", {"apiKey": "k"}))
            assert await next_json(ws) == {"event": "login", "code": "0"}
            await ws.send_str(sub_op("subscribe", {"channel": ORDER_CHANNEL, "instType": "ANY"},
                                     {"channel": BALANCE_CHANNEL},
                                     {"channel": POSITIONS_CHANNEL, "instType": "ANY"}))
            assert len([await next_json(ws) for _ in range(3)]) == 3      # 逐 arg 一帧回执

            await ws.send_str(json.dumps({"id": "2001", "op": "order", "args": [
                {"instId": BTC, "tdMode": "cash", "clOrdId": "R1", "side": "buy", "posSide": "net",
                 "ordType": "limit", "sz": 0.1, "px": 60000}]}))
            assert await next_json(ws) == {"id": "2001", "op": "order", "code": "0", "msg": "", "data": [
                {"clOrdId": "R1", "ordId": "1", "ts": str(TS_MS), "sCode": "0", "sMsg": ""}]}

            # 无成交、无持仓 → 只推 orders + balance 两帧（positions 没有行可推）
            by_channel = {frame["arg"]["channel"]: frame
                          for frame in [await next_json(ws) for _ in range(2)]}
            assert set(by_channel) == {ORDER_CHANNEL, BALANCE_CHANNEL}
            row = by_channel[ORDER_CHANNEL]["data"][0]
            assert row == order_row(clOrdId="R1", ordId="1", px="60000", sz="0.1", state="live",
                                    accFillSz="0", avgPx="0",
                                    fillPx="", fillSz="", fillTime="", uTime=str(TS_MS))
            assert kernel.book.best(BTC) == ((60000.0, 0.1), None)      # 委托真在簿上
            assert published == [None]                                   # 改簿 → 必须推 bbo
            await ws.close()

    asyncio.run(scenario())


def test_orderbook_partial_fill_pushes_partially_filled_and_rests_remainder():
    """对手只有 0.01 时买 0.1：成 0.01（对手价）、余 0.09 挂买一，推 `partially_filled` 帧。"""
    async def scenario():
        kernel, adapter = make_orderbook_kernel(), OkxAdapter()
        bind_trading(adapter, kernel)
        published = []
        kernel.bus.subscribe(f"book.{BTC}", lambda payload: published.append(payload))
        async with okx_server(adapter, kernel) as (client, registry):
            ws = await client.ws_connect(PRIVATE_PATH)
            await ws.send_str(sub_op("login", {"apiKey": "k"}))
            assert await next_json(ws) == {"event": "login", "code": "0"}
            await ws.send_str(sub_op("subscribe", {"channel": ORDER_CHANNEL, "instType": "ANY"},
                                     {"channel": BALANCE_CHANNEL},
                                     {"channel": POSITIONS_CHANNEL, "instType": "ANY"}))
            assert len([await next_json(ws) for _ in range(3)]) == 3      # 逐 arg 一帧回执

            resting = make_resting("MM1", SIDE_SELL, 60001.0, 0.01)
            kernel.book.place(resting)
            kernel.channels.track(resting)

            await ws.send_str(json.dumps({"id": "2002", "op": "order", "args": [
                {"instId": BTC, "tdMode": "cash", "clOrdId": "R2", "side": "buy", "posSide": "net",
                 "ordType": "limit", "sz": 0.1, "px": 60001}]}))
            assert (await next_json(ws))["data"][0]["sCode"] == "0"

            # 成交 → 推 orders + balance + positions 三帧
            by_channel = {frame["arg"]["channel"]: frame
                          for frame in [await next_json(ws) for _ in range(3)]}
            row = by_channel[ORDER_CHANNEL]["data"][0]
            assert (row["state"], row["accFillSz"], row["avgPx"], row["fillPx"], row["fillSz"]) == \
                ("partially_filled", "0.01", "60001", "60001", "0.01")
            assert kernel.book.best(BTC) == ((60001.0, 0.09), None)      # 余量挂回买一
            assert published == [None]
            await ws.close()

    asyncio.run(scenario())


def test_orderbook_maker_order_gets_its_own_fill_push():
    """被动方（先挂进簿的引擎单）被吃时也要收 `orders` 帧 —— 否则引擎侧停在"已报/filled=0"。"""
    async def scenario():
        kernel, adapter = make_orderbook_kernel(), OkxAdapter()
        bind_trading(adapter, kernel)
        async with okx_server(adapter, kernel) as (client, registry):
            ws = await client.ws_connect(PRIVATE_PATH)
            await ws.send_str(sub_op("login", {"apiKey": "k"}))
            assert await next_json(ws) == {"event": "login", "code": "0"}
            await ws.send_str(sub_op("subscribe", {"channel": ORDER_CHANNEL, "instType": "ANY"},
                                     {"channel": BALANCE_CHANNEL},
                                     {"channel": POSITIONS_CHANNEL, "instType": "ANY"}))
            assert len([await next_json(ws) for _ in range(3)]) == 3

            # ① 先挂一张买单（无对手 → live）
            await ws.send_str(json.dumps({"id": "3001", "op": "order", "args": [
                {"instId": BTC, "tdMode": "cash", "clOrdId": "B1", "side": "buy", "posSide": "net",
                 "ordType": "limit", "sz": 1.0, "px": 60000}]}))
            assert (await next_json(ws))["data"][0]["sCode"] == "0"
            resting_frame = {frame["arg"]["channel"]: frame
                             for frame in [await next_json(ws) for _ in range(2)]}
            assert resting_frame[ORDER_CHANNEL]["data"][0]["state"] == "live"

            # ② 再下卖单吃它 0.4：主动方一帧 + 被动方（B1）一帧 + balance/positions
            await ws.send_str(json.dumps({"id": "3002", "op": "order", "args": [
                {"instId": BTC, "tdMode": "cash", "clOrdId": "S1", "side": "sell", "posSide": "net",
                 "ordType": "limit", "sz": 0.4, "px": 60000}]}))
            assert (await next_json(ws))["data"][0]["sCode"] == "0"
            orders_frames = [await next_json(ws) for _ in range(2)]     # 主动方 + 被动方
            rows = {frame["data"][0]["clOrdId"]: frame["data"][0] for frame in orders_frames}
            assert set(rows) == {"B1", "S1"}
            assert (rows["S1"]["state"], rows["S1"]["accFillSz"]) == ("filled", "0.4")
            # 被动方：部分成交、累计口径，成交价恒为其挂单价
            assert (rows["B1"]["state"], rows["B1"]["accFillSz"], rows["B1"]["avgPx"]) == \
                ("partially_filled", "0.4", "60000")
            assert (rows["B1"]["fillPx"], rows["B1"]["fillSz"]) == ("60000", "0.4")
            assert kernel.book.best(BTC) == ((60000.0, 0.6), None)
            await ws.close()

    asyncio.run(scenario())
