"""
GTrade MCP Server 配置
所有路径和端口常量集中在此文件，通过环境变量覆盖。
"""

import os
from pathlib import Path

# ── 项目根目录 ──────────────────────────────────────────────────────────────────
GTRADE_ROOT = Path(os.environ.get("GTRADE_ROOT", "/opt/win/gtrade"))

# ── 服务地址 ────────────────────────────────────────────────────────────────────
# Flask web_server（查询类接口，需 JWT）；app.py 实际绑定 46011（文档中旧的 :5000 已过期）
FLASK_BASE_URL = os.environ.get("FLASK_BASE_URL", "http://127.0.0.1:46011")
# C++ HttpGateway（操作类接口，无需认证）
HTTP_GW_BASE_URL = os.environ.get("HTTP_GW_BASE_URL", "http://127.0.0.1:46012")

# ── Flask 认证（从 config/config.yml 读取，与 web_server 保持一致） ──────────
def _load_web_credentials() -> tuple[str, str]:
    import yaml
    cfg_file = GTRADE_ROOT / "config/config.yml"
    if cfg_file.exists():
        with open(cfg_file, "r", encoding="utf-8") as f:
            cfg = yaml.safe_load(f)
            return cfg.get("user", "admin"), cfg.get("password", "admin")
    return "admin", "admin"

_DEFAULT_USER, _DEFAULT_PASS = _load_web_credentials()
FLASK_USERNAME = os.environ.get("FLASK_USERNAME", _DEFAULT_USER)
FLASK_PASSWORD = os.environ.get("FLASK_PASSWORD", _DEFAULT_PASS)

# ── 路径常量 ────────────────────────────────────────────────────────────────────
BUILD_DIR             = GTRADE_ROOT / "cmake-build-debug-dockerubuntu24"
STRATEGY_SRC_DIR      = GTRADE_ROOT / "src/strategy"
STRATEGY_PARAM_DIR    = GTRADE_ROOT / "src/strategy_param"
STRATEGY_CONFIG_DIR   = GTRADE_ROOT / "strategy_config"
CMAKELISTS_PATH       = GTRADE_ROOT / "src/CMakeLists.txt"
BACKTEST_BIN          = BUILD_DIR / "bin/gtrade_bt"
BACKTEST_TEMP_DIR     = Path(os.environ.get("BACKTEST_TEMP_DIR", "/tmp/gtrade_backtest"))

# ── 安全：禁止读写的路径关键字 ──────────────────────────────────────────────────
SENSITIVE_PATH_PATTERNS = ["secret", ".env", "cert", "key", "passphrase"]

# ── HTTP 超时（秒） ──────────────────────────────────────────────────────────────
HTTP_TIMEOUT = 10.0
BUILD_TIMEOUT = 120   # 单个 .so 编译超时
