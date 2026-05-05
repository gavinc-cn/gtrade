"""
Python 策略子进程入口

由 SubprocessManager 通过以下命令启动：
  python3 -m gtrade_py.runner \
    --strat-id   <strat_id>  \
    --strat-config <yaml>    \
    --strat-class  <class>

主循环：
  1. 轮询 Channel C 控制命令（Start/Stop/Pause/Resume）
  2. 轮询 Channel A 行情帧并分发到策略回调
  3. 每 500ms 写一次心跳（与 C++ runner 保持一致）

SIGTERM 处理：
  注册 signal.SIGTERM，确保在进程退出前调用 on_stop()。

异常处理：
  策略回调中的异常被捕获并打印，不触发子进程重启（C++ runner 同策略）。
  runner 自身的异常（如 shm 打开失败）会让进程退出并触发 auto_restart。
"""

import argparse
import importlib.util
import signal
import sys
import time
import yaml

from .shm import StrategyShm
from .msg_id import MsgId
from .strategy_base import StrategyBase
from .structs import (
    Depth, KLine, Trade, Position, Balance, Order, TimerEventPush,
)

# ── 常量 ──────────────────────────────────────────────────────────────────────

# 心跳间隔：每循环约 50μs，10000 次 ≈ 500ms
_HEARTBEAT_LOOP_COUNT = 10_000

# Channel A 无数据时的休眠时长（秒）
_POLL_SLEEP_S = 50e-6  # 50μs


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog='gtrade_py.runner')
    parser.add_argument('--strat-id',     required=True,  help='策略 ID')
    parser.add_argument('--strat-config', required=True,  help='策略 YAML 配置文件路径')
    parser.add_argument('--strat-class',  required=True,  help='Python 策略类名')
    return parser.parse_args()


def _load_strategy_class(python_file: str, class_name: str):
    """从指定文件动态加载策略类。"""
    spec = importlib.util.spec_from_file_location('_user_strategy', python_file)
    if spec is None or spec.loader is None:
        raise ImportError(f'无法加载 python_file: {python_file}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    cls = getattr(module, class_name, None)
    if cls is None:
        raise AttributeError(f'策略文件 {python_file} 中未找到类 {class_name}')
    return cls


def _dispatch(strategy: StrategyBase, msg_type: int, payload: bytes) -> None:
    """将 Channel A 数据帧分发到对应的策略回调。"""
    if msg_type == MsgId.kDepth1:
        strategy.on_depth1(Depth.from_bytes(payload))

    elif msg_type == MsgId.kIndicatorKLineClosePush:
        strategy.on_kline_close(KLine.from_bytes(payload))

    elif msg_type == MsgId.kTradePush:
        strategy.on_trade_push(Trade.from_bytes(payload))

    elif msg_type == MsgId.kPositionPush:
        strategy.on_position_push(Position.from_bytes(payload))

    elif msg_type == MsgId.kBalancePush:
        strategy.on_balance_push(Balance.from_bytes(payload))

    elif msg_type == MsgId.kPlaceOrderConfirm:
        order = Order.from_bytes(payload)
        private_no = order.private_no.decode('utf-8', errors='replace').rstrip('\x00')
        status = order.status.decode('utf-8', errors='replace')
        strategy.on_place_order_confirm(private_no, order.entno, status)

    elif msg_type == MsgId.kTimerEvent:
        push = TimerEventPush.from_bytes(payload)
        strategy.on_timer(push.timer_id)

    # 其他消息类型暂时忽略（KLine open 等）


def main() -> None:
    args = _parse_args()

    # ── 读取 YAML 配置 ───────────────────────────────────────────────────────
    with open(args.strat_config, 'r', encoding='utf-8') as f:
        config = yaml.safe_load(f)

    python_file = config.get('python_file', '')
    if not python_file:
        print(f'[ERROR] python_file 字段不存在于 {args.strat_config}', flush=True)
        sys.exit(1)

    # ── 打开共享内存 ─────────────────────────────────────────────────────────
    print(f'[INFO] gtrade_py.runner: strat_id={args.strat_id} class={args.strat_class}',
          flush=True)
    shm = StrategyShm(args.strat_id)

    # ── 加载策略类并实例化 ───────────────────────────────────────────────────
    strategy_cls = _load_strategy_class(python_file, args.strat_class)
    strategy: StrategyBase = strategy_cls(shm, args.strat_id)

    # ── 注册 SIGTERM：确保 on_stop() 被调用后优雅退出 ────────────────────────
    def _sigterm_handler(signum, frame):
        print(f'[INFO] gtrade_py.runner: received SIGTERM, calling on_stop()', flush=True)
        try:
            strategy.on_stop()
        except Exception as e:
            print(f'[ERROR] on_stop() raised: {e}', flush=True)
        shm.close()
        sys.exit(0)

    signal.signal(signal.SIGTERM, _sigterm_handler)

    # ── 主循环 ───────────────────────────────────────────────────────────────
    started = False
    paused  = False
    hb_counter = 0

    while True:
        # 1. 检查 Channel C 控制命令（高优先级，每轮必查）
        ctrl = shm.read_ctrl()
        if ctrl == MsgId.kSubprocStratStart and not started:
            try:
                if strategy.on_init(config):
                    strategy.on_start()
                    started = True
                    print(f'[INFO] strategy {args.strat_id} started', flush=True)
                else:
                    print(f'[ERROR] strategy {args.strat_id} on_init() returned False',
                          flush=True)
            except Exception as e:
                print(f'[ERROR] on_init/on_start raised: {e}', flush=True)

        elif ctrl == MsgId.kSubprocStratStop:
            print(f'[INFO] strategy {args.strat_id} received Stop', flush=True)
            try:
                strategy.on_stop()
            except Exception as e:
                print(f'[ERROR] on_stop() raised: {e}', flush=True)
            break

        elif ctrl == MsgId.kSubprocStratPause and not paused:
            paused = True
            try:
                strategy.on_pause()
            except Exception as e:
                print(f'[ERROR] on_pause() raised: {e}', flush=True)

        elif ctrl == MsgId.kSubprocStratResume and paused:
            paused = False
            try:
                strategy.on_resume()
            except Exception as e:
                print(f'[ERROR] on_resume() raised: {e}', flush=True)

        # 2. 策略未启动时等待 Start 命令
        if not started:
            time.sleep(0.001)
            continue

        # 3. 读取 Channel A 行情帧（非阻塞）
        result = shm.try_read_data()
        if result is None:
            # 无数据，短暂休眠降低 CPU 占用
            time.sleep(_POLL_SLEEP_S)
        elif not paused:
            msg_type, payload = result
            try:
                _dispatch(strategy, msg_type, payload)
            except Exception as e:
                # 策略回调异常：记录日志，继续运行，不触发重启
                print(f'[ERROR] strategy callback exception (msg_type={msg_type}): {e}',
                      flush=True)

        # 4. 心跳（每 _HEARTBEAT_LOOP_COUNT 次循环约 500ms）
        hb_counter += 1
        if hb_counter >= _HEARTBEAT_LOOP_COUNT:
            shm.write_heartbeat()
            hb_counter = 0

    shm.close()
    print(f'[INFO] gtrade_py.runner: strat_id={args.strat_id} exited normally', flush=True)


if __name__ == '__main__':
    main()
