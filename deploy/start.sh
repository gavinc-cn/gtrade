#!/bin/bash
# 启动 gtrade 全部/指定服务 —— 已委托给统一管理脚本 svc.sh
# 用法: ./start.sh [服务名...]（不带服务名 = 按依赖顺序启动全部）
exec "$(dirname "$0")/svc.sh" start "$@"
