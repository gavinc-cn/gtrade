#!/usr/bin/env bash
# GTrade 测试统一入口
# 用法：
#   test/run_tests.sh                        # 跑 unit 层（默认，秒级）
#   test/run_tests.sh --integration          # 跑 integration 层（发布前）
#   test/run_tests.sh --all                  # unit + integration
#   test/run_tests.sh --filter "OrderManager*"   # ctest -R 过滤
#   test/run_tests.sh --build-dir <dir>      # 指定构建目录（默认自动探测 build/ 或 cmake-build-*/）
# 输出：控制台 + <build>/test_reports/ 下的 JUnit XML
# 退出码：任何用例失败、或未匹配到用例，均为非零
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}/.."

LAYER="unit"
FILTER=""
BUILD_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --unit)        LAYER="unit"; shift ;;
        --integration) LAYER="integration"; shift ;;
        --all)         LAYER="all"; shift ;;
        --filter)      FILTER="$2"; shift 2 ;;
        --build-dir)   BUILD_DIR="$2"; shift 2 ;;
        -h|--help)     sed -n '2,10p' "$0"; exit 0 ;;
        *) echo "未知参数: $1" >&2; exit 2 ;;
    esac
done

# 探测构建目录
if [[ -z "${BUILD_DIR}" ]]; then
    if [[ -f build/CMakeCache.txt ]]; then
        BUILD_DIR="build"
    else
        BUILD_DIR="$(ls -d cmake-build-*/ 2>/dev/null | head -1 || true)"
    fi
fi
if [[ -z "${BUILD_DIR}" || ! -d "${BUILD_DIR}" ]]; then
    echo "未找到构建目录，创建 build/ (Debug, 测试开启)..."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DGTRADE_BUILD_TESTING=ON
    BUILD_DIR="build"
fi

# 老构建目录可能缓存了 GTRADE_BUILD_TESTING=OFF，强制打开
if ! grep -q "GTRADE_BUILD_TESTING:BOOL=ON" "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null; then
    cmake -S . -B "${BUILD_DIR}" -DGTRADE_BUILD_TESTING=ON
fi

# 编译测试目标
cmake --build "${BUILD_DIR}" --target unit_test integration_test -j"$(nproc)"

# 组装 ctest 参数
CTEST_ARGS=(--output-on-failure --no-tests=error)
case "${LAYER}" in
    unit)        CTEST_ARGS+=(-L unit) ;;
    integration) CTEST_ARGS+=(-L integration) ;;
    all)         ;;
esac
[[ -n "${FILTER}" ]] && CTEST_ARGS+=(-R "${FILTER}")

cd "${BUILD_DIR}"
ctest "${CTEST_ARGS[@]}"
echo "JUnit 报告目录: ${PWD}/test_reports/"
