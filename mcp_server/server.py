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
            "FLASK_BASE_URL": "http://127.0.0.1:46011",
            "HTTP_GW_BASE_URL": "http://127.0.0.1:46012"
          }
        }
      }
    }

环境变量（均有合理默认值）：
    GTRADE_ROOT           项目根目录（默认 /opt/win/gtrade）
    FLASK_BASE_URL        Flask web_server 地址（默认 http://127.0.0.1:46011）
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

from mcp.server.fastmcp import FastMCP

import flask_client
from config import GTRADE_ROOT, STRATEGY_PARAM_DIR
from tools.query_tools import register_query_tools
from tools.control_tools import register_control_tools
from tools.backtest_tools import register_backtest_tools
from tools.codegen_tools import register_codegen_tools
from tools.trade_tools import register_trade_tools

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

C. MCP 交易接口（实验性，直接操控引擎下单）：
  1. get_balances()                          — 确认可用资金
  2. get_depth(inst_id="BTC-USDT")           — 查看最新行情
  3. place_order(account_id, inst_id, ...)   — 下单，返回 order_id
  4. get_orders()                            — 轮询确认委托状态
  5. cancel_order(account_id, order_id)      — 撤单

注意：place_order 返回的 order_id 为本地编号，OKX 确认异步完成。
      td_mode: cash=现货, cross=全仓, isolated=逐仓
      ord_type: limit/market/post_only/fok/ioc
""",
)

# ── 注册所有工具 ─────────────────────────────────────────────────────────────────
register_query_tools(mcp)
register_control_tools(mcp)
register_backtest_tools(mcp)
register_codegen_tools(mcp)
register_trade_tools(mcp)

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
# 进程名按传输方式区分，两者必须不同，否则由 AI 工具/编辑器（Zed、Cursor 等）拉起的
# stdio 实例会被 deploy/svc.py 误判成它管理的 SSE 服务「运行中」，进而 start 静默跳过、
# stop 误杀编辑器进程、clean 被永久阻塞。根因与影响见
# doc_ai/bug_report_history/20261002_1036_svc状态误报mcp进程名冲突/。
# 改这两个名字必须同步 deploy/svc.py 的 SVC_PROC_NAME（见 doc_ai/spec/deploy/服务启停脚本.md 强关系）。
# Linux comm 限 15 字符，下面两个名字分别为 10 / 13 字符。
PROC_NAME_STDIO = "gtrade_mcp"   # stdio：由 AI 工具拉起，不归 svc.py 管理
PROC_NAME_SSE = "gtrade_mcpsse"  # sse：deploy/svc.py 管理的 mcp 服务（:8765）


def set_process_name(transport):
    """按传输方式设置进程名，便于 deploy/svc.sh 的 status/stop 按名称精确识别服务。

    transport 为 "sse" 时用 PROC_NAME_SSE，其余（stdio）用 PROC_NAME_STDIO。
    setproctitle 不是必须依赖，缺失时静默跳过（进程名保持解释器名，功能不受影响）。
    """
    try:
        import setproctitle
        setproctitle.setproctitle(PROC_NAME_SSE if transport == "sse" else PROC_NAME_STDIO)
    except ImportError:
        pass  # setproctitle 未安装，不设置进程名


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

    # 进程名依赖 --transport，必须在参数解析之后设置
    set_process_name(args.transport)

    if args.transport == "sse":
        print(f"[gtrade-mcp] 启动 HTTP SSE 传输，监听 {args.host}:{args.port}", flush=True)
        # mcp 1.30.0 的 run() 不接收 host/port 参数，需经 ServerSettings 设置
        mcp.settings.host = args.host
        mcp.settings.port = args.port
        mcp.run(transport="sse")
    else:
        mcp.run(transport="stdio")
