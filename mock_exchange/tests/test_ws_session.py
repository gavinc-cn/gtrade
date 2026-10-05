"""WS 会话层测试（TDD：先红后绿）。

环境未装 pytest-asyncio（也不打算装）：测试函数一律同步、协程用 `asyncio.run` 驱动，
真实 WS 通路用 `aiohttp.test_utils.TestServer` / `TestClient` 起在临时端口上。

本文件把 T6/T7 依赖的订阅表语义钉死：`broadcast(channel, inst)` 只发给订阅了
`(channel, inst)` 或 `(channel, None)` 的连接，其余一条不发；同一条连接同时命中两条
规则也只收到一帧（重复 bbo 会让引擎双计）。
"""

import asyncio
import functools
import json
from contextlib import asynccontextmanager

import pytest
from aiohttp import WSMsgType, web
from aiohttp.test_utils import TestClient, TestServer

from mockex.core.events import EventBus
from mockex.protocol.ws import KIND_PRIVATE, KIND_PUBLIC, WsRegistry, handle_ws

PUBLIC_PATH, PRIVATE_PATH = "/ws/v5/public", "/ws/v5/private"
BBO, ORDERS = "bbo-tbt", "orders"
BTC, ETH = "BTC-USDT", "ETH-USDT"


class FakeWs:
    """只实现订阅表用到的两个成员（`send_str` / `closed`），让匹配语义能脱离网络单测。"""

    def __init__(self, closed=False):
        self.sent, self.closed = [], closed

    async def send_str(self, text):
        if self.closed:
            raise ConnectionResetError("closed")
        self.sent.append(text)


def make_conn(reg, kind, *pairs):
    """造一条假连接：登记 kind 并按 (channel, instId) 逐个订阅。"""
    ws = FakeWs()
    reg.add(ws, kind)
    for channel, inst in pairs:
        reg.subscribe(ws, channel, inst)
    return ws


async def settle(turns=5):
    """让 `broadcast` 内部排出的发送任务跑完（只让事件循环转圈，不等墙钟）。"""
    for _ in range(turns):
        await asyncio.sleep(0)


@asynccontextmanager
async def running_server(registry, on_json=None):
    """把 `handle_ws` 挂到 public / private 两条路径上跑（同真实 OKX 分路），yield 出客户端。"""
    app = web.Application()
    app.router.add_get(PUBLIC_PATH, functools.partial(handle_ws, registry=registry, on_json=on_json))
    app.router.add_get(PRIVATE_PATH, functools.partial(handle_ws, registry=registry,
                                                       kind=KIND_PRIVATE, on_json=on_json))
    async with TestClient(TestServer(app)) as client:
        yield client


async def next_text(ws, timeout=2.0):
    """读下一条文本帧；超时即失败（测试挂死比断言失败难查一个数量级）。"""
    msg = await asyncio.wait_for(ws.receive(), timeout)
    assert msg.type is WSMsgType.TEXT, f"期望 TEXT 帧，实得 {msg.type}: {msg.data!r}"
    return msg.data


def echo_subscribe(registry):
    """T6 的 handler 骨架：按 op 登记订阅并回执（真实版在 exchanges/okx/ws_public.py）。"""

    async def on_json(ws, text):
        req = json.loads(text)
        if req.get("op") == "subscribe":
            registry.subscribe(ws, req["channel"], req.get("instId"))
        await ws.send_str(json.dumps({"event": req.get("op")}))

    return on_json


def test_broadcast_reaches_exact_inst_wildcard_and_dedupes():
    async def scenario():
        reg = WsRegistry()
        exact = make_conn(reg, KIND_PUBLIC, (BBO, BTC))
        both = make_conn(reg, KIND_PUBLIC, (BBO, BTC), (BBO, None))   # 命中两条规则
        wildcard = make_conn(reg, KIND_PRIVATE, (ORDERS, None))       # 私有频道不吃 inst 约束
        other = make_conn(reg, KIND_PUBLIC, (BBO, ETH))

        assert reg.broadcast(BBO, BTC, "frame-1") == 2
        assert reg.broadcast(ORDERS, BTC, "o1") == 1                  # 通配订阅收任意 inst
        assert reg.broadcast(ORDERS, ETH, "o2") == 1
        await settle()
        assert (exact.sent, wildcard.sent, other.sent) == (["frame-1"], ["o1", "o2"], [])
        assert both.sent == ["frame-1"]                               # 去重：只收一帧
        assert reg.count() == 4

    asyncio.run(scenario())


def test_broadcast_to_unrelated_channel_or_inst_reaches_nobody():
    async def scenario():
        reg = WsRegistry()
        exact = make_conn(reg, KIND_PUBLIC, (BBO, BTC))

        assert reg.broadcast("books", BTC, "x") == 0      # channel 不匹配
        assert reg.broadcast(BBO, ETH, "x") == 0          # inst 不匹配
        assert reg.broadcast(BBO, None, "x") == 0         # 未指名 inst：不喂给盯具体 inst 的连接
        assert reg.broadcast(BBO, BTC, "y") == 1          # 配对的那条仍通，且只收到这一帧
        await settle()
        assert exact.sent == ["y"]

    asyncio.run(scenario())


def test_unsubscribe_and_remove_drop_delivery_idempotently():
    async def scenario():
        reg = WsRegistry()
        first = make_conn(reg, KIND_PUBLIC, (BBO, BTC))
        second = make_conn(reg, KIND_PUBLIC, (BBO, BTC))

        assert reg.unsubscribe(first, BBO, BTC) is True
        assert reg.unsubscribe(first, BBO, BTC) is False   # 未登记的退订：静默失败
        assert reg.remove(second) is True
        assert reg.remove(second) is False                 # 断连清理也要幂等
        assert reg.count() == 1
        assert reg.broadcast(BBO, BTC, "x") == 0
        await settle()
        assert (first.sent, second.sent) == ([], [])

    asyncio.run(scenario())


def test_broadcast_skips_closed_connection_without_raising():
    """`remove` 还没轮到的已关闭连接：跳过，不让发送失败掀翻调用方（撮合路径）。"""
    async def scenario():
        reg = WsRegistry()
        dead = make_conn(reg, KIND_PUBLIC, (BBO, BTC))
        dead.closed = True

        assert reg.broadcast(BBO, BTC, "x") == 0
        await settle()
        assert dead.sent == []

    asyncio.run(scenario())


def test_broadcast_passes_text_through_and_json_encodes_objects():
    async def scenario():
        reg = WsRegistry()
        conn = make_conn(reg, KIND_PUBLIC, (BBO, BTC))
        frame = {"arg": {"channel": BBO, "instId": BTC}, "data": [{"ts": "1700000000123"}]}

        assert reg.broadcast(BBO, BTC, "raw-text") == 1
        assert reg.broadcast(BBO, BTC, frame) == 1
        await settle()
        assert conn.sent == ["raw-text", json.dumps(frame, ensure_ascii=False)]

    asyncio.run(scenario())


def test_registration_rejects_bad_input_loudly():
    reg = WsRegistry()
    stray, good = FakeWs(), FakeWs()
    with pytest.raises(KeyError):
        reg.subscribe(stray, BBO, BTC)                    # 未 add 就订：永远不会被投递，当场报
    with pytest.raises(ValueError):
        reg.add(good, "admin")                            # kind 只认 public / private
    reg.add(good, KIND_PUBLIC)
    reg.add(good, KIND_PUBLIC)                            # 重复 add 幂等
    assert (reg.count(), reg.count(KIND_PUBLIC), reg.count(KIND_PRIVATE)) == (1, 1, 0)
    with pytest.raises(ValueError):
        reg.add(good, KIND_PRIVATE)                       # 一条连接只有一种身份
    with pytest.raises(ValueError):
        reg.count("admin")
    with pytest.raises(ValueError):
        reg.subscribe(good, "", BTC)                      # 空 channel 永不命中
    with pytest.raises(ValueError):
        reg.broadcast("", BTC, "x")
    with pytest.raises(TypeError):
        reg.subscribe(good, BBO, 0)                       # instId 只认 str / None


def test_broadcast_outside_event_loop_is_loud():
    """发送要排异步任务：不在事件循环里调用属装配错误，必须当场报而不是静默丢帧。"""
    reg = WsRegistry()
    make_conn(reg, KIND_PUBLIC, (BBO, BTC))
    with pytest.raises(RuntimeError):
        reg.broadcast(BBO, BTC, "x")


def test_ping_is_answered_repeatedly_and_bare_json_is_ignored():
    """保命线：与订阅/盘口无关，连发多次也必须每次回 pong（引擎 25s 判超时）；
    未注入 handler 时裸 JSON 被忽略且不断连接（业务由 T6/T7 注入）。"""
    async def scenario():
        async with running_server(WsRegistry()) as client:
            ws = await client.ws_connect(PUBLIC_PATH)
            await ws.send_str('{"op":"subscribe"}')
            for _ in range(3):
                await ws.send_str("ping")
                assert await next_text(ws) == "pong"   # 序列里第一个不是 pong 就说明多回了帧
            await ws.close()

    asyncio.run(scenario())


def test_json_text_goes_to_injected_handler_not_pong():
    async def scenario():
        seen = []

        async def on_json(ws, text):
            seen.append(text)
            await ws.send_str(json.dumps({"event": "echo", "got": text}))

        async with running_server(WsRegistry(), on_json=on_json) as client:
            ws = await client.ws_connect(PUBLIC_PATH)
            raw = '{"op":"subscribe","args":[]}'
            await ws.send_str(raw)
            assert json.loads(await next_text(ws)) == {"event": "echo", "got": raw}
            assert seen == [raw]                       # 原文透传，协议层不预解析
            await ws.close()

    asyncio.run(scenario())


def test_subscriptions_drive_broadcast_and_paths_stay_isolated():
    """T6/T7 的真实用法：handler 解析 op 后自行登记订阅，broadcast 按表投递、互不串台。"""
    async def scenario():
        registry = WsRegistry()
        async with running_server(registry, on_json=echo_subscribe(registry)) as client:
            btc = await client.ws_connect(PUBLIC_PATH)
            eth = await client.ws_connect(PUBLIC_PATH)
            priv = await client.ws_connect(PRIVATE_PATH)
            for ws, inst in ((btc, BTC), (eth, ETH)):
                await ws.send_str(json.dumps({"op": "subscribe", "channel": BBO, "instId": inst}))
                assert json.loads(await next_text(ws)) == {"event": "subscribe"}
            await priv.send_str(json.dumps({"op": "subscribe", "channel": ORDERS}))
            assert json.loads(await next_text(priv)) == {"event": "subscribe"}
            assert (registry.count(), registry.count(KIND_PRIVATE)) == (3, 1)

            registry.broadcast(BBO, ETH, {"n": 1})          # 只该到 eth
            registry.broadcast(BBO, BTC, {"n": 2})          # 只该到 btc
            registry.broadcast(ORDERS, BTC, {"ord": "a"})   # 私有 orders 不吃 inst 约束
            assert json.loads(await next_text(eth)) == {"n": 1}
            assert json.loads(await next_text(btc)) == {"n": 2}
            assert json.loads(await next_text(priv)) == {"ord": "a"}
            for ws in (btc, eth, priv):
                await ws.close()

    asyncio.run(scenario())


def test_disconnect_removes_session_and_subscriptions():
    async def scenario():
        registry = WsRegistry()
        async with running_server(registry) as client:
            ws = await client.ws_connect(PUBLIC_PATH)
            assert registry.count() == 1
            await ws.close()
            for _ in range(50):                            # 等服务端读到 FIN 并走完清理
                if registry.count() == 0:
                    break
                await asyncio.sleep(0.01)
            assert registry.count() == 0
            assert registry.broadcast(BBO, BTC, "x") == 0

    asyncio.run(scenario())


def test_event_bus_callback_can_drive_broadcast():
    """同步总线回调 → broadcast（内部排异步发送）：T6/T7 的桥接就靠这条缝。"""
    async def scenario():
        registry = WsRegistry()

        async def on_json(ws, text):
            registry.subscribe(ws, BBO, BTC)
            await ws.send_str("ready")

        async with running_server(registry, on_json=on_json) as client:
            ws = await client.ws_connect(PUBLIC_PATH)
            await ws.send_str("{}")
            assert await next_text(ws) == "ready"

            def on_book(payload):                          # 同步回调（T4 约定）
                registry.broadcast(BBO, BTC, payload)

            bus = EventBus()
            bus.subscribe(f"book.{BTC}", on_book)
            assert bus.publish(f"book.{BTC}", {"seqId": 7}) == 1
            assert json.loads(await next_text(ws)) == {"seqId": 7}
            await ws.close()

    asyncio.run(scenario())
