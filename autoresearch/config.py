"""系统配置加载 — 读取 autoresearch_config.yml，环境变量优先。"""

import os
import yaml
from pathlib import Path
from dotenv import load_dotenv

# 加载 .env 文件
env_path = Path(__file__).parent.parent / '.env'
if env_path.exists():
    load_dotenv(env_path)

_CONFIG: dict = {}


def load_config(path: str | None = None) -> dict:
    global _CONFIG
    if path is None:
        path = os.environ.get(
            "AUTORESEARCH_CONFIG",
            str(Path(__file__).parent.parent / "autoresearch_config.yml")
        )
    with open(path, "r", encoding="utf-8") as f:
        _CONFIG = yaml.safe_load(f)
    # 环境变量优先
    if api_key := os.environ.get("ANTHROPIC_API_KEY"):
        _CONFIG["anthropic_api_key"] = api_key
    return _CONFIG


def get() -> dict:
    if not _CONFIG:
        load_config()
    return _CONFIG
