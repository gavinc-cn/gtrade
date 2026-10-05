#!/bin/bash
# 一键重来: 停止 -> 清理共享内存 -> 启动 -> 状态检查（全部委托给 svc.sh）
set -eu
dir="$(dirname "$0")"
"$dir/svc.sh" stop
"$dir/svc.sh" clean
"$dir/svc.sh" start
"$dir/svc.sh" status
