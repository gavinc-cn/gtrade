"""WS 会话层：文本帧分派契约测试（Fix round 1 补测，先红后绿）。

`on_json` 的输入契约：**只有能解析为 JSON 对象的文本帧**才交给注入的 handler。
`"pong"`（协议层心跳回包）、非 JSON 文本、能解析但不是对象的 JSON（`123` / `[...]` / `null`）
一律由本层吞掉 —— 放行会让 T6/T7 的 handler 在 `json.loads` 上抛异常，而本层不吞异常，
连接被静默关闭，表现为"引擎反复重连而 mock 无痕"。

驱动方式同 `test_ws_session.py`：同步测试函数 + `asyncio.run`，不引 pytest-asyncio。
"""

import asyncio
import functools
import json
from contextlib import asynccontextmanager

from aiohttp import WSMsgType, web
from aiohttp.test_utils import TestClient, TestServer

from mockex.protocol.ws import WsRegistry, handle_ws

PUBLIC_PATH = "/ws/v5/public"


@asynccontextmanager
async def running_server(seen):
    """挂一条 public 路由，注入"只记录调用"的 handler（不自动回帧）；yield 出客户端。"""
    registry = WsRegistry()

    async def on_json(ws, text):
        seen.append(text)

    app = web.Application()
    app.router.add_get(PUBLIC_PATH, functools.partial(handle_ws, registry=registry, on_json=on_json))
    async with TestClient(TestServer(app)) as client:
        yield client


async def next_text(ws, timeout=2.0):
    """读下一条文本帧；超时即失败（测试挂死比断言失败难查一个数量级）。"""
    msg = await asyncio.wait_for(ws.receive(), timeout)
    assert msg.type is WSMsgType.TEXT, f"期望 TEXT 帧，实得 {msg.type}: {msg.data!r}"
    return msg.data


def test_pong_text_is_swallowed_and_connection_survives():
    """`"pong"` 是协议层心跳回包：不进 handler，也不能因此断连。"""
    async def scenario():
        seen = []
        async with running_server(seen) as client:
            ws = await client.ws_connect(PUBLIC_PATH)
            await ws.send_str("pong")
            await ws.send_str("ping")
            assert await next_text(ws) == "pong"   # 连接仍在：对随后的 ping 照常回 pong
            assert ws.closed is False
            assert seen == []
            await ws.close()

    asyncio.run(scenario())


def test_non_json_and_non_object_text_never_reach_handler():
    """非 JSON 文本、能解析但不是对象的 JSON：都吞掉，连接不关闭。"""
    async def scenario():
        seen = []
        async with running_server(seen) as client:
            ws = await client.ws_connect(PUBLIC_PATH)
            for text in ("hello", "{不合法", "123", "[1,2]", "null"):
                await ws.send_str(text)
            await ws.send_str("ping")
            assert await next_text(ws) == "pong"   # 中间没多回帧，且连接没被关
            assert ws.closed is False
            assert seen == []
            await ws.close()

    asyncio.run(scenario())


def test_valid_json_object_reaches_handler_as_raw_text():
    """合法 JSON 对象（含嵌套）仍正常投递；handler 收到的是原文，协议层不预解析。"""
    async def scenario():
        seen = []
        frame = {"op": "subscribe", "args": [{"channel": "bbo-tbt", "instId": "BTC-USDT"}]}
        raw = json.dumps(frame)
        async with running_server(seen) as client:
            ws = await client.ws_connect(PUBLIC_PATH)
            await ws.send_str(raw)
            await ws.send_str("ping")
            assert await next_text(ws) == "pong"   # 顺序保证：handler 没有额外回帧
            assert seen == [raw]
            await ws.close()

    asyncio.run(scenario())
