"""Optuna 优化器 — 批量提交 trial 到 Redis，等待并行 Worker 结果后汇报。

相比串行版本，N 个 Worker 可同时执行 N 个 trial，优化速度提升 N 倍。
"""

import json
import time
import uuid
import optuna
import redis
from autoresearch.config import get as cfg
from autoresearch.optimizer.walk_forward import generate_windows
from autoresearch.workers.backtest_worker import QUEUE_IN, QUEUE_OUT

optuna.logging.set_verbosity(optuna.logging.WARNING)
_RESULT_TIMEOUT = 600   # 单个 trial 最长等待秒数


def _rdb() -> redis.Redis:
    r = cfg()["redis"]
    return redis.Redis(host=r["host"], port=r["port"], db=r["db"], decode_responses=True)


def _submit(rdb, pipeline_id, trial_id, spec, strategy_file, class_name,
            params, start_date, end_date) -> None:
    """将一个回测任务推入 Redis 队列。"""
    rdb.lpush(QUEUE_IN, json.dumps({
        "pipeline_id": pipeline_id, "trial_id": trial_id,
        "spec": spec, "strategy_file": strategy_file, "class_name": class_name,
        "params": params, "start_date": start_date, "end_date": end_date, "_retries": 0,
    }))


def _wait(rdb, trial_ids: set, timeout: int = _RESULT_TIMEOUT) -> dict:
    """等待指定 trial_id 集合的结果，超时视为失败。"""
    results, pending = {}, set(trial_ids)
    deadline = time.time() + timeout
    while pending and time.time() < deadline:
        item = rdb.brpop(QUEUE_OUT, timeout=5)
        if item is None:
            continue
        r = json.loads(item[1])
        tid = r.get("trial_id")
        if tid in pending:
            results[tid] = r
            pending.discard(tid)
    for tid in pending:   # 超时视为失败
        results[tid] = {"trial_id": tid, "metrics": {}, "status": "timeout"}
    return results


def optimize(pipeline_id: str, spec: dict, strategy_file: str, class_name: str,
             start_date: str, end_date: str,
             n_trials: int = 30, n_windows: int = 3) -> tuple[dict, float]:
    """运行 Optuna 优化，返回 (best_params, best_oos_sharpe)。"""
    param_ranges = spec.get("param_ranges", {})
    if not param_ranges:
        return spec.get("params", {}), 0.0

    rdb = _rdb()
    windows = generate_windows(start_date, end_date, n_windows=n_windows)
    batch_size = min(cfg()["backtest"]["max_concurrent_workers"], n_trials)

    # storage 持久化：进程崩溃后 trial 历史不丢失；优先用 MySQL，兼容 SQLite
    m = cfg()["mysql"]
    _storage = (f"mysql+pymysql://{m['user']}:{m['password']}"
                f"@{m['host']}:{m['port']}/{m['db']}")
    study = optuna.create_study(
        study_name=f"study_{class_name}",
        direction="maximize",
        sampler=optuna.samplers.TPESampler(seed=42),
        storage=_storage,
        load_if_exists=True,   # 重启后接续上次进度
    )
    submitted = 0

    while submitted < n_trials:
        # 采样一批 trial
        batch = []
        for _ in range(min(batch_size, n_trials - submitted)):
            trial = study.ask()
            params = {}
            for name, (lo, hi) in param_ranges.items():
                params[name] = (trial.suggest_int(name, lo, hi)
                                if isinstance(lo, int) and isinstance(hi, int)
                                else trial.suggest_float(name, lo, hi))
            batch.append((trial, params))

        # 每个 trial × 每个测试窗口 = 一个 Redis 任务
        tid_map: dict[str, tuple[int, dict]] = {}
        for trial, params in batch:
            for i, win in enumerate(windows):
                tid = f"{trial.number}_{i}_{uuid.uuid4().hex[:6]}"
                tid_map[tid] = (trial.number, params)
                _submit(rdb, pipeline_id, tid, spec, strategy_file, class_name,
                        params, win["test_start"], win["test_end"])

        results = _wait(rdb, set(tid_map))

        # 按 trial 聚合 OOS Sharpe 后汇报给 Optuna
        trial_sharpes: dict[int, list[float]] = {}
        for tid, r in results.items():
            tno, _ = tid_map[tid]
            sh = r["metrics"].get("sharpe", -10.0) if r["status"] == "success" else -10.0
            trial_sharpes.setdefault(tno, []).append(sh)

        for trial, _ in batch:
            sharpes = trial_sharpes.get(trial.number, [-10.0])
            study.tell(trial, sum(sharpes) / len(sharpes))

        submitted += len(batch)
        print(f"[Optimizer] {submitted}/{n_trials} trials，"
              f"当前最优 OOS Sharpe: {study.best_value:.2f}")

    return study.best_params, study.best_value
