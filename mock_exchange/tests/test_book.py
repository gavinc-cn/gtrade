"""订单簿测试：价格优先 + 同价 FIFO（TDD：先红后绿）

订单簿是 mock 的**唯一真相** —— bbo 推送（T6）、REST 查盘口（T8）、`/admin/state`
（T9）都从它派生，所以这里守的是三件事：①最优价按方向取对（买取 max、卖取 min）；
②同价 FIFO 不因撤单/被吃而乱序；③成交消耗对手档位后，档位量必须同步减少、
扣到 0 整档消失（immediate 语义的另一半，见 `test_matching_immediate.py`）。
"""

import json

import pytest

from mockex.core.book import OrderBook
from mockex.core.models import (
    SIDE_BUY,
    SIDE_SELL,
    STATE_CANCELED,
    STATE_FILLED,
    STATE_PARTIALLY_FILLED,
    Order,
)

TS = 1_700_000_000_123


def make_order(cl_ord_id="E1", side=SIDE_BUY, px=60000.0, sz=1.0, inst_id="BTC-USDT",
               owner="mock_mm", **overrides):
    """一张挂单 —— 默认是 mock 自营（注入行情造的盘口）的 BTC-USDT 买一。"""
    fields = dict(cl_ord_id=cl_ord_id, ord_id=cl_ord_id, inst_id=inst_id, side=side, px=px, sz=sz,
                  owner=owner, c_time=TS, u_time=TS)
    fields.update(overrides)
    return Order(**fields)


# ── best：最优一档 ───────────────────────────────────────────────────────────

def test_best_returns_top_bid_and_ask():
    book = OrderBook()
    book.place(make_order("E1", SIDE_BUY, 60000.0, 1.0))
    book.place(make_order("E2", SIDE_SELL, 60001.0, 2.0))
    assert book.best("BTC-USDT") == ((60000.0, 1.0), (60001.0, 2.0))


def test_best_on_empty_book_is_none_pair():
    """空盘口是 M1 的初始状态（spec：初始空盘口、无推送），不能抛异常。"""
    book = OrderBook()
    assert book.best("BTC-USDT") == (None, None)
    one_side = OrderBook()
    one_side.place(make_order("E1", SIDE_BUY, 60000.0, 1.0))
    assert one_side.best("BTC-USDT") == ((60000.0, 1.0), None)


def test_best_picks_price_priority_per_side():
    """买侧取最高价、卖侧取最低价 —— 取反了盘口就反了。"""
    book = OrderBook()
    for cl, side, px in [("B1", SIDE_BUY, 59999.0), ("B2", SIDE_BUY, 60000.0), ("B3", SIDE_BUY, 60002.0),
                         ("A1", SIDE_SELL, 60003.0), ("A2", SIDE_SELL, 60001.0)]:
        book.place(make_order(cl, side, px, 1.0))
    assert book.best("BTC-USDT") == ((60002.0, 1.0), (60001.0, 1.0))


def test_best_is_isolated_per_instrument():
    book = OrderBook()
    book.place(make_order("E1", SIDE_BUY, 60000.0, 1.0))
    book.place(make_order("E2", SIDE_SELL, 3000.0, 5.0, inst_id="ETH-USDT"))
    assert book.best("BTC-USDT") == ((60000.0, 1.0), None)
    assert book.best("ETH-USDT") == (None, (3000.0, 5.0))
    assert book.best("DOGE-USDT") == (None, None)


# ── cancel：撤单 ─────────────────────────────────────────────────────────────

def test_cancel_removes_level_and_reports_hit():
    book = OrderBook()
    book.place(make_order("E1", SIDE_SELL, 60001.0, 1.0))
    assert book.cancel("E1") is True
    # 档位随之消失（单档单挂单）
    assert book.best("BTC-USDT") == (None, None)
    # 重复撤单是未命中，不是异常
    assert book.cancel("E1") is False
    assert book.cancel("不存在") is False


def test_cancel_keeps_other_orders_at_same_price():
    book = OrderBook()
    book.place(make_order("E1", SIDE_SELL, 60001.0, 1.0))
    book.place(make_order("E2", SIDE_SELL, 60001.0, 2.0))
    assert book.cancel("E1") is True
    assert book.best("BTC-USDT") == (None, (60001.0, 2.0))


def test_cancel_marks_order_canceled():
    """撤单是交易所侧的终态 —— T7 的 `state:"canceled"` 推送靠它（u_time 由调用方补）。"""
    order = make_order("E1", SIDE_SELL, 60001.0, 1.0)
    book = OrderBook()
    book.place(order)
    book.cancel("E1")
    assert order.state == STATE_CANCELED


def test_consumed_order_leaves_cancel_index():
    """整档被吃光后 `cancel` / `remove_owner` 必须是"未命中"，不能抛异常。

    回归护栏（Fix round 1）：`consume_best` 吃完挂单若只摘 `_levels` 而不清
    `_by_cl_ord_id`，索引就指向一张不在簿上的单 —— `cancel` 会在档位查找处击穿 KeyError，
    `/admin/depth` 的"先撤同源上一批再挂新的"流程直接 500。
    """
    book = OrderBook()
    book.place(make_order("mm-1", SIDE_SELL, 60001.0, 0.01))
    book.consume_best("BTC-USDT", SIDE_SELL, 0.1, TS)  # 整档被吃光
    assert book.best("BTC-USDT") == (None, None)

    assert book.cancel("mm-1") is False                    # 不在簿上 = 未命中，不是异常
    assert book.remove_owner("mock_mm", "BTC-USDT") == 0    # 注入替换流程不能炸


def test_consumed_order_frees_cl_ord_id_for_reuse():
    """吃完一张挂单后，同一 clOrdId 必须能立刻重挂（**不靠先撤单来清索引**）。

    这条用例刻意不先调 `cancel`：`cancel` 自身会顺手丢掉坏索引条目（自愈），会掩盖
    `consume_best` 漏清索引的问题 —— 只有纯粹 place → 吃光 → place 才能证明吃完那张
    已从 `_by_cl_ord_id` 里消失。
    """
    book = OrderBook()
    book.place(make_order("mm-1", SIDE_SELL, 60001.0, 0.01))
    book.consume_best("BTC-USDT", SIDE_SELL, 0.1, TS)  # 整档被吃光，索引应同步清掉

    book.place(make_order("mm-1", SIDE_SELL, 60001.0, 0.02))  # 重复校验不得误拦
    assert book.best("BTC-USDT") == (None, (60001.0, 0.02))


def test_cancel_does_not_rewrite_consumed_order_state():
    """已 `filled` 的挂单不能被 `cancel` 改写成 `canceled`。

    同价还有别的挂单时档位仍在，若索引里残留着已成交那张，"撤销"会命中索引、档位里却
    找不到它 —— 不摘单却返回 True 并把状态改写：`/admin/state` 与 T7 的 `orders` 推送
    就会把一笔成交报成撤单。
    """
    book = OrderBook()
    first = make_order("A1", SIDE_SELL, 60001.0, 0.01)
    book.place(first)
    book.place(make_order("A2", SIDE_SELL, 60001.0, 1.0))
    book.consume_best("BTC-USDT", SIDE_SELL, 0.01, TS)  # A1 全成、A2 分文未动

    assert book.cancel("A1") is False
    assert first.state == STATE_FILLED                  # 不被改写成 canceled
    assert book.best("BTC-USDT") == (None, (60001.0, 1.0))


# ── consume_best：成交消耗对手档位 ───────────────────────────────────────────

def test_consume_best_returns_counter_price_and_owner():
    book = OrderBook()
    book.place(make_order("A1", SIDE_SELL, 60001.0, 1.0, owner="mock_mm"))
    assert book.consume_best("BTC-USDT", SIDE_SELL, 0.3, TS) == (60001.0, "mock_mm")


def test_consume_best_on_empty_side_returns_none():
    book = OrderBook()
    book.place(make_order("B1", SIDE_BUY, 60000.0, 1.0))
    assert book.consume_best("BTC-USDT", SIDE_SELL, 1.0, TS) == (None, "")


def test_consume_best_reduces_level_size():
    book = OrderBook()
    book.place(make_order("A1", SIDE_SELL, 60001.0, 1.0))
    book.consume_best("BTC-USDT", SIDE_SELL, 0.3, TS)
    assert book.best("BTC-USDT") == (None, (60001.0, 0.7))
    # 档位量取自挂单剩余量（sz - acc_fill_sz），不是独立计数器
    assert book.snapshot()["BTC-USDT"]["asks"] == [(60001.0, 0.7)]


def test_consume_best_caps_at_level_size_and_drops_the_level():
    """immediate 不校验对手量，但**订单簿侧**不能超吃：挂单 sz=0.01 被吃 0.1 时
    只能是"整张成交并移除"，绝不能出现 acc_fill_sz(0.1) > sz(0.01) 的坏挂单。"""
    maker = make_order("A1", SIDE_SELL, 60001.0, 0.01)
    book = OrderBook()
    book.place(maker)
    assert book.consume_best("BTC-USDT", SIDE_SELL, 0.1, TS) == (60001.0, "mock_mm")
    assert book.best("BTC-USDT") == (None, None)
    assert (maker.acc_fill_sz, maker.state) == (0.01, STATE_FILLED)
    assert maker.u_time == TS


def test_consume_best_leaves_deeper_levels_untouched():
    """immediate 成交价只有一个（= 订单自身价格），故不跨档吃 —— 深档原样留着。"""
    book = OrderBook()
    book.place(make_order("A1", SIDE_SELL, 60001.0, 0.01))
    book.place(make_order("A2", SIDE_SELL, 60002.0, 5.0))
    book.consume_best("BTC-USDT", SIDE_SELL, 0.1, TS)
    assert book.best("BTC-USDT") == (None, (60002.0, 5.0))


def test_same_price_is_fifo():
    """同价按挂单先后吃：先挂的先成交，且 FIFO 顺序不因首单吃完而错位。"""
    first = make_order("A1", SIDE_SELL, 60001.0, 0.5, owner="maker-1")
    second = make_order("A2", SIDE_SELL, 60001.0, 0.5, owner="maker-2")
    book = OrderBook()
    book.place(first)
    book.place(second)

    assert book.consume_best("BTC-USDT", SIDE_SELL, 0.2, TS) == (60001.0, "maker-1")
    assert (first.acc_fill_sz, first.state) == (0.2, STATE_PARTIALLY_FILLED)
    assert (second.acc_fill_sz, second.state) == (0.0, "live")

    # 0.6 > 首单剩余 0.3：吃光首单后继续吃次单
    assert book.consume_best("BTC-USDT", SIDE_SELL, 0.6, TS) == (60001.0, "maker-1")
    assert (first.acc_fill_sz, first.state) == (0.5, STATE_FILLED)
    assert (second.acc_fill_sz, second.state) == (0.3, STATE_PARTIALLY_FILLED)
    assert book.best("BTC-USDT") == (None, (60001.0, 0.2))


def test_consume_best_accumulates_filled_amount_without_float_tail():
    """累计成交额用 Decimal 累加：0.1 这类价量直接 float 乘加会留长尾。"""
    maker = make_order("A1", SIDE_SELL, 60001.2, 0.3)
    book = OrderBook()
    book.place(maker)
    book.consume_best("BTC-USDT", SIDE_SELL, 0.3, TS)
    assert maker.filled_amount == 18000.36  # float 乘加给的 18000.359999999997 是坏值


def test_consume_best_on_buy_side():
    """买单被吃（撤单前的自营买盘被引擎卖出吃掉）走同一路径，方向别写死。"""
    maker = make_order("B1", SIDE_BUY, 60000.0, 1.0)
    book = OrderBook()
    book.place(maker)
    assert book.consume_best("BTC-USDT", SIDE_BUY, 1.0, TS) == (60000.0, "mock_mm")
    assert book.best("BTC-USDT") == (None, None)


# ── depth / snapshot / remove_owner ──────────────────────────────────────────

def test_depth_sorted_and_limited():
    book = OrderBook()
    for cl, side, px in [("B1", SIDE_BUY, 59999.0), ("B2", SIDE_BUY, 60000.0), ("B3", SIDE_BUY, 60002.0),
                         ("A1", SIDE_SELL, 60003.0), ("A2", SIDE_SELL, 60001.0)]:
        book.place(make_order(cl, side, px, 1.0))
    assert book.depth("BTC-USDT", 2) == {"bids": [(60002.0, 1.0), (60000.0, 1.0)],
                                         "asks": [(60001.0, 1.0), (60003.0, 1.0)]}
    # 档位不足时给多少算多少；无挂单一侧给空列表（不是 None）
    assert book.depth("BTC-USDT", 99)["bids"] == [(60002.0, 1.0), (60000.0, 1.0), (59999.0, 1.0)]
    assert book.depth("DOGE-USDT") == {"bids": [], "asks": []}


def test_snapshot_is_json_ready_and_covers_all_instruments():
    book = OrderBook()
    book.place(make_order("E1", SIDE_BUY, 60000.0, 1.0))
    book.place(make_order("E2", SIDE_SELL, 3000.0, 2.0, inst_id="ETH-USDT"))
    snapshot = book.snapshot()
    assert list(snapshot) == ["BTC-USDT", "ETH-USDT"]
    assert snapshot["ETH-USDT"] == {"bids": [], "asks": [(3000.0, 2.0)]}
    # `/admin/state` 直接 json.dumps 它 —— 不能有 tuple 之外的怪类型（Decimal 等）
    assert json.loads(json.dumps(snapshot))["BTC-USDT"]["bids"] == [[60000.0, 1.0]]


def test_remove_owner_is_scoped_by_owner_and_instrument():
    """`/admin/depth` 替换语义靠它：只撤同 owner 同 instId 的上一批。"""
    book = OrderBook()
    book.place(make_order("M1", SIDE_BUY, 60000.0, 1.0, owner="mock_mm"))
    book.place(make_order("M2", SIDE_SELL, 60001.0, 1.0, owner="mock_mm"))
    book.place(make_order("M3", SIDE_BUY, 3000.0, 1.0, owner="mock_mm", inst_id="ETH-USDT"))
    book.place(make_order("X1", SIDE_BUY, 59998.0, 1.0, owner="other-mm"))

    assert book.remove_owner("mock_mm", "BTC-USDT") == 2
    assert book.best("BTC-USDT") == ((59998.0, 1.0), None)          # 别人的挂单没被误撤
    assert book.best("ETH-USDT") == ((3000.0, 1.0), None)           # 别的合约没被误撤
    assert book.remove_owner("mock_mm", "BTC-USDT") == 0
    # inst_id 省略 = 该 owner 的全部合约（reset 用）
    assert book.remove_owner("mock_mm") == 1
    assert book.remove_owner("other-mm", "BTC-USDT") == 1
    assert book.snapshot() == {"BTC-USDT": {"bids": [], "asks": []}, "ETH-USDT": {"bids": [], "asks": []}}


def test_clear_empties_the_book():
    book = OrderBook()
    book.place(make_order("E1", SIDE_BUY, 60000.0, 1.0))
    book.clear()
    assert book.best("BTC-USDT") == (None, None)
    assert book.snapshot() == {}
    assert book.cancel("E1") is False  # 索引也清了，不会漏出一个已不在簿的挂单


# ── place 的入参校验 ─────────────────────────────────────────────────────────

def test_place_rejects_duplicate_cl_ord_id():
    """重复 clOrdId 会让撤单索引指向两张单 —— 宁可响亮报错（真实 OKX 也拒）。"""
    book = OrderBook()
    book.place(make_order("E1", SIDE_BUY, 60000.0, 1.0))
    with pytest.raises(ValueError):
        book.place(make_order("E1", SIDE_BUY, 60000.0, 1.0))


@pytest.mark.parametrize("bad", [
    dict(side="Buy"),          # 大小写写错：引擎 DictBsSideFromOkx 只认 "buy"/"sell"
    dict(side="long"),
    dict(px=0.0),
    dict(sz=0.0),
    dict(sz=-1.0),
])
def test_place_rejects_bad_fields(bad):
    book = OrderBook()
    with pytest.raises(ValueError):
        book.place(make_order(**bad))
