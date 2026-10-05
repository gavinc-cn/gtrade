#!/bin/bash
# 停止 gtrade 全部/指定服务 —— 已委托给统一管理脚本 svc.sh
# 停止顺序约束（gtrade 先于 repl 停，repl 需处理遗留 WAL）由 svc.sh 内置保证
exec "$(dirname "$0")/svc.sh" stop "$@"
