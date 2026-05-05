"""回测 Worker — 消费 Redis ar:backtest_queue，并行执行 gtrade_bt。

启动方式（可启多个实例）：
  python -m autoresearch.workers.backtest_worker
"""

import json
import signal
import redis
from autoresearch.config import get as cfg
from autoresearch.backtest.config_builder import build_backtest_config, build_strategy_yaml
from autoresearch.backtest.runner import run_backtest, BacktestFailedError, BacktestTimeoutError
from autoresearch.backtest.result_parser import find_done_csv, parse_done_csv
from autoresearch.backtest.evaluator import compute_metrics

QUEUE_IN   = "ar:backtest_queue"
QUEUE_OUT  = "ar:backtest_results"
QUEUE_DEAD = "ar:backtest_dead"
MAX_RETRIES = 3

_running = True


def _rdb() -> redis.Redis:
    r = cfg()["redis"]
    return redis.Redis(host=r["host"], port=r["port"], db=r["db"], decode_responses=True)


def _handle(task: dict) -> dict:
    """执行单个回测任务，返回带 metrics 和 status 的结果。"""
    try:
        strat_yaml = build_strategy_yaml(
            task["spec"], task["strategy_file"], task["class_name"], params=task["params"])
        bt_config, out_dir = build_backtest_config(strat_yaml, task["start_date"], task["end_date"])
        run_backtest(bt_config)
        done_csv = find_done_csv(out_dir)
        trades = parse_done_csv(done_csv) if done_csv else []
        return {**task, "metrics": compute_metrics(trades), "status": "success"}
    except Exception as e:
        return {**task, "metrics": {}, "status": "failed", "error": str(e)}


def run_worker() -> None:
    """启动 Worker 主循环，监听 Redis 队列处理回测任务。"""
    rdb = _rdb()
    print(f"[Worker] 启动，监听 {QUEUE_IN}")
    global _running
    signal.signal(signal.SIGTERM, lambda *_: globals().update(_running=False))
    signal.signal(signal.SIGINT,  lambda *_: globals().update(_running=False))

    while _running:
        item = rdb.brpop(QUEUE_IN, timeout=5)
        if item is None:
            continue
        task = json.loads(item[1])
        retries = task.get("_retries", 0)
        result = _handle(task)

        if result["status"] == "failed" and retries < MAX_RETRIES:
            task["_retries"] = retries + 1
            rdb.lpush(QUEUE_IN, json.dumps(task))   # 重新入队
            print(f"[Worker] trial={task.get('trial_id')} 失败，重试 {task['_retries']}/{MAX_RETRIES}")
        else:
            if result["status"] == "failed":
                rdb.lpush(QUEUE_DEAD, json.dumps(task))   # 死信队列
            rdb.lpush(QUEUE_OUT, json.dumps(result))
            print(f"[Worker] trial={task.get('trial_id')} {result['status']}")


if __name__ == "__main__":
    from autoresearch.config import load_config
    load_config()
    run_worker()
