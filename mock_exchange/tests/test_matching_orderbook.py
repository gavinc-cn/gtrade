"""orderbook 撮合测试（M2）：进簿挂单 / 部分成交 / 多档逐档吃 / 价格不交叉 / 撤单。

逐条对照 `doc_ai/spec/mock_exchange/模拟交易所.md` 的「下单与成交语义」表（orderbook 列）：

| 语义 | 本文件对应断言 |
|---|---|
| 进订单簿 | 空盘口下单零成交、`best()` 出现该档、`state == live` |
| 严格按对手量撮合 | 对手只有 0.01 时买 0.1 只成 0.01，余 0.09 挂单（`partially_filled`） |
| 成交价 = 对手挂单价 | 买 60002 吃卖一 60001 → 成交价 60001（不是更高的委托价） |
| 多档逐档吃（价格优先） | 卖一 60001 / 卖二 60002 各一笔成交，余量挂 60002 |
| 卖单吃买盘 | 卖单吃买一，成交价取买一价，买卖两侧对称 |
| 余量挂单构成盘口 | 部分成交后剩余量出现在本侧最优档 |
| 撤单可命中 | 挂单后可撤（`cancel` True），全成/撤过即 False |

两处刻意的简化（mock 不是撮合精度仿真器）：

1. 同一价位跨多张挂单时**聚合成一笔**成交，`Fill.maker_owner` 取该档 FIFO 队首 —— mock 是
   单账户（引擎委托 owner 恒为配置账户），对手方几乎只有注入方 `mock_mm`，聚合不影响记账；
2. 不做自成交保护（STP）：同一账户两侧照常各记一笔，净额自然抵消。
"""

from mockex.core.book import OrderBook
from mockex.core.matching import FILL_MODE_IMMEDIATE, FILL_MODE_ORDERBOOK, MatchingEngine
from mockex.core.models import (
    SIDE_BUY,
    SIDE_SELL,
    STATE_CANCELED,
    STATE_FILLED,
    STATE_LIVE,
    STATE_PARTIALLY_FILLED,
    Order,
)

INST = "BTC-USDT"
TS = 1_700_000_000_123
OWNER = "mock-okx-key"  # 引擎账户（api_key）；注入方是 "mock_mm"


def make_order(cl_ord_id="E1", side=SIDE_BUY, px=60000.0, sz=0.1, inst_id=INST, owner=OWNER, **overrides):
    """引擎通过协议面下的限价单（orderbook 下按对手量成交、余量挂簿）。"""
    fields = dict(cl_ord_id=cl_ord_id, ord_id=cl_ord_id, inst_id=inst_id, side=side, px=px, sz=sz,
                  owner=owner, c_time=TS, u_time=TS)
    fields.update(overrides)
    return Order(**fields)


def make_engine(book, fill_mode=FILL_MODE_ORDERBOOK):
    """时钟注入固定毫秒 —— 成交时间可精确断言（默认走真实时钟）。"""
    return MatchingEngine(book, fill_mode, clock=lambda: TS)


def rest(book, cl_ord_id, side, px, sz, owner="mock_mm"):
    """往订单簿挂一张对手单（注入行情 / `/admin/order` 的等价动作），返回该挂单对象。"""
    maker = make_order(cl_ord_id, side, px, sz, owner=owner)
    book.place(maker)
    return maker


# ── 空盘口：整单挂进本侧 ─────────────────────────────────────────────────────

def test_empty_book_rests_the_order_as_best_bid():
    book = OrderBook()
    order = make_order("E1", SIDE_BUY, 60000.0, 0.1)
    fills = make_engine(book).place(order)

    assert fills == []                          # 无对手：零成交
    assert order.state == STATE_LIVE            # 挂在簿上等对手
    assert (order.acc_fill_sz, order.filled_amount) == (0.0, 0.0)
    assert order.u_time == TS
    assert book.best(INST) == ((60000.0, 0.1), None)
    assert book.cancel("E1") is True             # 真在簿上：撤得掉（immediate 撤不到）


# ── 价格交叉：吃对手价内挂单，成交价取对手价 ─────────────────────────────────

def test_crossing_buy_fills_at_counter_price_and_leaves_no_remainder():
    book = OrderBook()
    rest(book, "MM1", SIDE_SELL, 60001.0, 0.5)
    order = make_order("E1", SIDE_BUY, 60002.0, 0.1)   # 买价高于卖一 → 吃
    fills = make_engine(book).place(order)

    assert [(f.px, f.sz, f.maker_owner, f.taker_owner, f.side) for f in fills] == [
        (60001.0, 0.1, "mock_mm", OWNER, SIDE_BUY)]
    assert order.state == STATE_FILLED
    assert order.acc_fill_sz == 0.1
    assert order.filled_amount == 6000.1               # 60001 × 0.1：按成交价累计，不是委托价
    # 只吃掉对手 0.1：卖一剩 0.4，全成的主动单不留挂单
    assert book.best(INST) == (None, (60001.0, 0.4))
    assert book.cancel("E1") is False


def test_partial_fill_rests_the_remainder_as_best_bid():
    book = OrderBook()
    rest(book, "MM1", SIDE_SELL, 60001.0, 0.01)
    order = make_order("E1", SIDE_BUY, 60001.0, 0.1)   # 对手量不够：只成 0.01
    fills = make_engine(book).place(order)

    assert [(f.px, f.sz) for f in fills] == [(60001.0, 0.01)]
    assert order.state == STATE_PARTIALLY_FILLED
    assert order.acc_fill_sz == 0.01
    assert order.filled_amount == 600.01
    assert book.best(INST) == ((60001.0, 0.09), None)  # 余量挂回本侧，成为买一


def test_sweeps_two_levels_in_price_order_then_rests_the_remainder():
    book = OrderBook()
    rest(book, "MM1", SIDE_SELL, 60001.0, 0.01)
    rest(book, "MM2", SIDE_SELL, 60002.0, 0.02)
    order = make_order("E1", SIDE_BUY, 60002.0, 0.05)
    fills = make_engine(book).place(order)

    assert [(f.px, f.sz) for f in fills] == [(60001.0, 0.01), (60002.0, 0.02)]  # 价格优先
    assert order.state == STATE_PARTIALLY_FILLED
    assert order.acc_fill_sz == 0.03
    assert order.filled_amount == 1800.05              # 600.01 + 1200.04
    assert book.best(INST) == ((60002.0, 0.02), None)  # 卖侧被吃空，余 0.02 成买一


def test_price_not_crossed_rests_instead_of_trading():
    book = OrderBook()
    rest(book, "MM1", SIDE_SELL, 60001.0, 0.5)
    order = make_order("E1", SIDE_BUY, 60000.0, 0.1)   # 买价低于卖一：不交叉、不成交
    fills = make_engine(book).place(order)

    assert fills == []
    assert order.state == STATE_LIVE
    assert book.best(INST) == ((60000.0, 0.1), (60001.0, 0.5))  # 两侧挂单都在


def test_sell_order_takes_the_bid_side():
    book = OrderBook()
    rest(book, "MM1", SIDE_BUY, 60000.0, 0.05)
    order = make_order("E1", SIDE_SELL, 60000.0, 0.05)
    fills = make_engine(book).place(order)

    assert [(f.px, f.sz, f.side) for f in fills] == [(60000.0, 0.05, SIDE_SELL)]
    assert order.state == STATE_FILLED
    assert book.best(INST) == (None, None)
    assert book.cancel("E1") is False


def test_same_price_level_consumes_fifo_head_first():
    """同价两张挂单：按 FIFO 吃队首，聚合成一笔成交并把 maker 记在队首 owner 上。"""
    book = OrderBook()
    first = rest(book, "M1", SIDE_SELL, 60001.0, 0.01, owner="maker-1")
    second = rest(book, "M2", SIDE_SELL, 60001.0, 0.01, owner="maker-2")
    order = make_order("E1", SIDE_BUY, 60001.0, 0.015)
    fills = make_engine(book).place(order)

    assert [(f.px, f.sz, f.maker_owner) for f in fills] == [(60001.0, 0.015, "maker-1")]
    assert (first.state, first.acc_fill_sz) == (STATE_FILLED, 0.01)
    assert (second.state, second.acc_fill_sz) == (STATE_PARTIALLY_FILLED, 0.005)
    assert book.best(INST) == (None, (60001.0, 0.005))


# ── 撤单与模式标志 ───────────────────────────────────────────────────────────

def test_resting_order_can_be_canceled_and_level_disappears():
    book = OrderBook()
    engine = make_engine(book)
    order = make_order("E1", SIDE_BUY, 60000.0, 0.1)
    engine.place(order)

    assert engine.cancel("E1") is True
    assert order.state == STATE_CANCELED
    assert book.best(INST) == (None, None)
    assert engine.cancel("E1") is False           # 撤过即未命中


def test_partially_filled_order_cancels_only_the_remainder():
    book = OrderBook()
    rest(book, "MM1", SIDE_SELL, 60001.0, 0.01)
    engine = make_engine(book)
    order = make_order("E1", SIDE_BUY, 60001.0, 0.1)
    engine.place(order)

    assert engine.cancel("E1") is True
    assert order.state == STATE_CANCELED
    assert order.acc_fill_sz == 0.01              # 已成部分照旧
    assert book.best(INST) == (None, None)


def test_uses_book_flag_marks_the_mode():
    """协议层靠它决定下单后要不要发 book 事件（immediate 不动簿）。"""
    assert make_engine(OrderBook(), FILL_MODE_ORDERBOOK).uses_book is True
    assert make_engine(OrderBook(), FILL_MODE_IMMEDIATE).uses_book is False
    assert MatchingEngine(OrderBook(), FILL_MODE_ORDERBOOK).fill_mode == FILL_MODE_ORDERBOOK
