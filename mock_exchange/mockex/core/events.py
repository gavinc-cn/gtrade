"""事件总线：内核状态变化 → 订阅者（同步回调，本任务不碰 asyncio）。

**topic 约定**（T5/T6/T7 按此订阅，命名必须一致）：

| topic | 含义 |
|---|---|
| `book.<instId>` | 订单簿变化（bbo 可能变） |
| `orders.<account>` | 某账户的订单状态变化 |
| `account.<account>` | 账户余额/持仓变化 |

三条刻意的取舍：

1. **回调是同步函数**：调用方（aiohttp handler / 撮合路径）在自己的上下文里直接调用，
   需要异步就在回调里自行上 `asyncio` 任务 —— 这里不引事件循环，也不做成协程；
2. **订阅者异常直接抛出**（不吞、不吞后 log）：桥接层的 bug 必须当场可见，"静默丢帧"
   比抛异常难查一个数量级；单个 WS 连接发送失败由桥接方自己兜住；
3. **`publish` 遍历订阅者快照**：回调里退订/新增订阅都是常态（连接断开即退订），不得踩
   `RuntimeError: list changed size during iteration`；本轮快照内的订阅者一定收到，
   回调里新增的订阅者从**下一轮**开始生效。

不做线程安全（mock 是单进程 asyncio，全在一个事件循环里）；不保存事件序列
（`/admin/events` 的环形缓冲归 T9，与订阅表无关）。
"""

from typing import Any, Callable

__all__ = ["EventBus"]

# 回调签名：`cb(payload)`；payload 由发布方自定（内核对象或已编码报文均可）
Callback = Callable[[Any], None]


class EventBus:
    """topic → 订阅者列表的极简发布/订阅（一个进程一个实例，由 T10 装配）。"""

    def __init__(self) -> None:
        self._subs: dict[str, list[Callback]] = {}

    def subscribe(self, topic: str, cb: Callback) -> None:
        """登记一个订阅（幂等：同一 topic 下同一回调只登记一次）。

        重复登记会双推帧（引擎收到重复 bbo / 重复 orders），故此处去重；`in` 走相等比较，
        绑定的方法每次取都是新对象但相等，同样只登记一次。

        Args:
            topic: `book.<instId>` / `orders.<account>` / `account.<account>`。
            cb: 同步回调 `cb(payload)`；带 `__call__` 的对象（如记录器）亦可。

        Raises:
            ValueError: topic 为空 —— 空 topic 永远不会被命中，属写错，当场报。
            TypeError: cb 不可调用（传成了调用的结果之类）。
        """
        if not topic:
            raise ValueError("topic 不能为空")
        if not callable(cb):
            raise TypeError(f"订阅回调必须可调用: {cb!r}")
        subs = self._subs.setdefault(topic, [])
        if cb not in subs:
            subs.append(cb)

    def unsubscribe(self, topic: str, cb: Callback) -> bool:
        """退订，返回是否确有登记被移除。

        没登记过（含 topic 从未出现过）返回 False 且不报错 —— 退订属清理动作，要幂等。
        """
        subs = self._subs.get(topic)
        if not subs:
            return False
        remained = [item for item in subs if item != cb]
        self._subs[topic] = remained
        return len(remained) != len(subs)

    def publish(self, topic: str, payload) -> int:
        """把 payload 同步派发给该 topic 的全部订阅者，返回本轮实际调用次数。

        Args:
            topic: 同 `subscribe`。
            payload: 原样传给回调（不拷贝、不编码）。

        Returns:
            int：本轮调用的回调数（该 topic 无订阅者 = 0，即静默无操作）。

        Raises:
            ValueError: topic 为空。
            Exception: 订阅者抛出的异常原样透传（见模块文档取舍 2）。
        """
        if not topic:
            raise ValueError("topic 不能为空")
        calls = 0
        for cb in list(self._subs.get(topic, ())):  # 快照：回调内可安全改订阅表
            cb(payload)
            calls += 1
        return calls
