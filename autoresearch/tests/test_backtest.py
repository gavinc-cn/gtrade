"""测试回测评价器（不依赖 gtrade_bt）。"""

import pytest
from autoresearch.backtest.evaluator import (
    compute_metrics, meets_promotion_criteria, compute_ic, compute_rolling_ic
)
from autoresearch import config as cfg_mod

SAMPLE_TRADES = [
    {"pnl": 100.0, "cumulative_pnl": 100.0},
    {"pnl": -30.0, "cumulative_pnl": 70.0},
    {"pnl": 80.0,  "cumulative_pnl": 150.0},
    {"pnl": -20.0, "cumulative_pnl": 130.0},
    {"pnl": 60.0,  "cumulative_pnl": 190.0},
    {"pnl": 0.0,   "cumulative_pnl": 190.0},  # 未平仓，忽略
]


@pytest.fixture(autouse=True)
def mock_cfg(monkeypatch):
    monkeypatch.setattr(cfg_mod, "_CONFIG", {
        "promotion": {
            "min_sharpe": 1.5,
            "max_drawdown": 0.15,
            "min_calmar": 1.0,
            "min_oos_correlation": 0.6,
            "min_ic_mean": 0.02,
            "ic_window": 5,
        }
    })


def test_compute_metrics_basic():
    m = compute_metrics(SAMPLE_TRADES, initial_capital=10000.0)
    assert m["total_trades"] == 5   # 0 pnl 的不算
    assert m["win_rate"] == pytest.approx(3 / 5, rel=0.01)
    assert 0 < m["profit_factor"] < 100
    assert 0 <= m["max_dd"] <= 1.0


def test_compute_metrics_empty():
    m = compute_metrics([], initial_capital=10000.0)
    assert m["sharpe"] == 0.0
    assert m["max_dd"] == 0.0   # 无交易 = 无回撤，不是 100% 回撤


def test_meets_promotion_good():
    metrics = {"sharpe": 2.0, "max_dd": 0.10, "calmar": 1.5,
               "win_rate": 0.6, "profit_factor": 1.5, "total_trades": 100, "total_pnl": 5000}
    passed, reasons = meets_promotion_criteria(metrics)
    assert passed
    assert reasons == []


def test_meets_promotion_bad_sharpe():
    metrics = {"sharpe": 0.8, "max_dd": 0.10, "calmar": 1.5,
               "win_rate": 0.6, "profit_factor": 1.5, "total_trades": 100, "total_pnl": 5000}
    passed, reasons = meets_promotion_criteria(metrics)
    assert not passed
    assert any("Sharpe" in r for r in reasons)


def test_compute_ic_positive():
    # 信号与收益正相关时 IC 应为正
    signals = [1.0, 2.0, 3.0, 4.0, 5.0]
    returns = [0.1, 0.2, 0.3, 0.4, 0.5]
    ic = compute_ic(signals, returns)
    assert ic == pytest.approx(1.0, abs=0.01)


def test_compute_ic_insufficient_data():
    # 数据不足时返回 0
    ic = compute_ic([1.0, 2.0], [0.1, 0.2])
    assert ic == 0.0


def test_compute_rolling_ic():
    # 构造30笔交易，足够滚动窗口
    import random
    random.seed(42)
    trades = [{"pnl": random.gauss(10, 50), "signal": random.gauss(0, 1)}
              for _ in range(30)]
    result = compute_rolling_ic(trades, window=10)
    assert "ic_mean" in result
    assert "icir" in result
    assert len(result["ic_series"]) > 0


def test_meets_promotion_low_ic():
    # IC 低于门槛时应不通过
    metrics = {"sharpe": 2.0, "max_dd": 0.10, "calmar": 1.5,
               "win_rate": 0.6, "profit_factor": 1.5, "total_trades": 100,
               "total_pnl": 5000, "ic_mean": 0.005}  # 低于 min_ic_mean=0.02
    passed, reasons = meets_promotion_criteria(metrics)
    assert not passed
    assert any("IC" in r for r in reasons)
