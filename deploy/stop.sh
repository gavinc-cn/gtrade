#!/bin/bash
# GTrade 服务停止脚本

set +e  # 允许命令失败继续执行

# 检查进程是否存在且非僵尸状态
# 僵尸进程(Z状态)已经死亡，无法被kill，应排除
is_process_alive() {
    local name="$1"
    ps -eo stat,comm 2>/dev/null | awk -v pat="$name" '$1 !~ /^Z/ && $2 == pat {found=1; exit} END {exit !found}'
}

echo "======================================"
echo "  停止 GTrade 服务"
echo "======================================"
echo ""

# 先停止 gtrade 主进程，让 gtrade_repl 处理剩余的 WAL
echo "正在停止 gtrade 主进程..."

if is_process_alive "gtrade_main"; then
    # 发送 SIGTERM 信号（优雅退出）
    killall -TERM "gtrade_main" && echo "  已发送 TERM 信号"

    # 等待进程退出（最多等待 10 秒）
    echo "  等待进程优雅退出..."
    for i in {1..10}; do
        sleep 1
        if ! is_process_alive "gtrade_main"; then
            echo "  ✓ gtrade 进程已优雅停止"
            break
        fi
        echo "  等待中... ($i/10 秒)"
    done

    # 如果还在运行，强制停止
    if is_process_alive "gtrade_main"; then
        echo "  进程未响应，强制停止..."
        killall -9 "gtrade_main" 2>/dev/null
        sleep 1
        echo "  ✓ gtrade 进程已强制停止"
    fi
else
    echo "  - gtrade 进程未运行"
fi

# 停止 gtrade_repl（WAL 持久化和复制进程）
# 注意：在 gtrade 停止后再停止 repl，确保剩余 WAL 被处理
echo "正在停止 gtrade_repl 进程..."

if is_process_alive "gtrade_repl"; then
    # 发送 SIGTERM 信号（优雅退出，会处理剩余 WAL）
    killall -TERM "gtrade_repl" && echo "  已发送 TERM 信号"

    # 等待进程退出（最多等待 10 秒）
    echo "  等待进程优雅退出..."
    for i in {1..10}; do
        sleep 1
        if ! is_process_alive "gtrade_repl"; then
            echo "  ✓ gtrade_repl 进程已优雅停止"
            break
        fi
        echo "  等待中... ($i/10 秒)"
    done

    # 如果还在运行，强制停止
    if is_process_alive "gtrade_repl"; then
        echo "  进程未响应，强制停止..."
        killall -9 "gtrade_repl" 2>/dev/null
        sleep 1
        echo "  ✓ gtrade_repl 进程已强制停止"
    fi
else
    echo "  - gtrade_repl 进程未运行"
fi

# 停止 web_server (Flask) - 通过进程名称
echo "正在停止 web_server (gtrade_websrv)..."
if is_process_alive "gtrade_websrv"; then
    killall -TERM "gtrade_websrv" 2>/dev/null && echo "  ✓ web_server 进程已停止"
    sleep 1
    killall -9 "gtrade_websrv" 2>/dev/null  # 清理残留
else
    echo "  - web_server 进程未运行"
fi

# 停止 web_client (Node.js) - 通过进程名称
echo "正在停止 web_client (gtrade_webcli)..."
if is_process_alive "gtrade_webcli"; then
    killall -TERM "gtrade_webcli" 2>/dev/null && echo "  ✓ web_client 进程已停止"
    sleep 1
    killall -9 "gtrade_webcli" 2>/dev/null  # 清理残留
else
    echo "  - web_client 进程未运行"
fi

# 停止 MCP Server (Python) - 通过进程名称
echo "正在停止 mcp_server (gtrade_mcp)..."
if is_process_alive "gtrade_mcp"; then
    killall -TERM "gtrade_mcp" 2>/dev/null && echo "  ✓ mcp_server 进程已停止"
    sleep 1
    killall -9 "gtrade_mcp" 2>/dev/null  # 清理残留
else
    echo "  - mcp_server 进程未运行"
fi

echo ""
echo "======================================"
echo "  停止完成"
echo "======================================"
echo ""
echo "使用 ./status.sh 检查服务状态"
echo ""