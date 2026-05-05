"""测试 MySQL catalog（需要 MySQL 服务运行在 localhost:3307）。"""

import os
import socket
import pytest
import autoresearch.config as cfg_mod
from autoresearch.db import catalog
from pathlib import Path
from dotenv import load_dotenv

# 加载 .env 文件
env_path = Path(__file__).parent.parent.parent / '.env'
if env_path.exists():
    load_dotenv(env_path)

TEST_CFG = {
    "mysql": {"host": os.environ.get("DB_HOST", "localhost"),
              "port": int(os.environ.get("DB_PORT", "3307")),
              "db": os.environ.get("DB_NAME", "gtrade"),
              "user": os.environ.get("DB_USER", "gtrade"),
              "password": os.environ.get("DB_PASSWORD", "")},
    "anthropic_api_key": os.environ.get("ANTHROPIC_API_KEY", ""),
}


def _mysql_available() -> bool:
    """检查 MySQL 是否可达（用于 CI/无 DB 环境跳过）。"""
    try:
        s = socket.create_connection(("localhost", 3307), timeout=2)
        s.close()
        return True
    except OSError:
        return False


pytestmark = pytest.mark.skipif(
    not _mysql_available(), reason="MySQL localhost:3307 not reachable"
)


@pytest.fixture(autouse=True)
def setup(monkeypatch):
    monkeypatch.setattr(cfg_mod, "_CONFIG", TEST_CFG)
    monkeypatch.setattr(catalog, "_engine", None)
    catalog.init_db()


def test_add_and_get_pipeline():
    pid = catalog.add_pipeline("test_strat", "arxiv:test",
                               {"signal_logic": "buy low sell high"}, "/tmp/t.py")
    p = catalog.get_pipeline(pid)
    assert p is not None
    assert p["name"] == "test_strat"
    assert p["status"] == "draft"


def test_update_status():
    pid = catalog.add_pipeline("s", "src", {}, "/tmp/s.py")
    catalog.update_status(pid, "backtesting")
    assert catalog.get_pipeline(pid)["status"] == "backtesting"


def test_add_run():
    pid = catalog.add_pipeline("s", "src", {}, "/tmp/s.py")
    rid = catalog.add_run(pid, {"p": 10}, "20240101_0000", "20241231_0000",
                          {"sharpe": 1.8, "max_dd": 0.12, "calmar": 1.5,
                           "win_rate": 0.55, "profit_factor": 1.4})
    assert rid is not None


def test_list_pipelines():
    catalog.add_pipeline("a", "src", {}, "/tmp/a.py")
    catalog.add_pipeline("b", "src", {}, "/tmp/b.py")
    assert len(catalog.list_pipelines()) >= 2
