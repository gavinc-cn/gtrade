"""Walk-Forward 时间窗口生成器。"""

from datetime import datetime, timedelta


def generate_windows(start_date: str, end_date: str,
                     n_windows: int = 4, train_ratio: float = 0.7) -> list[dict]:
    """生成 Walk-Forward 时间窗口（格式 YYYYmmdd_HHMM）。

    Args:
        start_date: 起始日期，如 20240101_0000
        end_date: 结束日期，如 20241231_0000
        n_windows: 总窗口数
        train_ratio: 每个窗口中训练集占比

    Returns:
        list of {train_start, train_end, test_start, test_end}
    """
    def parse(s): return datetime.strptime(s, "%Y%m%d_%H%M")
    def fmt(dt): return dt.strftime("%Y%m%d_%H%M")

    start, end = parse(start_date), parse(end_date)
    window_days = (end - start).days / n_windows
    train_days = window_days * train_ratio

    windows = []
    for i in range(n_windows):
        ws = start + timedelta(days=i * window_days)
        te = ws + timedelta(days=train_days)
        we = ws + timedelta(days=window_days)
        windows.append({"train_start": fmt(ws), "train_end": fmt(te),
                        "test_start": fmt(te), "test_end": fmt(we)})
    return windows
