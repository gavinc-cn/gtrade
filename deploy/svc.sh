#!/bin/bash
# =====================================================================
# GTrade 服务管理入口（薄委托）—— 实现已迁移到同目录 svc.py（纯标准库 Python）
#
# 委托模式与 start.sh / stop.sh 等旧入口一致：exec 透传全部参数与退出码。
# 解释器选择与 svc.py 内 pick_python 相同: GTRADE_PYTHON > my_pyenv > python3 > python。
# 用法/命令/服务/退出码说明:  ./svc.sh help
# =====================================================================

# 解析软链接后的脚本目录。既可能是源码目录 deploy/，也可能是安装产物目录 <build>/bin/
SCRIPT_DIR=$(cd "$(dirname "$(realpath "$0")")" && pwd)

if [ -n "${GTRADE_PYTHON:-}" ]; then
    PY=${GTRADE_PYTHON}
elif [ -x /opt/miniconda3/envs/my_pyenv/bin/python ]; then
    PY=/opt/miniconda3/envs/my_pyenv/bin/python
elif command -v python3 >/dev/null 2>&1; then
    PY=python3
else
    PY=python
fi

exec "$PY" "$SCRIPT_DIR/svc.py" "$@"
