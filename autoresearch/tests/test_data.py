"""测试 CSV 写入逻辑（不依赖网络）。"""

import csv
import os
import tempfile
from autoresearch.data.csv_writer import write_depth1_csv

SAMPLE_CANDLES = [
    {"ex_time": 1704067200000, "local_time": 1704067200050,
     "ex_time_iso": "2024-01-01T00:00:00.000Z",
     "market": "okx", "symbol": "BTC-USDT-SWAP",
     "open": 42000.0, "high": 42100.0, "low": 41900.0, "close": 42050.0, "vol": 10.0},
    {"ex_time": 1704070800000, "local_time": 1704070800050,
     "ex_time_iso": "2024-01-01T01:00:00.000Z",
     "market": "okx", "symbol": "BTC-USDT-SWAP",
     "open": 42050.0, "high": 42200.0, "low": 41950.0, "close": 42150.0, "vol": 8.0},
]


def test_write_depth1_csv():
    with tempfile.TemporaryDirectory() as tmp:
        files = write_depth1_csv(SAMPLE_CANDLES, tmp, "BTC-USDT-SWAP", "okx")
        assert len(files) == 1  # 同一天
        assert "20240101" in files[0]

        with open(files[0]) as f:
            rows = list(csv.DictReader(f))
        assert len(rows) == 2
        assert float(rows[0]["bid1_px"]) < float(rows[0]["ask1_px"])  # spread > 0
        assert rows[0]["market"] == "okx"


def test_write_depth1_csv_multiday():
    candles = SAMPLE_CANDLES + [{
        "ex_time": 1704153600000,   # 2024-01-02
        "local_time": 1704153600050,
        "ex_time_iso": "2024-01-02T00:00:00.000Z",
        "market": "okx", "symbol": "BTC-USDT-SWAP",
        "open": 42150.0, "high": 42300.0, "low": 42000.0, "close": 42200.0, "vol": 12.0,
    }]
    with tempfile.TemporaryDirectory() as tmp:
        files = write_depth1_csv(candles, tmp, "BTC-USDT-SWAP", "okx")
    assert len(files) == 2
