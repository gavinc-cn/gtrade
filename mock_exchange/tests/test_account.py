"""账户结算测试（TDD：先红后绿）

事实依据两处：
- task-4 brief：买入 → base ccy 增、quote ccy 减；卖空 → 持仓为负；快照对齐 OKX；
- `doc_ai/spec/mock_exchange/引擎OKX契约.md` 的 `balance_and_position` 行：
  `data[].balData[].{ccy,cashBal,uTime}`、`data[].posData[].{instId,instType,mgnMode,posSide,pos,uTime,avgPx}`。

引擎侧读法（`src/websocket/okx_trade.cpp:538` 起）定下两条硬约束：这些字段**一律字符串**
（`GetString()` 遇数字在 Debug 构建直接 abort），且 `cashBal` 同时填 `total` 与 `available`、
`pos` 直接进 `hold.available`。
"""

import json

import pytest

from mockex.config import AccountConfig, InstrumentConfig
from mockex.core.account import AccountManager
from mockex.core.models import SIDE_BUY, SIDE_SELL, Fill

TS = 1_700_000_000_123
ACC = "mock-okx-key"  # 引擎账户
MM = "mock_mm"        # 注入方（不在配置里，按需建账户）
START = {"USDT": 1_000_000.0, "BTC": 100.0}


def acc(api_key=ACC, balances=None):
    """配置账户：默认 100 万 USDT + 100 BTC。"""
    return AccountConfig(api_key=api_key, secret="secret", passphrase="pass",
                         balances=dict(START) if balances is None else balances)


def inst(inst_id="BTC-USDT", base="BTC", quote="USDT", inst_type="SPOT"):
    """合约规格：结算币种只从这里取（不从 instId 拆字符串）。"""
    return InstrumentConfig(inst_id=inst_id, inst_type=inst_type, base_ccy=base,
                            quote_ccy=quote, settle_ccy=quote)


def manager(accounts=None, instruments=None):
    """默认单账户 + BTC-USDT 现货。"""
    return AccountManager([acc()] if accounts is None else accounts,
                          [inst()] if instruments is None else instruments)


def fill(side=SIDE_BUY, px=60000.0, sz=0.1, taker=ACC, maker=MM, inst_id="BTC-USDT", ts=TS):
    """一笔成交；`side` 是**主动方**方向，maker 按反方向结算。"""
    return Fill(ord_id="1", inst_id=inst_id, px=px, sz=sz, ts=ts,
                taker_owner=taker, maker_owner=maker, side=side)


def cash(element, ccy):
    """取某币种的 cashBal（字段名写错要立刻暴露，而不是 KeyError 一片）。"""
    return next(b["cashBal"] for b in element["balData"] if b["ccy"] == ccy)


def position(element, inst_id="BTC-USDT"):
    """取某合约的 posData 行。"""
    return next(p for p in element["posData"] if p["instId"] == inst_id)


# ── 现货方向：base 与 quote 一增一减 ────────────────────────────────────────

def test_buy_credits_base_and_debits_quote():
    m = manager()
    m.apply_fill(fill(SIDE_BUY, 60000.0, 0.1, maker=""))
    element = m.snapshot(ACC)
    assert (cash(element, "BTC"), cash(element, "USDT")) == ("100.1", "994000")


def test_sell_credits_quote_and_debits_base_without_float_tail():
    """1000 + 60001.2 × 0.3 走 float 得到 19000.359999999997（T2/T3 已因浮点长尾返工一次）。

    余额刻意取小（1000 而非 100 万）：1e6 级别的基数会把双精度误差吸收掉，测不出问题。
    """
    m = manager([acc(balances={"USDT": 1000.0, "BTC": 100.0})])
    m.apply_fill(fill(SIDE_SELL, 60001.2, 0.3, maker=""))
    element = m.snapshot(ACC)
    assert (cash(element, "BTC"), cash(element, "USDT")) == ("99.7", "19000.36")


def test_sell_without_holdings_is_allowed_and_goes_negative():
    """卖空合法：余额与持仓都为负，不拒单（后续衍生品走同一条路径）。"""
    m = manager([acc(balances={"USDT": 1000.0})])
    m.apply_fill(fill(SIDE_SELL, 60000.0, 1.0, maker=""))
    element = m.snapshot(ACC)
    assert (cash(element, "BTC"), cash(element, "USDT")) == ("-1", "61000")
    assert position(element)["pos"] == "-1"


# ── 对手方（被动方）结算 ────────────────────────────────────────────────────

def test_maker_is_settled_in_opposite_direction():
    m = manager([acc(), acc(MM)])
    changed = m.apply_fill(fill(SIDE_BUY, 60000.0, 0.1))  # 主动买 → 被动卖
    assert changed == [ACC, MM]
    element = m.snapshot(MM)
    assert (cash(element, "BTC"), cash(element, "USDT")) == ("99.9", "1006000")


def test_empty_maker_owner_settles_taker_only():
    """空盘口成交没有真对手（T3 的隐含流动性）：跳过空 owner，但绝不丢整笔成交。"""
    m = manager()
    changed = m.apply_fill(fill(SIDE_BUY, 60000.0, 0.1, maker=""))
    assert changed == [ACC]
    assert cash(m.snapshot(ACC), "BTC") == "100.1"  # 主动方侧照常结算
    assert "" not in m.snapshot()                   # 不给空 owner 建账户


def test_unknown_owner_gets_lazy_account():
    """注入方 mock_mm 不在配置里（注入流动性不预扣资金）：按需建账户，负余额可见。"""
    m = manager()
    m.apply_fill(fill(SIDE_BUY, 60000.0, 0.1, maker=MM))
    assert sorted(m.snapshot()) == [ACC, MM]
    element = m.snapshot(MM)
    assert (cash(element, "BTC"), cash(element, "USDT")) == ("-0.1", "6000")


def test_self_trade_nets_out_and_is_reported_once():
    """同一账户自成交：两侧都要记账（净额为 0），但改动账户只报一次。"""
    m = manager()
    changed = m.apply_fill(fill(SIDE_BUY, 60000.0, 0.1, taker=ACC, maker=ACC))
    assert changed == [ACC]
    element = m.snapshot(ACC)
    assert (cash(element, "BTC"), cash(element, "USDT")) == ("100", "1000000")
    assert position(element)["pos"] == "0"


# ── 持仓与均价 ──────────────────────────────────────────────────────────────

def test_buy_accumulates_position_with_weighted_avg_px():
    m = manager()
    m.apply_fill(fill(SIDE_BUY, 60000.0, 0.1, maker=""))
    m.apply_fill(fill(SIDE_BUY, 60002.0, 0.1, maker=""))
    pos = position(m.snapshot(ACC))
    assert (pos["pos"], pos["avgPx"]) == ("0.2", "60001")


def test_position_sums_without_float_tail():
    """0.1 + 0.2 走 float 得到 0.30000000000000004 —— 数量也不能用浮点累加。"""
    m = manager()
    m.apply_fill(fill(SIDE_BUY, 60000.0, 0.1, maker=""))
    m.apply_fill(fill(SIDE_BUY, 60000.0, 0.2, maker=""))
    assert position(m.snapshot(ACC))["pos"] == "0.3"


def test_reducing_position_keeps_avg_px():
    m = manager()
    m.apply_fill(fill(SIDE_BUY, 60000.0, 0.1, maker=""))
    m.apply_fill(fill(SIDE_BUY, 60002.0, 0.1, maker=""))
    m.apply_fill(fill(SIDE_SELL, 59000.0, 0.1, maker=""))  # 平掉一半，均价不动
    pos = position(m.snapshot(ACC))
    assert (pos["pos"], pos["avgPx"]) == ("0.1", "60001")


def test_flip_resets_avg_px_to_new_fill_price():
    m = manager()
    m.apply_fill(fill(SIDE_BUY, 60000.0, 0.1, maker=""))
    m.apply_fill(fill(SIDE_SELL, 60003.0, 0.3, maker=""))  # 穿仓 → 反手做空
    pos = position(m.snapshot(ACC))
    assert (pos["pos"], pos["avgPx"]) == ("-0.2", "60003")


def test_short_position_records_avg_px_of_the_short():
    m = manager([acc(balances={"USDT": 1_000_000.0})])
    m.apply_fill(fill(SIDE_SELL, 60000.0, 0.5, maker=""))
    pos = position(m.snapshot(ACC))
    assert (pos["pos"], pos["avgPx"]) == ("-0.5", "60000")


def test_closed_position_is_reported_as_zero():
    """平仓后仍上报该合约（pos=0）：引擎据 posData 推 kPositionPush，
    丢条目会让策略侧留下过期的持仓。"""
    m = manager()
    m.apply_fill(fill(SIDE_BUY, 60000.0, 0.1, maker=""))
    m.apply_fill(fill(SIDE_SELL, 61000.0, 0.1, maker=""))
    pos = position(m.snapshot(ACC))
    assert (pos["pos"], pos["avgPx"]) == ("0", "0")


# ── snapshot 结构与取值 ─────────────────────────────────────────────────────

def test_snapshot_matches_balance_and_position_shape():
    m = manager()
    m.apply_fill(fill(SIDE_BUY, 60000.0, 0.1, maker=""))
    element = m.snapshot(ACC)

    assert set(element) == {"uTime", "balData", "posData"}
    assert set(element["balData"][0]) == {"ccy", "cashBal", "uTime"}
    pos = position(element)
    assert set(pos) == {"instId", "instType", "mgnMode", "posSide", "pos", "uTime", "avgPx"}
    assert (pos["instType"], pos["mgnMode"], pos["posSide"]) == ("SPOT", "cash", "net")
    assert all(isinstance(v, str) for row in element["balData"] + element["posData"]
               for v in row.values())
    assert isinstance(element["uTime"], str)
    json.dumps(element)  # 不抛 = 没有 float / Decimal 漏进报文


def test_snapshot_lists_all_accounts_keyed_by_name():
    """无参调用给全部账户（`/admin/state` 与枚举账户用），键是账户名。"""
    assert sorted(manager([acc(), acc(MM)]).snapshot()) == [ACC, MM]


def test_snapshot_of_unknown_account_raises():
    with pytest.raises(KeyError):
        manager().snapshot("nobody")


def test_snapshot_is_sorted_and_isolated_from_caller_mutation():
    m = manager([acc(balances={"USDT": 1_000_000.0, "BTC": 100.0, "ETH": 10.0})])
    m.apply_fill(fill(SIDE_BUY, 60000.0, 0.1, maker=""))
    element = m.snapshot(ACC)
    assert [b["ccy"] for b in element["balData"]] == ["BTC", "ETH", "USDT"]  # 稳定顺序

    element["balData"][0]["cashBal"] = "0"  # 改动快照不得回头污染账户
    assert cash(m.snapshot(ACC), "BTC") == "100.1"


def test_u_time_follows_fill_ts_and_stays_zero_when_untouched():
    m = manager([acc(balances={"USDT": 1_000_000.0, "BTC": 100.0, "ETH": 10.0})])
    m.apply_fill(fill(SIDE_BUY, 60000.0, 0.1, maker="", ts=TS))
    element = m.snapshot(ACC)
    assert element["uTime"] == str(TS)
    assert next(b for b in element["balData"] if b["ccy"] == "ETH")["uTime"] == "0"


# ── 合约映射与入参校验 ──────────────────────────────────────────────────────

def test_fill_on_unregistered_instrument_raises():
    """base/quote 只从合约规格取：未知合约的结算币种未知 = 账要记错，当场报。"""
    with pytest.raises(ValueError):
        manager().apply_fill(fill(SIDE_BUY, 2000.0, 1.0, inst_id="ETH-USDT", maker=""))


def test_each_instrument_settles_its_own_ccys():
    m = manager([acc(balances={"USDT": 1_000_000.0, "BTC": 100.0, "ETH": 10.0})],
                [inst(), inst("ETH-USDT", "ETH", "USDT")])
    m.apply_fill(fill(SIDE_BUY, 2000.0, 2.0, inst_id="ETH-USDT", maker=""))
    element = m.snapshot(ACC)
    assert (cash(element, "ETH"), cash(element, "USDT"), cash(element, "BTC")) == ("12", "996000", "100")
    assert position(element, "ETH-USDT")["pos"] == "2"


def test_unknown_side_rejected():
    """`Fill.side` 缺失/写错 → 资金方向反过来，宁可报错（models 备注过旧调用方可能传空串）。"""
    with pytest.raises(ValueError):
        manager().apply_fill(fill(side=""))


def test_missing_taker_owner_rejected_and_ledger_untouched():
    """主动方 owner 缺失 = 这笔成交没有任何账户该记账：响亮报错，绝不静默丢账。

    `Order.owner` 默认空串、撮合原样透传 `taker_owner=order.owner`，所以 T5/T7 任何一处忘盖
    owner 的下单路径都会走到这里；判据是"抛错 + 账本零变化"，而非"返回空列表"——后者正是最难查的
    静默不一致（成交了但账户没动）。
    """
    m = manager()
    before = m.snapshot(ACC)
    bad_fills = [fill(SIDE_BUY, 60000.0, 0.1, taker="", maker=""),
                 fill(SIDE_BUY, 60000.0, 0.1, taker="", maker=MM)]
    for bad in bad_fills:
        with pytest.raises(ValueError):
            m.apply_fill(bad)

    assert m.snapshot(ACC) == before      # 账本零变化（空对手也不许被结算）
    assert sorted(m.snapshot()) == [ACC]  # 不给空 owner / 未成功配对的一方建账户


def test_invalid_price_or_size_rejected():
    """0 / 负数 / NaN / inf 都不许进账：NaN 过得了 `<= 0`，会把 "NaN" 漏进推送帧。"""
    m = manager()
    bad_fills = [fill(SIDE_BUY, 0.0, 0.1, maker=""), fill(SIDE_BUY, 60000.0, 0.0, maker=""),
                 fill(SIDE_BUY, 60000.0, -0.1, maker=""),
                 fill(SIDE_BUY, float("nan"), 0.1, maker=""),
                 fill(SIDE_BUY, 60000.0, float("inf"), maker="")]
    for bad in bad_fills:
        with pytest.raises(ValueError):
            m.apply_fill(bad)


# ── reset ───────────────────────────────────────────────────────────────────

def test_reset_restores_initial_state_and_drops_lazy_accounts():
    m = manager()
    m.apply_fill(fill(SIDE_BUY, 60000.0, 0.1, maker=MM))  # 顺带按需建出 mock_mm
    m.reset()

    assert sorted(m.snapshot()) == [ACC]
    element = m.snapshot(ACC)
    assert (cash(element, "BTC"), cash(element, "USDT")) == ("100", "1000000")
    assert element["posData"] == []
