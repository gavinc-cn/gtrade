"""自动熔断：通过 HTTP API 暂停策略。

GTrade HttpGateway 提供 REST 接口控制策略状态。
"""

import requests

_GTRADE_HTTP = "http://localhost:8080"


def pause_strategy(strat_id: str, drawdown: float) -> bool:
    """通过 HTTP API 暂停策略。

    Args:
        strat_id: 策略 ID
        drawdown: 当前回撤比例（触发熔断）

    Returns:
        True 表示暂停成功，False 表示失败
    """
    print(f"[CircuitBreaker] {strat_id} 回撤 {drawdown:.1%}，触发熔断，暂停策略")
    try:
        resp = requests.post(
            f"{_GTRADE_HTTP}/strategy/pause",
            json={"strat_id": strat_id},
            timeout=5,
        )
        resp.raise_for_status()
        print(f"[CircuitBreaker] {strat_id} 已暂停")
        return True
    except Exception as e:
        print(f"[CircuitBreaker] 暂停 {strat_id} 失败: {e}")
        return False
