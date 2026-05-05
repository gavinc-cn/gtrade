"""OKX REST API 历史 K 线下载器。

OKX Candles API: GET /api/v5/market/history-candles
参数: instId, bar (1m/5m/1H/etc), before, after, limit(max 100)
返回: [[ts, open, high, low, close, vol, volCcy, volCcyQuote, confirm], ...]
ts 单位：毫秒
"""

import time
import requests
from datetime import datetime, timezone

OKX_BASE = "https://www.okx.com"
_BAR_MAP = {
    "1m": "1m", "5m": "5m", "15m": "15m", "30m": "30m",
    "1H": "1H", "4H": "4H", "1D": "1D",
}


def _ts_to_ms(dt: datetime) -> int:
    return int(dt.replace(tzinfo=timezone.utc).timestamp() * 1000)


def _parse_date(s: str) -> datetime:
    """支持 'YYYYmmdd_HHMM' 或 'YYYY-mm-dd' 格式。"""
    s = s.strip()
    if "_" in s:
        return datetime.strptime(s, "%Y%m%d_%H%M")
    return datetime.strptime(s, "%Y-%m-%d")


def download_candles(
    inst_id: str,
    bar: str,
    start_date: str,
    end_date: str,
    market: str = "okx",
) -> list[dict]:
    """下载 OKX K 线数据。

    Args:
        inst_id: 合约 ID，如 BTC-USDT-SWAP
        bar: K 线周期，如 1H、4H、1D
        start_date: 开始日期，如 20240101_0000
        end_date: 结束日期，如 20241231_0000
        market: 市场标识，默认 okx

    Returns:
        list of dicts with keys: ex_time(ms), open, high, low, close,
        vol, local_time(ms), ex_time_iso, market, symbol
    """
    assert bar in _BAR_MAP, f"Unsupported bar: {bar}. Use {list(_BAR_MAP)}"
    start_ms = _ts_to_ms(_parse_date(start_date))
    end_ms = _ts_to_ms(_parse_date(end_date))

    result = []
    before_ms = end_ms  # OKX: before 表示拉取该时间戳之前的数据

    while True:
        url = f"{OKX_BASE}/api/v5/market/history-candles"
        params = {
            "instId": inst_id,
            "bar": _BAR_MAP[bar],
            "before": str(before_ms),
            "limit": "100",
        }
        resp = requests.get(url, params=params, timeout=10)
        resp.raise_for_status()
        data = resp.json()
        if data.get("code") != "0":
            raise RuntimeError(f"OKX API error: {data}")

        candles = data.get("data", [])
        if not candles:
            break

        for c in candles:
            ts = int(c[0])
            if ts < start_ms:
                return result
            if ts > end_ms:
                continue
            dt_iso = datetime.fromtimestamp(ts / 1000, tz=timezone.utc).strftime(
                "%Y-%m-%dT%H:%M:%S.000Z"
            )
            result.append({
                "ex_time": ts,
                "local_time": ts + 50,   # 模拟本地时间偏移
                "ex_time_iso": dt_iso,
                "market": market,
                "symbol": inst_id,
                "open": float(c[1]),
                "high": float(c[2]),
                "low": float(c[3]),
                "close": float(c[4]),
                "vol": float(c[5]),
            })

        oldest_ts = int(candles[-1][0])
        if oldest_ts <= start_ms:
            break
        before_ms = oldest_ts
        time.sleep(0.2)  # 避免触发速率限制

    return sorted(result, key=lambda x: x["ex_time"])
