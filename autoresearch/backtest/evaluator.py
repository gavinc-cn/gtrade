"""计算回测评价指标：Sharpe、MaxDD、Calmar、胜率、盈亏比、IC/ICIR。

IC（信息系数）参考 Qlib 的 Alpha 评估体系：
  - IC = 预测信号与下期收益的 Pearson 相关系数
  - ICIR = IC均值 / IC标准差（IC 稳定性）
  - IC > 0.02 有统计意义，IC > 0.05 较优
Sharpe 高可能是运气，IC 持续为正才说明策略真的有 alpha。
"""

import math


def compute_metrics(trades: list[dict], initial_capital: float = 100000.0,
                    bar_interval_hours: float = 1.0, n_days: float = 365.0) -> dict:
    """计算核心回测指标。

    Args:
        trades: parse_done_csv 返回的成交记录
        initial_capital: 初始资金（用于计算回撤百分比）
        bar_interval_hours: K 线周期（小时），用于计算 Sharpe 年化系数。
            1H → 1.0，4H → 4.0，1D → 24.0。调用方从策略 spec 传入。
        n_days: 回测实际覆盖天数，用于计算 Calmar 年化收益。
            从 start_date/end_date 推算后传入，避免固定假设1年。

    Returns:
        {sharpe, max_dd, calmar, win_rate, profit_factor, total_trades, total_pnl}
    """
    if not trades:
        # max_dd=0.0 表示"无交易无回撤"；1.0 语义是"100%回撤"，不适用于空情况
        return {
            "sharpe": 0.0, "max_dd": 0.0, "calmar": 0.0,
            "win_rate": 0.0, "profit_factor": 0.0,
            "total_trades": 0, "total_pnl": 0.0,
        }

    pnls = [t["pnl"] for t in trades if t["pnl"] != 0]
    if not pnls:
        return {
            "sharpe": 0.0, "max_dd": 0.0, "calmar": 0.0,
            "win_rate": 0.0, "profit_factor": 0.0,
            "total_trades": 0, "total_pnl": 0.0,
        }

    total_pnl = sum(pnls)
    wins = [p for p in pnls if p > 0]
    losses = [p for p in pnls if p < 0]
    win_rate = len(wins) / len(pnls)
    # 无亏损时 profit_factor 上限 99.0，避免 float("inf") 写入 MySQL/JSON 失败
    profit_factor = (sum(wins) / abs(sum(losses))) if losses else min(sum(wins) / 1e-9, 99.0)

    # Sharpe 年化：根据实际 K 线周期动态计算年化系数，不固定假设1小时
    # n_periods_per_year = 8760 / bar_interval_hours（如4H策略 → sqrt(2190)）
    n_periods_per_year = 8760.0 / bar_interval_hours
    mean_pnl = sum(pnls) / len(pnls)
    variance = sum((p - mean_pnl) ** 2 for p in pnls) / len(pnls)
    std_pnl = math.sqrt(variance) if variance > 0 else 1e-9
    sharpe = (mean_pnl / std_pnl) * math.sqrt(n_periods_per_year) if std_pnl > 0 else 0.0

    # Max Drawdown
    equity = initial_capital
    peak = initial_capital
    max_dd = 0.0
    for t in trades:
        equity += t["pnl"]
        if equity > peak:
            peak = equity
        dd = (peak - equity) / peak if peak > 0 else 0
        if dd > max_dd:
            max_dd = dd

    # Calmar = 年化收益 / MaxDD；用实际回测天数换算年化，避免固定假设1年
    actual_years = max(n_days / 365.0, 1e-6)
    annual_return = (total_pnl / initial_capital) / actual_years
    calmar = annual_return / max_dd if max_dd > 0 else 0.0

    return {
        "sharpe": round(sharpe, 4),
        "max_dd": round(max_dd, 4),
        "calmar": round(calmar, 4),
        "win_rate": round(win_rate, 4),
        "profit_factor": round(min(profit_factor, 99.0), 4),
        "total_trades": len(pnls),
        "total_pnl": round(total_pnl, 4),
    }


def compute_ic(signals: list[float], next_returns: list[float]) -> float:
    """计算 IC（信息系数）：预测信号与下期收益的 Pearson 相关系数。

    IC > 0.02 通常被认为有统计意义；IC > 0.05 已属较好。
    ICIR = IC均值 / IC标准差，反映 IC 的稳定性。

    Args:
        signals: 每期的策略信号值（如 SMA 偏离度、动量值等）
        next_returns: 对应期的下期实际收益（与 signals 等长，对齐后移一期）

    Returns:
        IC 值，范围 [-1, 1]；数据不足时返回 0.0
    """
    if len(signals) < 5 or len(signals) != len(next_returns):
        return 0.0
    n = len(signals)
    mean_s = sum(signals) / n
    mean_r = sum(next_returns) / n
    cov = sum((s - mean_s) * (r - mean_r) for s, r in zip(signals, next_returns)) / n
    std_s = math.sqrt(sum((s - mean_s) ** 2 for s in signals) / n) or 1e-9
    std_r = math.sqrt(sum((r - mean_r) ** 2 for r in next_returns) / n) or 1e-9
    return round(cov / (std_s * std_r), 4)


def compute_rolling_ic(trades: list[dict], window: int = 20) -> dict:
    """用滚动窗口计算 IC 序列，汇报均值 IC 和 ICIR。

    trades 中需包含 signal 字段（策略信号值）和 pnl 字段。
    若无 signal 字段则用 pnl 滚动均值代替（退化为自相关检验）。

    Returns:
        {"ic_mean": float, "icir": float, "ic_series": list[float]}
    """
    if len(trades) < window + 1:
        return {"ic_mean": 0.0, "icir": 0.0, "ic_series": []}

    # 用 pnl 的滚动均值作信号，下一笔 pnl 作收益（若无 signal 字段）
    pnls = [tr["pnl"] for tr in trades]
    signals = [trades[i].get("signal", sum(pnls[max(0, i - window):i]) / window)
               for i in range(len(trades))]
    next_rets = pnls[1:] + [0.0]  # 下期收益，最后一期无法观测填0

    ic_series = []
    for i in range(window, len(trades)):
        s_win = signals[i - window:i]
        r_win = next_rets[i - window:i]
        ic_series.append(compute_ic(s_win, r_win))

    if not ic_series:
        return {"ic_mean": 0.0, "icir": 0.0, "ic_series": []}

    ic_mean = sum(ic_series) / len(ic_series)
    ic_std = math.sqrt(sum((x - ic_mean) ** 2 for x in ic_series) / len(ic_series)) or 1e-9
    icir = ic_mean / ic_std

    return {
        "ic_mean": round(ic_mean, 4),
        "icir": round(icir, 4),
        "ic_series": [round(x, 4) for x in ic_series],
    }


def meets_promotion_criteria(metrics: dict) -> tuple[bool, list[str]]:
    """检查指标是否满足晋级标准。

    Returns:
        (passed: bool, reasons: list[str])
    """
    from autoresearch.config import get as cfg
    promo = cfg()["promotion"]
    reasons = []

    if metrics["sharpe"] < promo["min_sharpe"]:
        reasons.append(f"Sharpe {metrics['sharpe']:.2f} < {promo['min_sharpe']}")
    if metrics["max_dd"] > promo["max_drawdown"]:
        reasons.append(f"MaxDD {metrics['max_dd']:.1%} > {promo['max_drawdown']:.1%}")
    if metrics["calmar"] < promo["min_calmar"]:
        reasons.append(f"Calmar {metrics['calmar']:.2f} < {promo['min_calmar']}")
    # IC 检查：若 metrics 包含 ic_mean 则验证，不含则跳过（兼容不记录 signal 的策略）
    min_ic = promo.get("min_ic_mean", 0.0)
    if min_ic > 0 and "ic_mean" in metrics:
        if metrics["ic_mean"] < min_ic:
            reasons.append(f"IC均值 {metrics['ic_mean']:.4f} < {min_ic}（信号预测力不足）")

    return len(reasons) == 0, reasons
