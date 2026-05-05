"""运行 gtrade_bt 进程，带超时控制。"""

import subprocess
import os
from autoresearch.config import get as cfg


class BacktestTimeoutError(Exception):
    pass


class BacktestFailedError(Exception):
    pass


def run_backtest(backtest_config_path: str) -> str:
    """运行 gtrade_bt，返回 stdout 输出。

    Raises:
        FileNotFoundError: gtrade_bt 二进制不存在
        BacktestTimeoutError: 超时
        BacktestFailedError: 非零退出码
    """
    bin_path = cfg()["paths"]["gtrade_bt_bin"]
    timeout = cfg()["backtest"]["timeout_seconds"]

    if not os.path.exists(bin_path):
        raise FileNotFoundError(f"gtrade_bt 不存在: {bin_path}")

    try:
        result = subprocess.run(
            [bin_path, "--config", backtest_config_path],
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        raise BacktestTimeoutError(f"回测超时 ({timeout}s)")

    if result.returncode != 0:
        raise BacktestFailedError(
            f"gtrade_bt 返回 {result.returncode}\n"
            f"stdout: {result.stdout[-2000:]}\n"
            f"stderr: {result.stderr[-2000:]}"
        )
    return result.stdout
