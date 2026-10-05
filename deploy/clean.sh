#!/bin/bash
# 清理 /dev/shm 共享内存 —— 已委托给统一管理脚本 svc.sh
# 安全约束（所有服务停止后才允许清理）由 svc.sh 内置保证
exec "$(dirname "$0")/svc.sh" clean
