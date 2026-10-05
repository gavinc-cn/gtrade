"""事件总线测试（TDD：先红后绿）

topic 约定（T5/T6/T7 按此订阅，命名必须一致）：
`book.<instId>` 订单簿变化 / `orders.<account>` 某账户订单状态 / `account.<account>` 账户余额持仓。
"""

import pytest

from mockex.core.events import EventBus

BOOK = "book.BTC-USDT"
ORDERS = "orders.mock-okx-key"
ACCOUNT = "account.mock-okx-key"


class Recorder:
    """订阅回调：记下收到的 payload（同时证明"同步函数"这一约定成立）。"""

    def __init__(self, name=""):
        self.name = name
        self.calls = []

    def __call__(self, payload):
        self.calls.append(payload)


def test_subscriber_receives_payload_object_unchanged():
    bus = EventBus()
    got = Recorder()
    bus.subscribe(BOOK, got)
    payload = {"ts": 1, "asks": [["60001", "1"]]}

    assert bus.publish(BOOK, payload) == 1
    assert got.calls[0] is payload  # 原样传递：不拷贝、不编码


def test_publish_to_nobody_is_a_noop():
    assert EventBus().publish(ACCOUNT, {}) == 0


def test_unsubscribe_stops_delivery():
    bus = EventBus()
    got = Recorder()
    bus.subscribe(ORDERS, got)

    assert bus.unsubscribe(ORDERS, got) is True
    bus.publish(ORDERS, "x")
    assert got.calls == []
    assert bus.unsubscribe(ORDERS, got) is False  # 未登记过：静默失败，不报错


def test_topic_match_is_exact_not_prefix():
    """`book.BTC-USDT` 与 `book.BTC-USDT-SWAP` 是两条 topic，两个方向都不得串台。"""
    bus = EventBus()
    short, long = Recorder(), Recorder()
    bus.subscribe(BOOK, short)
    bus.subscribe("book.BTC-USDT-SWAP", long)

    bus.publish("book.BTC-USDT-SWAP", 1)  # 长名推送：短名订阅者不该收到
    assert (short.calls, long.calls) == ([], [1])

    bus.publish(BOOK, 2)                  # 短名推送：长名订阅者不该收到（前缀实现会串）
    assert (short.calls, long.calls) == ([2], [1])


def test_all_subscribers_of_topic_receive_in_registration_order():
    bus = EventBus()
    first, second = Recorder("first"), Recorder("second")
    bus.subscribe(ACCOUNT, first)
    bus.subscribe(ACCOUNT, second)

    assert bus.publish(ACCOUNT, "snap") == 2
    assert (first.calls, second.calls) == (["snap"], ["snap"])


def test_duplicate_subscribe_delivers_once():
    """重复登记同一回调会双推帧（引擎收到重复 bbo / orders）——订阅幂等。"""
    bus = EventBus()
    got = Recorder()
    bus.subscribe(BOOK, got)
    bus.subscribe(BOOK, got)
    assert bus.publish(BOOK, 1) == 1


def test_topics_are_isolated():
    bus = EventBus()
    book, order = Recorder(), Recorder()
    bus.subscribe(BOOK, book)
    bus.subscribe(ORDERS, order)
    bus.publish(ORDERS, "o")
    assert (book.calls, order.calls) == ([], ["o"])


def test_unsubscribe_inside_callback_does_not_break_publish():
    """回调里退订是常态（连接断开即退订）；本轮已快照的订阅者仍收到这一次。"""
    bus = EventBus()
    once, twice = Recorder(), Recorder()

    def first(payload):
        once(payload)
        bus.unsubscribe(BOOK, twice)

    bus.subscribe(BOOK, first)
    bus.subscribe(BOOK, twice)

    assert bus.publish(BOOK, 1) == 2
    assert (once.calls, twice.calls) == ([1], [1])
    bus.publish(BOOK, 2)
    assert (once.calls, twice.calls) == ([1, 2], [1])


def test_subscribe_inside_callback_takes_effect_next_publish():
    """publish 遍历订阅者快照：回调里新增的订阅者从下一轮开始生效（本轮不掺入）。"""
    bus = EventBus()
    late = Recorder()

    def first(payload):
        bus.subscribe(BOOK, late)  # 同一 topic：活列表实现会当场把它也调用一次

    bus.subscribe(BOOK, first)
    bus.publish(BOOK, 1)
    assert late.calls == []
    assert bus.publish(BOOK, 2) == 2
    assert late.calls == [2]


def test_subscriber_exception_propagates():
    """不做吞异常：桥接层的 bug 必须当场可见（静默丢帧比抛异常难查一个数量级）。"""

    def boom(payload):
        raise RuntimeError("bridge down")

    bus = EventBus()
    bus.subscribe(BOOK, boom)
    with pytest.raises(RuntimeError):
        bus.publish(BOOK, 1)


def test_empty_topic_or_non_callable_rejected():
    """topic 写空 / 回调传成结果值：都会静默永不触发，当场报。"""
    bus = EventBus()
    with pytest.raises(ValueError):
        bus.subscribe("", Recorder())
    with pytest.raises(ValueError):
        bus.publish("", 1)
    with pytest.raises(TypeError):
        bus.subscribe(BOOK, 42)
