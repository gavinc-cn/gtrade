#!/bin/bash

# 启动Redis（用于HA Demo）

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA_DIR="${SCRIPT_DIR}/../data"

echo "========================================="
echo "Starting Redis for HA Demo"
echo "========================================="

# 检查Redis是否已安装
if ! command -v redis-server &> /dev/null; then
    echo "Redis not found. Trying to start with Docker..."

    # 使用Docker启动Redis
    docker run -d \
        --name ha_demo_redis \
        -p 6379:6379 \
        redis:7-alpine

    if [ $? -eq 0 ]; then
        echo "Redis started in Docker (container: ha_demo_redis)"
        echo "Redis is available at localhost:6379"
    else
        echo "Failed to start Redis. Please install Redis or Docker."
        exit 1
    fi
else
    # 检查Redis是否已在运行
    if redis-cli ping &> /dev/null; then
        echo "Redis is already running"
    else
        echo "Starting Redis server..."

        # 创建数据目录
        mkdir -p "$DATA_DIR"

        # 启动Redis
        redis-server --daemonize yes --dir "$DATA_DIR"

        if [ $? -eq 0 ]; then
            echo "Redis started successfully"
        else
            echo "Failed to start Redis"
            exit 1
        fi
    fi
fi

# 验证连接
echo ""
echo "Testing connection..."
redis-cli ping

echo ""
echo "Redis is ready!"
echo "========================================="
