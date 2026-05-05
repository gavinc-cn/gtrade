"""
GTrade MCP Server 主入口

启动方式：
  stdio（本地 Claude Desktop / Cursor）：
    cd mcp_server && python server.py

  HTTP SSE（远程客户端，生产模式）：
    cd mcp_server && python server.py --transport sse --port 8765

Claude Desktop 配置（~/.config/claude/claude_desktop_config.json）：
    {
      "mcpServers": {
        "gtrade": {
          "command": "python",
          "args": ["/opt/win/gtrade/mcp_server/server.py"],
          "env": {
            "GTRADE_ROOT": "/opt/win/gtrade",
            "FLASK_BASE_URL": "http://127.0.0.1:5000",
            "HTTP_GW_BASE_URL": "http://127.0.0.1:46012"
          }
        }
      }
    }

环境变量（均有合理默认值）：
    GTRADE_ROOT           项目根目录（默认 /opt/win/gtrade）
    FLASK_BASE_URL        Flask web_server 地址（默认 http://127.0.0.1:5000）
    HTTP_GW_BASE_URL      C++ HttpGateway 地址（默认 http://127.0.0.1:46012）
    FLASK_USERNAME        Flask 登录用户名（默认读取 config/config.yml）
    FLASK_PASSWORD        Flask 登录密码（默认读取 config/config.yml）
    BACKTEST_TEMP_DIR     回测临时目录（默认 /tmp/gtrade_backtest）
    MCP_HOST              SSE 绑定地址（默认 127.0.0.1）
    MCP_PORT              SSE 监听端口（默认 8765）
"""

import argparse
import json
import sys
import os

# 确保 mcp_server/ 目录在 Python 路径中
sys.path.insert(0, os.path.dirname(__file__))

# 设置进程名，便于 stop.sh / status.sh 按名称识别和管理
try:
    import setproctitle
    setproctitle.setproctitle("gtrade_mcp")
except ImportError:
    pass  # setproctitle 不是必须依赖，缺失时静默跳过

from mcp.server.fastmcp import FastMCP

import flask_client
from config import GTRADE_ROOT, STRATEGY_PARAM_DIR
from tools.query_tools import register_query_tools
from tools.control_tools import register_control_tools
from tools.backtest_tools import register_backtest_tools
from tools.codegen_tools import register_codegen_tools

# ── MCP server 实例 ──────────────────────────────────────────────────────────────
mcp = FastMCP(
    name="gtrade",
    instructions="""
GTrade 量化交易系统 MCP 接口。支持现货网格、期货套利、SMA 均线等策略的配置、
监控、代码生成和回测。

【常用工作流】

A. 快速配置现有策略（5 分钟）：
  1. list_strategy_types()               — 查看可用策略类型和参数
  2. create_strategy(type, name, params) — 创建并热加载策略
  3. start_strategy(strategy_name)       — 启动策略
  4. get_strategy_indicators(id)         — 监控 PnL 和成交

B. 开发全新策略类型（完整流程）：
  1. read_strategy_reference("strategy_engine/strategy_plugin.h")   — 了解工厂接口（必读）
  2. read_strategy_reference("strategy/strategy_sma.h")              — 参考实现
  3. write_strategy_code(name, type_id, header, impl, param_schema) — 写入代码
  4. build_strategy_plugin(name, "backtest")                         — 编译回测 .so
  5. run_backtest(type, params, so_path=..., ...)                    — 验证回测
  6. get_backtest_results(run_id)                                    — 查看结果
  7. build_strategy_plugin(name, "live")                             — 编译实盘版
  8. create_strategy(type, name, params, so_path=...)                — 实盘部署

【注意事项】
- 新策略类型实盘部署前必须先通过回测
- list_strategies() 中 status=1 表示运行中，status=0 表示已停止
- build_strategy_plugin 失败时，根据 errors 字段修正代码后重试（最多 5 次）
- start/stop/restart 的 strategy_id 参数为策略名称（strat_name 字段）
""",
)

# ── 注册所有工具 ─────────────────────────────────────────────────────────────────
register_query_tools(mcp)
register_control_tools(mcp)
register_backtest_tools(mcp)
register_codegen_tools(mcp)

# ── MCP Resources（只读上下文，AI 可直接查阅无需调用工具） ──────────────────────

import yaml as _yaml


@mcp.resource("gtrade://strategy-types")
def strategy_types_resource() -> str:
    """
    所有策略类型的完整参数与指标定义。
    AI 创建策略前可直接查阅此资源，无需调用 list_strategy_types() 工具。
    """
    strategy_types = []
    for yml_file in sorted(STRATEGY_PARAM_DIR.glob("*.yml")):
        try:
            with open(yml_file, "r", encoding="utf-8") as f:
                data = _yaml.safe_load(f)
            strategy_types.append({
                "type_id": yml_file.stem,
                "params": data.get("param", []),
                "indicators": data.get("indicator", []),
            })
        except Exception:
            pass
    return json.dumps({"strategy_types": strategy_types}, ensure_ascii=False, indent=2)


@mcp.resource("gtrade://strategy-base-class")
def strategy_base_resource() -> str:
    """
    StrategyBase 抽象类头文件（src/strategy_engine/strategy_base.h）。
    定义了所有策略必须实现的生命周期接口和可用 API。
    """
    base_h = GTRADE_ROOT / "src/strategy_engine/strategy_base.h"
    if base_h.exists():
        return base_h.read_text(encoding="utf-8", errors="replace")
    return "# strategy_base.h not found"


@mcp.resource("gtrade://strategy-plugin-interface")
def strategy_plugin_resource() -> str:
    """
    .so 动态库工厂接口定义（src/strategy_engine/strategy_plugin.h）。
    AI 生成新策略代码时必须实现其中的 C 工厂函数。
    """
    plugin_h = GTRADE_ROOT / "src/strategy_engine/strategy_plugin.h"
    if plugin_h.exists():
        return plugin_h.read_text(encoding="utf-8", errors="replace")
    return "# strategy_plugin.h not found"


@mcp.resource("gtrade://active-strategies")
def active_strategies_resource() -> str:
    """
    当前所有策略实例列表（实时从 Flask web_server 获取）。
    status=1 表示运行中，status=0 表示已停止。
    """
    try:
        result = flask_client.get("/api/strategies")
        return json.dumps(result, ensure_ascii=False, indent=2)
    except Exception as e:
        return json.dumps({"error": str(e), "note": "Flask web_server 未运行或不可达"})


# ── 启动 ─────────────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="GTrade MCP Server")
    parser.add_argument(
        "--transport",
        choices=["stdio", "sse"],
        default="stdio",
        help="传输方式: stdio（本地，默认）或 sse（HTTP SSE，用于远程客户端）",
    )
    parser.add_argument(
        "--host",
        default=os.environ.get("MCP_HOST", "127.0.0.1"),
        help="SSE 绑定地址（仅 --transport sse 时有效，默认 127.0.0.1）",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=int(os.environ.get("MCP_PORT", "8765")),
        help="SSE 监听端口（仅 --transport sse 时有效，默认 8765）",
    )
    args = parser.parse_args()

    if args.transport == "sse":
        print(f"[gtrade-mcp] 启动 HTTP SSE 传输，监听 {args.host}:{args.port}", flush=True)
        mcp.run(transport="sse", host=args.host, port=args.port)
    else:
        mcp.run(transport="stdio")
