#!/bin/bash

# 检查进程是否运行
if ! pgrep -f gtrade_scheduler > /dev/null; then
    echo "Process not running"
    exit 1
fi

# 检查最新日志文件时间戳
LOG_DIR="/opt/gtrade/scripts/log"
if [ -d "$LOG_DIR" ]; then
    # 查找最新的日志文件
    LATEST_LOG=$(find "$LOG_DIR" -name "*.log" -type f -printf '%T@ %p\n' | sort -n | tail -1 | cut -d' ' -f2-)

    if [ -n "$LATEST_LOG" ]; then
        # 获取文件的修改时间戳
        FILE_TIME=$(stat -c %Y "$LATEST_LOG")
        CURRENT_TIME=$(date +%s)
        TIME_DIFF=$((CURRENT_TIME - FILE_TIME))

        # 检查是否在1分钟内（60秒）
        if [ $TIME_DIFF -gt 60 ]; then
            echo "Log file too old: $TIME_DIFF seconds"
            exit 1
        fi
    else
        echo "No log files found"
        exit 1
    fi
else
    echo "Log directory not found"
    exit 1
fi

echo "Health check passed"
exit 0