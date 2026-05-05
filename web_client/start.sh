#!/bin/bash

# GTrade Web Client 启动脚本

# 获取本机 IP 地址
get_ip() {
    # 尝试获取主要网络接口的 IP
    ip=$(hostname -I | awk '{print $1}')
    if [ -z "$ip" ]; then
        ip="localhost"
    fi
    echo $ip
}

echo "=========================================="
echo "   GTrade Strategy Manager Web Client"
echo "=========================================="
echo ""

# 检查是否已安装依赖
if [ ! -d "node_modules" ]; then
    echo "首次运行，正在安装依赖..."
    npm install
    echo ""
fi

# 启动开发服务器
echo "正在启动开发服务器..."
echo ""

# 获取并显示访问地址
SERVER_IP=$(get_ip)

echo "服务器已启动，可通过以下地址访问："
echo ""
echo "  本机访问："
echo "    http://localhost:3000"
echo ""
echo "  其他主机访问："
echo "    http://${SERVER_IP}:3000"
echo ""
echo "按 Ctrl+C 停止服务器"
echo "=========================================="
echo ""

# 使用自定义启动脚本（设置进程名称为 gtrade_web_client）
# 创建日志目录并将输出重定向到日志文件
mkdir -p /tmp/gtrade
node start-vite.js 2>&1 | tee /tmp/gtrade/web_client.log
