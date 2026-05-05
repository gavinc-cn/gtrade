"""晋级门槛检查：综合评估策略是否可以上实盘。

双重验证：
1. 回测样本内指标 (Sharpe/MaxDD/Calmar/IC)
2. Walk-Forward OOS Sharpe（样本外）
"""

from autoresearch.backtest.evaluator import meets_promotion_criteria


class PromotionResult:
    def __init__(self, passed: bool, reasons: list[str], metrics: dict, oos_sharpe: float):
        self.passed = passed
        self.reasons = reasons
        self.metrics = metrics
        self.oos_sharpe = oos_sharpe

    def __str__(self):
        status = "通过" if self.passed else "未通过"
        reasons_str = "\n  ".join(self.reasons) if self.reasons else "无"
        ic_str = (f"  IC均值: {self.metrics.get('ic_mean', 'N/A')}  "
                  f"ICIR: {self.metrics.get('icir', 'N/A')}")
        return (
            f"{status}\n"
            f"Sharpe: {self.metrics.get('sharpe', 0):.2f}  "
            f"MaxDD: {self.metrics.get('max_dd', 1):.1%}  "
            f"Calmar: {self.metrics.get('calmar', 0):.2f}  "
            f"OOS Sharpe: {self.oos_sharpe:.2f}\n"
            f"{ic_str}\n"
            f"未通过原因: {reasons_str}"
        )


def check_promotion(metrics: dict, oos_sharpe: float) -> PromotionResult:
    """检查是否满足晋级条件。

    Args:
        metrics: compute_metrics 返回的回测指标
        oos_sharpe: Walk-Forward 样本外 Sharpe

    Returns:
        PromotionResult 包含 passed、reasons、metrics、oos_sharpe
    """
    from autoresearch.config import get as cfg
    promo = cfg()["promotion"]

    passed, reasons = meets_promotion_criteria(metrics)

    # 额外检查 OOS Sharpe
    if oos_sharpe < promo["min_sharpe"] * 0.7:  # OOS 至少达到标准的70%
        reasons.append(
            f"OOS Sharpe {oos_sharpe:.2f} 低于阈值 "
            f"{promo['min_sharpe'] * 0.7:.2f}"
        )
        passed = False

    return PromotionResult(passed=passed, reasons=reasons,
                           metrics=metrics, oos_sharpe=oos_sharpe)
