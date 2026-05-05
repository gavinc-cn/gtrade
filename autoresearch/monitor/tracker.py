"""轮询 MySQL 中的实盘数据，计算实时 PnL 和回撤。

从 strat_info 表获取策略状态，从 kline 表辅助计算收益。
"""

import time
import sqlalchemy
from sqlalchemy import text
from autoresearch.config import get as cfg


def _engine():
    m = cfg()["mysql"]
    db_url = (f"mysql+pymysql://{m['user']}:{m['password']}"
              f"@{m['host']}:{m['port']}/{m['db']}")
    return sqlalchemy.create_engine(db_url, pool_pre_ping=True)


def get_strategy_pnl(strat_id: str) -> dict:
    """获取策略实时 PnL 信息。

    Returns:
        {strat_id, total_pnl, peak_pnl, drawdown}
    """
    engine = _engine()
    with engine.connect() as conn:
        # 查询 strat_info 表（根据实际表结构调整字段名）
        result = conn.execute(
            text("SELECT * FROM strat_info WHERE strat_id = :sid LIMIT 1"),
            {"sid": strat_id}
        ).fetchone()

    if result is None:
        return {"strat_id": strat_id, "total_pnl": 0.0, "drawdown": 0.0}

    row = dict(result._mapping)
    total_pnl = float(row.get("total_pnl", 0) or 0)
    peak_pnl = float(row.get("peak_pnl", total_pnl) or total_pnl)
    drawdown = (peak_pnl - total_pnl) / abs(peak_pnl) if peak_pnl != 0 else 0.0

    return {
        "strat_id": strat_id,
        "total_pnl": total_pnl,
        "peak_pnl": peak_pnl,
        "drawdown": max(0.0, drawdown),
    }


def monitor_loop(strat_ids: list[str], on_breach: callable, interval_s: int = 60) -> None:
    """持续监控，回撤超阈值时调用 on_breach(strat_id, drawdown)。"""
    cb_threshold = cfg()["monitor"]["circuit_breaker_drawdown"]
    print(f"[Monitor] 开始监控 {strat_ids}, 熔断阈值={cb_threshold:.1%}")
    while True:
        for sid in strat_ids:
            try:
                info = get_strategy_pnl(sid)
                if info["drawdown"] >= cb_threshold:
                    on_breach(sid, info["drawdown"])
            except Exception as e:
                print(f"[Monitor] 获取 {sid} 数据失败: {e}")
        time.sleep(interval_s)
