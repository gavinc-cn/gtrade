#!/bin/bash
# GTrade 服务状态检查脚本

set +e  # 允许命令失败继续执行

echo "======================================"
echo "  GTrade 服务状态"
echo "======================================"
echo ""

# 显示所有相关进程（详细信息）
ps -ef | grep -E "gtrade|app\.py|vite|npm.*dev" | grep -v "grep" | grep -v "status.sh" | grep -v "<defunct>" || echo "  无相关进程"
echo ""

# 获取非僵尸进程的 PID（排除状态为 Z 的僵尸进程）
get_running_pids() {
    local pattern="$1"
    ps -eo pid,stat,comm 2>/dev/null | awk -v pat="$pattern" '$2 !~ /^Z/ && $3 == pat {print $1}'
}

# 检查 gtrade 进程（进程名: gtrade_main），排除僵尸进程
GTRADE_PIDS=$(get_running_pids "gtrade_main")
if [ -n "$GTRADE_PIDS" ]; then
    echo "  ✓ 运行中 【gtrade 主进程】"
else
    echo "  ✗ 未运行 【gtrade 主进程】"
fi

# 检查 gtrade_repl 进程（WAL 持久化和复制），排除僵尸进程
REPL_PIDS=$(get_running_pids "gtrade_repl")
if [ -n "$REPL_PIDS" ]; then
    echo "  ✓ 运行中 【gtrade_repl (WAL复制)】"
else
    echo "  ✗ 未运行 【gtrade_repl (WAL复制)】"
fi

# 检查 web_server 进程 (进程名: gtrade_websrv)，排除僵尸进程
WEB_SERVER_PIDS=$(get_running_pids "gtrade_websrv")
if [ -n "$WEB_SERVER_PIDS" ]; then
    echo "  ✓ 运行中 【web_server (Flask)】"
else
    echo "  ✗ 未运行 【web_server (Flask)】"
fi

# 检查 web_client 进程 (进程名: gtrade_webcli)，排除僵尸进程
WEB_CLIENT_PIDS=$(get_running_pids "gtrade_webcli")
if [ -n "$WEB_CLIENT_PIDS" ]; then
    echo "  ✓ 运行中 【web_client (Vite)】"
else
    echo "  ✗ 未运行 【web_client (Vite)】"
fi

# 检查 MCP Server 进程 (进程名: gtrade_mcp)，排除僵尸进程
MCP_PIDS=$(get_running_pids "gtrade_mcp")
if [ -n "$MCP_PIDS" ]; then
    echo "  ✓ 运行中 【mcp_server (MCP SSE :8765)】"
else
    echo "  ✗ 未运行 【mcp_server (MCP SSE :8765)】"
fi
echo ""

echo "======================================"