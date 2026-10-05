"""WS 会话层：订阅表 + 心跳（aiohttp）。

本模块只管"会话"这件事——谁连着、谁订了什么、帧该发给谁、心跳怎么回；**不认识任何
OKX 频道业务**（subscribe 回执、login、order / cancel-order 全在 `exchanges/okx/` 的
`ws_public.py` / `ws_private.py`，由 T6/T7 经 `handle_ws(on_json=...)` 注入）。

三条定死的语义：

1. **订阅匹配**：`broadcast(channel, inst_id, payload)` 发给订阅了 `(channel, inst_id)`
   的连接 **加上** 订阅了 `(channel, None)` 的连接（`None` = 该频道不吃 instrument 约束，
   私有 `orders` / `positions` / `balance_and_position` 就是这种）；两者都不匹配则一条不发。
   同一条连接同时命中两条规则也只收到一帧 —— 重复 bbo 会让引擎双计。
2. **同步发送接口**：`broadcast` 是同步方法，内部把 `ws.send_str` 排成 asyncio 任务 ——
   内核事件总线的回调是同步函数（T4 约定），桥接层不该自己操心 `create_task`。
   代价是发送失败（连接刚断）只能在任务里消化（debug 日志，不掀翻撮合路径），
   因此 `broadcast` 必须在事件循环里调用，否则当场 `RuntimeError`。
3. **心跳是保命线**：收到文本 `"ping"` 立即回文本 `"pong"`，任何时刻、任何盘口状态下都回。
   引擎每 20s 发一次 ping，25s 收不到入站消息就断开重连，而 `OkxWs::on_message` 在
   任何入站报文（含 pong）上刷新 `m_last_msg_recv_time` —— 回 pong 就是引擎的保命线。
   这条路径绕过 `on_json`，也不看订阅表。
"""

import asyncio
import json
import logging
from dataclasses import dataclass, field
from typing import Awaitable, Callable

from aiohttp import WSMsgType, web

__all__ = ["KIND_PRIVATE", "KIND_PUBLIC", "JsonHandler", "WsRegistry", "handle_ws"]

KIND_PUBLIC = "public"
KIND_PRIVATE = "private"
_KINDS = (KIND_PUBLIC, KIND_PRIVATE)

# 注入的 JSON 文本处理器：`(ws, text)`，自行解析原文并回帧（业务归 T6/T7）
JsonHandler = Callable[[web.WebSocketResponse, str], Awaitable[None]]

logger = logging.getLogger(__name__)


@dataclass
class _Session:
    """一条 WS 会话：身份（kind）+ 订阅集合 `(channel, instId)`（instId=None 表示不吃 inst 约束）。"""

    kind: str
    subs: set[tuple[str, str | None]] = field(default_factory=set)


def _swallow_send_error(task: asyncio.Task) -> None:
    """消化发送任务的异常：连接刚断开时发送失败属常态（引擎会自己重连），不上报。

    不取一次异常，任务回收时事件循环会刷 "Task exception was never retrieved" 警告；
    真要排障看这个 debug 日志。
    """
    if not task.cancelled() and task.exception() is not None:
        logger.debug("WS 发送失败（连接应已断开）: %r", task.exception())


class WsRegistry:
    """连接 → 订阅集合的登记表（一个进程一个实例，由 T10 装配）。

    键是连接对象（生产是 aiohttp `WebSocketResponse`，按身份哈希）；值只在 `add` 时建一次，
    `remove` 时整体摘除 —— 连接断开后表里不留死引用。
    """

    def __init__(self) -> None:
        self._sessions: dict[web.WebSocketResponse, _Session] = {}

    def add(self, ws: web.WebSocketResponse, kind: str) -> None:
        """登记一条新连接；`kind` 区分 public / private 两条 WS 路径。

        重复 add 同一条连接幂等（handler 重入不报错），但不许改 kind —— 那说明路由装配错了。

        Raises:
            ValueError: kind 不是 "public" / "private"。
            ValueError: 同一连接已以另一种 kind 登记。
        """
        if kind not in _KINDS:
            raise ValueError(f"kind 只认 {_KINDS}: {kind!r}")
        session = self._sessions.get(ws)
        if session is None:
            self._sessions[ws] = _Session(kind=kind)
        elif session.kind != kind:
            raise ValueError(f"连接已以 kind={session.kind!r} 登记，不能再改成 {kind!r}")

    def subscribe(self, ws: web.WebSocketResponse, channel: str, inst_id: str | None = None) -> None:
        """登记订阅 `(channel, inst_id)`；`inst_id=None` 表示该频道不分 instrument。

        幂等：同一对重复登记只存一份，广播时不会重复发帧。

        Raises:
            KeyError: 连接未登记（先 `add`）—— 未登记的订阅永远不会被投递。
            ValueError: channel 为空 —— 空 channel 永远不会被命中，属写错，当场报。
            TypeError: inst_id 既不是 str 也不是 None（传对象会静默永不命中）。
        """
        if not channel:
            raise ValueError("channel 不能为空")
        if inst_id is not None and not isinstance(inst_id, str):
            raise TypeError(f"inst_id 只认 str 或 None: {inst_id!r}")
        self._session(ws).subs.add((channel, inst_id))

    def unsubscribe(self, ws: web.WebSocketResponse, channel: str, inst_id: str | None = None) -> bool:
        """退订，返回是否确有订阅被移除；未登记过（含连接已断）返回 False 且不报错。"""
        session = self._sessions.get(ws)
        if session is None:
            return False
        before = len(session.subs)
        session.subs.discard((channel, inst_id))
        return len(session.subs) != before

    def remove(self, ws: web.WebSocketResponse) -> bool:
        """整条连接下线（连同它的全部订阅），返回是否确有登记被移除；重复调用幂等。"""
        return self._sessions.pop(ws, None) is not None

    def count(self, kind: str | None = None) -> int:
        """连接数；给 `kind` 时只数该身份（`/admin/state` 与测试用）。

        Raises:
            ValueError: kind 既不是 None 也不是已知身份。
        """
        if kind is None:
            return len(self._sessions)
        if kind not in _KINDS:
            raise ValueError(f"kind 只认 None 或 {_KINDS}: {kind!r}")
        return sum(1 for session in self._sessions.values() if session.kind == kind)

    def broadcast(self, channel: str, inst_id: str | None, payload) -> int:
        """把一帧发给匹配的订阅者，返回**实际排入发送**的连接数。

        匹配规则见模块文档第 1 条；已关闭但还没走完 `remove` 的连接跳过（发送必失败），
        也不计入返回值。`payload` 不做类型转换以外的加工：str 原样发，其余对象
        `json.dumps` 后发（可以直接丢 codec 报文）。

        Args:
            channel: 频道名（如 "bbo-tbt"）。
            inst_id: instrument；`None` 表示"未指名 instrument"，此时只发给
                `(channel, None)` 订阅者，不喂给盯具体 inst 的连接。
            payload: str（原样）或可 JSON 序列化对象。

        Returns:
            int：排入发送的连接数（0 = 无人订阅，静默无操作）。

        Raises:
            ValueError: channel 为空。
            RuntimeError: 在事件循环之外调用 —— 发送要排异步任务，装配错误必须当场可见。
        """
        if not channel:
            raise ValueError("channel 不能为空")
        loop = asyncio.get_running_loop()
        text = payload if isinstance(payload, str) else json.dumps(payload, ensure_ascii=False)
        sent = 0
        for ws, session in list(self._sessions.items()):  # 快照：发送失败/回调里改表都不影响本轮
            if not self._matched(session, channel, inst_id) or ws.closed:
                continue
            task = loop.create_task(ws.send_str(text))
            task.add_done_callback(_swallow_send_error)
            sent += 1
        return sent

    @staticmethod
    def _matched(session: _Session, channel: str, inst_id: str | None) -> bool:
        """订阅命中判定：精确 `(channel, inst_id)` 或频道级 `(channel, None)`。"""
        return (channel, inst_id) in session.subs or (channel, None) in session.subs

    def _session(self, ws: web.WebSocketResponse) -> _Session:
        """取连接会话，未登记即报（写错的订阅永远不会被投递，不能静默）。"""
        session = self._sessions.get(ws)
        if session is None:
            raise KeyError("连接未登记：先 add(ws, kind)")
        return session


async def handle_ws(request: web.Request, registry: WsRegistry, *, kind: str = KIND_PUBLIC,
                    on_json: JsonHandler | None = None) -> web.WebSocketResponse:
    """aiohttp WS handler：登记会话 → 分发报文 → 断开即摘表。

    挂路由（T6/T7，与真实 OKX 的 public/private 分路一致）：

        PUBLIC = functools.partial(handle_ws, registry=reg, on_json=on_public_json)
        app.router.add_get("/ws/v5/public", PUBLIC)
        app.router.add_get("/ws/v5/private", functools.partial(
            handle_ws, registry=reg, kind=KIND_PRIVATE, on_json=on_private_json))

    报文处理：
    - 文本 `"ping"` → 回文本 `"pong"`（保命线，绕过 on_json，不看订阅表）；
    - **能解析为 JSON 对象**的文本 → `await on_json(ws, text)`（原文透传，协议层不预解析）；
    - 其余文本（`"pong"` 心跳回包、非 JSON 文本、`123` / `[...]` 这类非对象 JSON）→
      本层吞掉并记一条 debug（见 `_is_json_object`：放行会让 handler 在 `json.loads` 上抛异常，
      而本层不吞异常 → 连接被静默关闭 → "引擎反复重连而 mock 无痕"）；
    - `on_json=None`（业务未接）时上述文本一律忽略，连接保持；
    - 非文本帧（二进制/关闭帧/控制帧）一律忽略 —— WS 级 PING 由 aiohttp 协议层自动回 PONG。

    `on_json` 抛出的异常不吞：handler 的 bug 当场可见，连接随之关闭（会话在 `finally`
    里摘除）。`add` / `remove` 都在本函数内配对完成，断线不会在订阅表里留死连接。

    Returns:
        web.WebSocketResponse：aiohttp 要求的返回对象（连接已在此过程中处理完毕）。
    """
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    registry.add(ws, kind)
    try:
        async for msg in ws:
            if msg.type is not WSMsgType.TEXT:
                continue
            if msg.data == "ping":
                await ws.send_str("pong")
            elif on_json is not None and _is_json_object(msg.data):
                await on_json(ws, msg.data)
            else:
                logger.debug("忽略非 JSON 对象文本帧: %r", msg.data)
    finally:
        registry.remove(ws)
    return ws


def _is_json_object(text: str) -> bool:
    """文本帧是否为 JSON 对象（`{...}`）—— `on_json` 的输入契约就在这一处兜住。

    OKX 协议面的指令（subscribe/unsubscribe/login/order/cancel-order）**全是 JSON 对象**，
    解析不了或不是对象的只可能是心跳回包（`"pong"`）或噪声。按"能解析就放行"处理会让
    T6/T7 的 handler 在 `json.loads` 上抛异常，连接随即断开且本层不写日志 —— 排障时只看到
    引擎反复重连。故这里要求严格：非对象 JSON（`123`、`[...]`、`null`）同样不投递。
    """
    try:
        return isinstance(json.loads(text), dict)
    except json.JSONDecodeError:
        return False
