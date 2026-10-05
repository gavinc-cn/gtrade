#!/bin/bash
# 先停后起 gtrade 全部/指定服务 —— 已委托给统一管理脚本 svc.sh
exec "$(dirname "$0")/svc.sh" restart "$@"
