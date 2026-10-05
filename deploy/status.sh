#!/bin/bash
# 查看 gtrade 服务状态（pid / uptime / 端口）—— 已委托给统一管理脚本 svc.sh
exec "$(dirname "$0")/svc.sh" status "$@"
