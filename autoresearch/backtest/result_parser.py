"""解析回测输出的 done CSV 文件，构建 PnL 序列。

gtrade_bt 在 backtest_out_dir 下输出:
  {start}_{end}_done_{date}.csv  -- 成交记录
  字段: timestamp, inst_id, bs_side, price, qty, pnl, ...
"""

import csv
import os
import glob


def find_done_csv(out_dir: str) -> str | None:
    """在输出目录中查找 done CSV 文件。"""
    pattern = os.path.join(out_dir, "*_done_*.csv")
    files = glob.glob(pattern)
    if not files:
        return None
    return sorted(files)[-1]   # 取最新


def parse_done_csv(csv_path: str) -> list[dict]:
    """解析 done CSV，返回成交记录列表。

    Returns:
        list of {timestamp_ms, inst_id, bs_side, price, qty, pnl, cumulative_pnl}
    """
    trades = []
    cumulative_pnl = 0.0
    with open(csv_path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            pnl = float(row.get("pnl", 0) or 0)
            cumulative_pnl += pnl
            trades.append({
                "timestamp_ms": int(row.get("timestamp", 0) or 0),
                "inst_id": row.get("inst_id", ""),
                "bs_side": row.get("bs_side", ""),
                "price": float(row.get("price", 0) or 0),
                "qty": float(row.get("qty", 0) or 0),
                "pnl": pnl,
                "cumulative_pnl": cumulative_pnl,
            })
    return trades
