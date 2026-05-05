"""将下载的数据写成 gtrade_bt 要求的 CSV 格式。

depth1 CSV 格式（gtrade_bt csv_quote.cpp 要求）：
  路径：{base_dir}/depth1/{YYYYMMDD}/{inst}.{market}.{YYYYMMDD}.csv
  列：ex_time,local_time,ex_time_iso,market,symbol,bid1_px,bid1_vol,ask1_px,ask1_vol

使用 K 线收盘价作为 bid/ask（spread=0.5 bps），因为回测策略多基于 K 线，
depth1 仅用于触发时间戳对齐。
"""

import csv
from pathlib import Path
from datetime import datetime, timezone


def write_depth1_csv(
    candles: list[dict],
    base_dir: str,
    inst_id: str,
    market: str = "okx",
) -> list[str]:
    """写 depth1 CSV，按日期分文件。返回写入的文件路径列表。"""
    by_date: dict[str, list] = {}
    for c in candles:
        date_str = datetime.fromtimestamp(c["ex_time"] / 1000, tz=timezone.utc).strftime("%Y%m%d")
        by_date.setdefault(date_str, []).append(c)

    written = []
    for date_str, rows in by_date.items():
        dir_path = Path(base_dir) / "depth1" / date_str
        dir_path.mkdir(parents=True, exist_ok=True)
        file_path = dir_path / f"{inst_id}.{market}.{date_str}.csv"

        with open(file_path, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["ex_time", "local_time", "ex_time_iso", "market", "symbol",
                             "bid1_px", "bid1_vol", "ask1_px", "ask1_vol"])
            for c in rows:
                spread = c["close"] * 0.00005   # 0.5 bps spread
                writer.writerow([
                    c["ex_time"], c["local_time"], c["ex_time_iso"],
                    c["market"], c["symbol"],
                    round(c["close"] - spread, 4), c["vol"],
                    round(c["close"] + spread, 4), c["vol"],
                ])
        written.append(str(file_path))
    return written
