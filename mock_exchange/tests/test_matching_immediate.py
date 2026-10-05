"""immediate 撮合测试（TDD：先红后绿）

逐条对照 `doc_ai/spec/mock_exchange/模拟交易所.md` 的「下单与成交语义」表：

| 语义 | 本文件对应断言 |
|---|---|
| 不进订单簿 | `best()` 在成交后仍为空（订单不占盘口） |
| 立即全量成交、不校验对手量 | 对手档只有 0.01 时买 0.1 也全成（`acc_fill_sz == sz`） |
| 成交价 = 订单自身价格 | 空盘口、有对手、与盘口不交叉时都按订单价成交 |
| 订单簿完全不受影响 | 簿上有挂单时成交，档位量、挂单状态原样不动（2026-09-25 修订：不吃簿） |

`orderbook` 模式（严格撮合/部分成交/余量挂单）的用例在同目录 `test_matching_orderbook.py`；
本文件只保留"模式切换后 immediate 语义不变"的那条护栏。
"""

import pytest

from mockex.core.book import OrderBook
from mockex.core.matching import FILL_MODE_IMMEDIATE, FILL_MODE_ORDERBOOK, MatchingEngine
from mockex.core.models import (
    SIDE_BUY,
    SIDE_SELL,
    STATE_FILLED,
    STATE_LIVE,
    Fill,
    Order,
)

TS = 1_700_000_000_123
OWNER = "mock-okx-key"  # 引擎账户（api_key）；注入方是 "mock_mm"


def make_order(cl_ord_id="E1", side=SIDE_BUY, px=60000.0, sz=0.1, inst_id="BTC-USDT",
               owner=OWNER, **overrides):
    """引擎通过协议面下的限价单（immediate 下必成交、不进簿）。"""
    fields = dict(cl_ord_id=cl_ord_id, ord_id=cl_ord_id, inst_id=inst_id, side=side, px=px, sz=sz,
                  owner=owner, c_time=TS, u_time=TS)
    fields.update(overrides)
    return Order(**fields)


def make_engine(book, fill_mode=FILL_MODE_IMMEDIATE):
    """时钟注入固定毫秒 —— 成交时间可精确断言（默认走真实时钟）。"""
    return MatchingEngine(book, fill_mode, clock=lambda: TS)


def rest(book, cl_ord_id, side, px, sz, owner="mock_mm"):
    """往订单簿挂一张对手单（注入行情 / `/admin/order` 的等价动作），返回该挂单对象。"""
    maker = make_order(cl_ord_id, side, px, sz, owner=owner)
    book.place(maker)
    return maker


# ── 空盘口：用订单自身价格全量成交，且不进簿 ─────────────────────────────────

def test_empty_book_fills_at_own_price_and_does_not_rest():
    book = OrderBook()
    order = make_order("E1", SIDE_BUY, 60000.0, 0.1)
    fills = make_engine(book).place(order)

    assert len(fills) == 1
    fill = fills[0]
    assert (fill.ord_id, fill.inst_id, fill.px, fill.sz, fill.ts) == ("E1", "BTC-USDT", 60000.0, 0.1, TS)
    assert (order.acc_fill_sz, order.filled_amount, order.state) == (0.1, 6000.0, STATE_FILLED)
    assert order.u_time == TS
    # 关键区别：订单不进订单簿 —— 盘口仍然空（引擎的单不占盘口）
    assert book.best("BTC-USDT") == (None, None)


def test_empty_book_sell_fills_at_own_price():
    book = OrderBook()
    order = make_order("E2", SIDE_SELL, 60001.0, 0.2)
    fills = make_engine(book).place(order)
    assert (fills[0].px, fills[0].sz, fills[0].side) == (60001.0, 0.2, SIDE_SELL)
    assert book.best("BTC-USDT") == (None, None)


# ── 成交字段（T4 结算与 T7 推送的输入） ──────────────────────────────────────

def test_fill_carries_side_and_no_maker():
    """`Fill.side` 是账户结算判资金方向的唯一依据（T2 复审点名），漏填会静默出错；
    maker_owner 恒为空串 —— 隐含对手不是簿上任何真实账户，结算侧据此跳过对手账户。"""
    book = OrderBook()
    rest(book, "A1", SIDE_SELL, 60001.0, 1.0, owner="mock_mm")
    order = make_order("E1", SIDE_BUY, 60001.0, 0.1, owner=OWNER)

    fill = make_engine(book).place(order)[0]

    assert isinstance(fill, Fill)
    assert fill.side == SIDE_BUY
    assert (fill.taker_owner, fill.maker_owner) == (OWNER, "")
    assert (fill.px, fill.sz, fill.ts) == (60001.0, 0.1, TS)


# ── 有对手：按订单价全成，但订单簿原样不动 ──────────────────────────────────

def test_resting_orders_are_untouched_by_fill():
    """spec 对照例（2026-09-25 语义修订）：盘口卖一 60001×0.01，引擎买 60001×0.1 →
    0.1 全成（成交价=订单价 60001），卖一档原样不动 —— 流动性是隐含新对手单，不吃簿。"""
    book = OrderBook()
    maker = rest(book, "A1", SIDE_SELL, 60001.0, 0.01)
    order = make_order("E1", SIDE_BUY, 60001.0, 0.1)

    fill = make_engine(book).place(order)[0]

    assert (fill.px, fill.sz) == (60001.0, 0.1)  # 不校验对手量：全成
    assert order.acc_fill_sz == 0.1
    assert book.best("BTC-USDT") == (None, (60001.0, 0.01))  # 档位量不变
    assert (maker.acc_fill_sz, maker.state) == (0.0, "live")  # 挂单未被吃


def test_no_book_level_is_touched():
    """两侧、深档全部原样：盘口是 bbo 推送源（T6），不动盘口即无行情变化。"""
    book = OrderBook()
    bid = rest(book, "B1", SIDE_BUY, 60000.0, 1.0)
    ask_best = rest(book, "A1", SIDE_SELL, 60001.0, 0.5)
    ask_deep = rest(book, "A2", SIDE_SELL, 60002.0, 5.0)

    make_engine(book).place(make_order("E1", SIDE_BUY, 60002.0, 1.0))

    assert book.best("BTC-USDT") == ((60000.0, 1.0), (60001.0, 0.5))
    assert all(o.state == "live" and o.acc_fill_sz == 0.0 for o in (bid, ask_best, ask_deep))


def test_buy_fills_at_own_price_even_when_not_crossing():
    """immediate = "单必成"：成交价恒为订单自身价格，即便不与盘口交叉也全成。

    注入行情造的盘口常与策略挂单价不重合，按订单价成交才能让策略拿到与挂单一致的
    成交价；订单簿不受影响（2026-09-25 修订）。
    """
    book = OrderBook()
    rest(book, "A1", SIDE_SELL, 60001.0, 1.0)
    order = make_order("E1", SIDE_BUY, 60000.0, 0.1)
    assert make_engine(book).place(order)[0].px == 60000.0
    assert book.best("BTC-USDT") == (None, (60001.0, 1.0))


def test_sell_fills_at_own_price_with_bid_present():
    book = OrderBook()
    rest(book, "B1", SIDE_BUY, 60000.0, 1.0)
    order = make_order("E1", SIDE_SELL, 60000.0, 0.4)
    fill = make_engine(book).place(order)[0]
    assert (fill.px, fill.sz, fill.side) == (60000.0, 0.4, SIDE_SELL)
    assert book.best("BTC-USDT") == ((60000.0, 1.0), None)


def test_filled_amount_has_no_float_tail():
    """累计成交额走 Decimal：60001.2 × 0.3 直接 float 乘加得到 18000.359999999997。"""
    order = make_order("E1", SIDE_BUY, 60001.2, 0.3)
    make_engine(OrderBook()).place(order)
    assert order.filled_amount == 18000.36
    assert order.acc_fill_sz == 0.3


def test_place_accumulates_on_repeat_calls():
    """同一张委托被再次送撮合时是**累加**，不是覆盖（M2 分次成交的前置条件）。"""
    order = make_order("E1", SIDE_BUY, 60000.0, 0.1, acc_fill_sz=0.4, filled_amount=24000.0)
    make_engine(OrderBook()).place(order)
    assert order.acc_fill_sz == 0.5
    assert order.filled_amount == 30000.0


# ── cancel ──────────────────────────────────────────────────────────────────

def test_cancel_delegates_to_book():
    book = OrderBook()
    rest(book, "A1", SIDE_BUY, 60000.0, 1.0)
    engine = make_engine(book)
    assert engine.cancel("A1") is True
    assert engine.cancel("A1") is False
    assert book.best("BTC-USDT") == (None, None)


def test_cancel_of_immediate_order_is_a_miss():
    """immediate 下单后无挂单可撤 —— 引擎的单从没进过簿（T7 据此回撤单失败）。"""
    book = OrderBook()
    engine = make_engine(book)
    engine.place(make_order("E1", SIDE_BUY, 60000.0, 0.1))
    assert engine.cancel("E1") is False


# ── 模式边界（M2 起 orderbook 有实现，用例见 test_matching_orderbook.py）────────

def test_orderbook_mode_is_accepted_and_rests_instead_of_filling():
    """护栏：切到 orderbook 不再拒绝，且空盘口下**不**按 immediate 语义伪造成交。"""
    engine = make_engine(OrderBook(), FILL_MODE_ORDERBOOK)
    order = make_order("E1", SIDE_BUY, 60000.0, 0.1)

    assert engine.place(order) == []            # 无对手：零成交
    assert order.state == STATE_LIVE            # 进簿挂单（immediate 会标 filled）
    assert engine.uses_book is True
    assert make_engine(OrderBook()).uses_book is False


def test_market_order_raises_not_implemented():
    """引擎只用限价单，市价单不必实现 —— 但也不能静默乱成交。"""
    engine = make_engine(OrderBook())
    with pytest.raises(NotImplementedError):
        engine.place(make_order("E1", SIDE_BUY, 60000.0, 0.1, ord_type="market"))


def test_unknown_fill_mode_rejected_at_construction():
    """配置写错（"immediate " 多一个空格）要在启动时就炸，而不是静默按默认行为跑。"""
    with pytest.raises(ValueError):
        MatchingEngine(OrderBook(), "immediate ")
    assert make_engine(OrderBook()).fill_mode == FILL_MODE_IMMEDIATE


def test_zero_size_order_rejected():
    with pytest.raises(ValueError):
        make_engine(OrderBook()).place(make_order("E1", SIDE_BUY, 60000.0, 0.0))


def test_non_positive_price_rejected():
    """px=0 的限价单若放行，空盘口下会成交在 0：累计成交额为 0，而 codec 的 avgPx
    会在远处抛"有量无额"，错在源头就该报。"""
    with pytest.raises(ValueError):
        make_engine(OrderBook()).place(make_order("E1", SIDE_BUY, 0.0, 0.1))


def test_unknown_side_rejected():
    """方向写错（"Long"/"Buy"）不能静默按买处理 —— 结算方向会反过来。"""
    with pytest.raises(ValueError):
        make_engine(OrderBook()).place(make_order("E1", "Buy", 60000.0, 0.1))
