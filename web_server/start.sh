#!/bin/bash
# Web Server 开发启动脚本

echo "======================================"
echo "  启动 GTrade Web Server (开发模式)"
echo "======================================"
echo ""

# 设置环境变量
export FLASK_ENV=development
export FLASK_DEBUG=1

echo ""
echo "======================================"
echo "  服务器启动中..."
echo "======================================"
echo ""
echo "  URL: http://localhost:5000"
echo "  API: http://localhost:5000/api/health"
echo ""
echo "  按 Ctrl+C 停止服务器"
echo ""

# 启动服务器
python app.py
