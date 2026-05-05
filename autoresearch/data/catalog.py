"""记录已下载的数据范围，避免重复下载。"""

import json
from pathlib import Path
from autoresearch.config import get as cfg


def _catalog_path() -> Path:
    return Path(cfg()["paths"]["csv_quote_base_dir"]) / ".data_catalog.json"


def _load() -> dict:
    p = _catalog_path()
    if p.exists():
        return json.loads(p.read_text())
    return {}


def _save(data: dict) -> None:
    _catalog_path().write_text(json.dumps(data, indent=2))


def mark_downloaded(inst_id: str, market: str, bar: str,
                    start_date: str, end_date: str) -> None:
    """记录某合约某周期的数据已下载。"""
    key = f"{inst_id}.{market}.{bar}"
    data = _load()
    data[key] = {"start": start_date, "end": end_date}
    _save(data)


def is_downloaded(inst_id: str, market: str, bar: str,
                  start_date: str, end_date: str) -> bool:
    """检查该合约/周期/日期范围是否已下载。"""
    key = f"{inst_id}.{market}.{bar}"
    data = _load()
    if key not in data:
        return False
    rec = data[key]
    return rec["start"] <= start_date and rec["end"] >= end_date
