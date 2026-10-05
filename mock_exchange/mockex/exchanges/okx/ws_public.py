"""OKX 公共频道：`bbo-tbt` 订阅回执 + 订单簿变化推送。

两块内容，一块在协议面、一块在内核面：

- `public_handler`：同步处理 `{"op":"subscribe"/"unsubscribe","args":[{...}]}` —— 登记/摘除
  `WsRegistry` 里的订阅并向调用方返回回执帧（真正发送在 `base.make_json_handler` 里）；
- `BboBroadcaster`：`EventBus` 的 `book.<instId>` 事件 → `codec.bbo_frame` → `registry.broadcast`。
  内核不认识 OKX 帧、WS 层不认识订单簿，这条桥就是本任务（T3 裁定）的落点。

三条写死的语义：

1. **空盘口不推送**：买卖两侧都没有挂单时一条帧都不发（spec：初始空盘口、无推送；引擎侧靠
   pong 保鲜）。只有单侧的盘口照推，缺的一侧编码成空数组 —— 引擎 `OnBboTbt` 对 `asks`/`bids`
   各自判空后写 `ask_cnt`/`bid_cnt`，单边帧是它明确支持的输入。
2. **订阅必须带 instId**：缺 / 空 / 字段名打错（`InstId`）的 `instId` 一律按坏 arg 处理（记 warning
   后跳过）—— `WsRegistry` 把 `(channel, None)` 当"该频道不分合约"的通配订阅，落了它会静默开始
   推全市场行情（客户端没点过任何合约），且回执还会悄悄省掉 `instId`，属最坏的一类静默错误。
3. **逐 arg 回执**：一次请求带多条 `args` 时，OKX 惯例是一条 arg 回一帧
   `{"event":"subscribe","arg":{...}}`；单 arg（引擎的唯一用法）返回单个 `dict`，见 `base.Reply`。

**订阅后补首帧快照**（不在本模块实现，落点是 `OkxAdapter.ws_public_handler` + `_push_snapshots`）：
只靠"下一次盘口变化"驱动推送，会让重连/重启的引擎在盘口早已有挂单时永远等不到行情。补的这一帧
走 `BboBroadcaster.push` → 空盘口自然被语义 1 吞掉，故"初始空盘口无推送"依旧成立。
"""

import logging
import time
from collections.abc import Callable

from aiohttp import web

from mockex.core.book import OrderBook
from mockex.core.events import Callback, EventBus
from mockex.exchanges.base import Reply
from mockex.exchanges.okx.codec import bbo_frame
from mockex.protocol.ws import WsRegistry

__all__ = ["BBO_CHANNEL", "BboBroadcaster", "public_handler"]

# 引擎唯一订阅的公共频道（内部名 depth1 → OKX 名的映射在引擎里硬编码，契约同此拼写）
BBO_CHANNEL = "bbo-tbt"

logger = logging.getLogger(__name__)


def _now_ms() -> int:
    """当前毫秒 epoch（调用方未注入时钟时的兜底）。"""
    return int(time.time() * 1000)


def public_handler(payload: dict, ws: web.WebSocketResponse, registry: WsRegistry) -> Reply:
    """处理公共频道报文（已解析的 JSON 对象）：登记订阅表并返回回执帧。

    入参是会话层 `json.loads` 的产物（`protocol/ws.py` 只投递 JSON 对象）；本函数是同步的，
    **报文内容本身不会让它抛异常** —— 解析不出的字段记 warning 后跳过（见 `base` 模块文档）。
    会话表侧的拒绝（例如连接未登记）是装配写错，照旧响亮抛出，由 `make_json_handler` 记进日志。

    Args:
        payload: 订阅报文，如 `{"op":"subscribe","args":[{"channel":"bbo-tbt","instId":"BTC-USDT"}]}`。
        ws: 发起请求的连接（订阅表的键）。
        registry: 会话表。

    Returns:
        Reply：单 arg → 单帧 `{"event":op,"arg":{...}}`；多 arg → 逐 arg 一帧的列表；
        未知 `op`、无有效 arg 或 `args` 不合法 → None。
    """
    op = payload.get("op")
    if op not in ("subscribe", "unsubscribe"):
        logger.warning("忽略不支持的公共频道 op: %r", op)
        return None
    args = payload.get("args")
    if not isinstance(args, list) or not args:
        logger.warning("忽略无有效 args 的公共频道报文: %r", payload)
        return None

    replies: list[dict] = []
    for arg in args:
        parsed = _parse_arg(arg)
        if parsed is None:
            continue  # 坏 arg 单独跳过：同一报文里的好 arg 照常生效
        channel, inst_id = parsed
        if op == "subscribe":
            registry.subscribe(ws, channel, inst_id)
        else:
            registry.unsubscribe(ws, channel, inst_id)
        replies.append(_ack(op, channel, inst_id))
    if not replies:
        return None
    return replies[0] if len(replies) == 1 else replies


def _parse_arg(arg) -> tuple[str, str] | None:
    """`args[]` 元素 → `(channel, instId)`；解析不了返回 None（调用方跳过）并记 warning。

    `channel` 与 `instId` 都**必填且非空**：公共频道一律按合约订阅，缺 `instId` 会被落成
    `(channel, None)` 的通配订阅（静默收全市场行情），故与缺 `channel` 同等对待。
    """
    channel = arg.get("channel") if isinstance(arg, dict) else None
    inst_id = arg.get("instId") if isinstance(arg, dict) else None
    if not isinstance(channel, str) or not channel:
        logger.warning("忽略非法的公共频道订阅参数: %r", arg)
        return None
    if not isinstance(inst_id, str) or not inst_id:
        logger.warning("忽略缺 instId（或非字符串）的公共频道订阅参数: %r", arg)
        return None
    return channel, inst_id


def _ack(op: str, channel: str, inst_id: str) -> dict:
    """订阅 / 退订回执帧（契约逐字：`{"event":op,"arg":{"channel":...,"instId":...}}`）。"""
    return {"event": op, "arg": {"channel": channel, "instId": inst_id}}


class BboBroadcaster:
    """订单簿变化 → `bbo-tbt` 推送帧（内核事件与协议帧之间唯一的桥）。

    装配（T10）：`BboBroadcaster(bus, book, registry, now_ms=...)`，再逐个 `attach(inst_id)`
    —— 合约清单来自配置 `instruments[]`；一个合约只需 attach 一次，重复 attach 是空操作
    （同一合约挂两次回调，每次变化就会推两帧，引擎双计）。
    """

    def __init__(self, bus: EventBus, book: OrderBook, registry: WsRegistry, *,
                 now_ms: Callable[[], int] | None = None) -> None:
        """Args:
            bus: 内核事件总线（`book.<instId>` topic）。
            book: 订单簿 —— 行情的唯一真相，推送内容只从它派生。
            registry: 会话表（广播出口）。
            now_ms: 时钟（毫秒 epoch 取值函数）；缺省取系统时间，测试注入固定值。
        """
        self._bus = bus
        self._book = book
        self._registry = registry
        self._now_ms = _now_ms if now_ms is None else now_ms
        self._seq: dict[str, int] = {}
        self._callbacks: dict[str, Callback] = {}

    def attach(self, inst_id: str) -> bool:
        """开始监听 `book.<inst_id>`；返回是否新登记（已监听的合约幂等返回 False）。

        Raises:
            ValueError: `inst_id` 为空 —— 空 topic 永远不会被 `publish` 命中，属装配写错。
        """
        if not inst_id:
            raise ValueError("inst_id 不能为空")
        if inst_id in self._callbacks:
            return False
        callback = self._make_callback(inst_id)
        self._callbacks[inst_id] = callback
        self._bus.subscribe(f"book.{inst_id}", callback)
        return True

    def stop(self) -> None:
        """退订全部合约（关停与测试清理用）；调用后内核事件不再进桥。"""
        for inst_id, callback in self._callbacks.items():
            self._bus.unsubscribe(f"book.{inst_id}", callback)
        self._callbacks.clear()

    def push(self, inst_id: str) -> dict | None:
        """按订单簿当前最优档推一帧；空盘口返回 None 且一条帧都不发（语义 1）。

        Returns:
            dict | None：实际排入广播的帧（`codec.bbo_frame` 的产物）；空盘口 = None。
        """
        bid, ask = self._book.best(inst_id)
        if bid is None and ask is None:
            return None
        seq = self._seq.get(inst_id, 0) + 1
        self._seq[inst_id] = seq
        frame = bbo_frame(inst_id, bid, ask, self._now_ms(), seq)
        self._registry.broadcast(BBO_CHANNEL, inst_id, frame)
        return frame

    def _make_callback(self, inst_id: str) -> Callback:
        """给某合约造一个总线回调：payload 不看（盘口直接从订单簿读），只当"该刷新了"的信号。"""

        def on_book_change(_payload=None) -> None:
            self.push(inst_id)

        return on_book_change
