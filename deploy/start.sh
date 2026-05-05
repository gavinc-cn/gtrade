set -eu

cd "$(dirname "$0")" && pwd

ulimit -c unlimited

echo ""
echo "======================================"
echo "  启动服务"
echo "======================================"
echo ""

project_dir=$(pwd)/../..
echo -- project_dir: $project_dir

# 启动 gtrade 主进程
nohup ./gtrade &>gtrade.out &
echo -- gtrade started

# 等待共享内存 WAL 初始化
sleep 1

# 启动 gtrade_repl（WAL 持久化和复制进程）
# 可根据需要调整参数：
#   --file-wal <dir>   文件 WAL 目录
#   --peer <addr>      备机地址
#   --port <port>      备机端口
#   --no-file-wal      禁用文件 WAL（仅网络复制）
if [ -x "./gtrade_repl" ]; then
    nohup ./gtrade_repl --file-wal ./wal &>gtrade_repl.out &
    echo -- gtrade_repl started
else
    echo -- gtrade_repl not found, skipping
fi

cd ${project_dir}/web_server || exit 1
nohup bash start.sh &>web_server.out &
echo -- web_server started

cd ${project_dir}/web_client || exit 1
nohup bash start.sh &>web_client.out &
echo -- web_client started

# 启动 MCP Server（HTTP SSE 模式，供 AI 客户端远程连接）
cd ${project_dir}/mcp_server || exit 1
nohup python server.py --transport sse --host 127.0.0.1 --port 8765 &>mcp_server.out &
echo -- mcp_server started

echo ""
echo "======================================"
echo "  启动完成"
echo "======================================"
echo ""