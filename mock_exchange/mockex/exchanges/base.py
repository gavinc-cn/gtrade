"""交易所适配器抽象：内核 / WS 会话层与某家交易所协议之间的唯一接缝。

三条职责边界（T3 裁定"内核不耦合协议层"的落点）：

1. adapter 只吐**可直接 `json.dumps` 的帧**，内核（book/matching/account）不认识 OKX 拼写；
   报文编码集中在各家交易所的 `codec.py`；
2. 协议业务在 adapter 内是**同步函数**（`ws_public_handler` / `ws_private_handler`）：入参是
   已解析的 JSON 对象，返回值是"要回给客户端的帧"。发送与异常兜底由 `make_json_handler`
   统一负责 —— 会话层（`protocol/ws.py`）只把原文转发进来，且**不吞异常**；
3. REST 业务以 `(method, path, handler)` 表的形式交给装配方（T10）挂路由。

**回执约定**（本文件是唯一定义处，T6/T7 共享）：

| 返回值 | 含义 |
|---|---|
| `dict` | 单帧回执（引擎只发单 arg 报文，这是常态） |
| `list[dict]` | 多帧 —— 一次请求带多条 `args` 时，OKX 惯例是一条 arg 一帧回执 |
| `None` | 无需回帧（未知 op、解析失败等，handler 自己记 warning） |

**handler 不得抛异常**：会话层不吞异常，抛出去就是连接被静默关闭、引擎反复重连而 mock 无痕
（T5 复审遗留）。解析不了的字段由 handler 记 warning 后跳过；`make_json_handler` 再兜一层，
把漏网的异常记 warning 后忽略 —— "丢一帧"比"断连无痕"好查一个数量级。兜底的这条 warning
**按方向分流**：公共路径记原文，私有路径只记 `op` + 异常摘要（私有报文正文含
`apiKey/passphrase/sign`，spec 硬约束「凭据不落盘」），详见 `make_json_handler`。
"""

import json
import logging
from abc import ABC, abstractmethod
from collections.abc import Awaitable, Callable

from aiohttp import web

from mockex.protocol.ws import WsRegistry

__all__ = ["ExchangeAdapter", "Reply", "RestRoute", "frames_of", "make_json_handler"]

logger = logging.getLogger(__name__)

# REST 路由三元组：`(HTTP 方法, 路径, aiohttp handler)`；T10 逐个 `app.router.add_route`
RestRoute = tuple[str, str, Callable[[web.Request], Awaitable[web.Response]]]
# WS 业务的回执，语义见模块文档的约定表
Reply = dict | list[dict] | None


class ExchangeAdapter(ABC):
    """一家交易所的协议适配器；子类在自己的 `__init__` 里持有内核依赖（订单簿/撮合/账户）。"""

    #: 适配器标识（如 "okx"）：日志与管理面用它区分交易所
    name: str

    @abstractmethod
    def rest_routes(self) -> list[RestRoute]:
        """REST 路由表；尚未实现或不支持的端点返回空表（装配不该因此报错）。"""

    @abstractmethod
    def ws_public_handler(self, payload: dict, ws: web.WebSocketResponse, registry: WsRegistry) -> Reply:
        """公共频道报文（已解析的 JSON 对象）→ 回执帧；**同步**，不得抛异常。"""

    @abstractmethod
    def ws_private_handler(self, payload: dict, ws: web.WebSocketResponse, registry: WsRegistry) -> Reply:
        """私有频道报文 → 回执帧；**同步**，不得抛异常。"""


def make_json_handler(adapter: ExchangeAdapter, registry: WsRegistry, *, private: bool = False):
    """把 adapter 某个方向的 WS 业务包成会话层的 `on_json`（T10 挂路由用）。

    会话层只保证"投递过来的是能解析为 JSON 对象的原文"，本函数负责剩下三件事（T6/T7 各写一份
    必然漂移，故收在这里）：解析原文 → 调对应 handler → 把回执逐帧发出；任何一步出错都只记
    warning 并忽略，**绝不把异常抛回会话层**（抛出去 = 连接静默关闭）。

    **兜底日志按方向分流**（spec 硬约束「凭据不落盘」）：公共路径照记原文（排障价值高，公共
    报文里没有秘密）；私有路径只记 `op` + 异常摘要 —— `login` 帧的正文就是
    `apiKey/passphrase/sign`，整条打进日志即凭据泄漏，而"发送失败/处理炸了"这种兜底恰恰是
    登录帧最可能踩到的路径。

    Args:
        adapter: 提供 `ws_public_handler` / `ws_private_handler` 的适配器。
        registry: 会话表（handler 登记订阅用）。
        private: True 挂私有频道业务，False（缺省）挂公共频道。

    Returns:
        协程函数 `on_json(ws, text)`，直接作为 `protocol.ws.handle_ws(on_json=...)` 注入。
    """
    handler = adapter.ws_private_handler if private else adapter.ws_public_handler
    label = "private" if private else "public"

    async def on_json(ws: web.WebSocketResponse, text: str) -> None:
        try:
            for frame in frames_of(handler(json.loads(text), ws, registry)):
                await ws.send_str(json.dumps(frame, ensure_ascii=False))
        except Exception as exc:  # noqa: BLE001 —— 兜底正是本函数的职责（含发送失败：连接刚断）
            if private:
                # 私有报文正文含凭据，**一个字都不落**：键级脱敏要看键名枚举是否齐全
                # （apikey/API_KEY/嵌套 args 换个写法就漏），不回显正文则从构造上不可能漏。
                # `exc_info` 只展开调用栈与异常摘要，不含报文内容。
                logger.warning("忽略无法处理的 %s WS 报文（op=%r）: %s: %s", label, _op_of(text),
                               type(exc).__name__, exc, exc_info=True)
            else:
                logger.warning("忽略无法处理的 %s WS 报文: %s", label, text, exc_info=True)

    return on_json


def _op_of(text: str) -> object:
    """尽力从报文原文取 `op` 字段（私有路径留痕用）；取不到返回 None。

    只回显 `op`，其余字段一律不动 —— 本函数的存在理由就是"给私有报文留一条不含凭据的痕"。
    解析失败（坏 JSON、深层嵌套触发递归上限…）按取不到处理：诊断辅助函数绝不能把异常
    抛回 `on_json` 的兜底分支。
    """
    try:
        payload = json.loads(text)
    except Exception:  # noqa: BLE001 —— 见 docstring：取不到 op 而已，不值当把兜底二次炸掉
        return None
    return payload.get("op") if isinstance(payload, dict) else None


def frames_of(reply: Reply) -> list[dict]:
    """回执规整成帧列表：`dict` → 单元素列表；`list` → 原样；`None` → 空列表。"""
    if reply is None:
        return []
    return reply if isinstance(reply, list) else [reply]
