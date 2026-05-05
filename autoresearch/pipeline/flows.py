"""Prefect Flow — 全流程编排。

优势：
- @task 自动重试，无需手写 try/except 循环
- 审批门控：flow 轮询 MySQL 状态，Flask 路由更新 DB 即可触发，无需 Prefect Server resume 信号
- Prefect Web UI (:4200) 可视化运行历史
- 支持定时触发（prefect deployment）
"""

from prefect import flow, task, get_run_logger
from prefect.tasks import task_input_hash

from autoresearch.config import get as cfg
from autoresearch.db import catalog
from autoresearch.research.arxiv_fetcher import search_papers
from autoresearch.research.strategy_extractor import extract_strategy_spec
from autoresearch.codegen.generator import generate_strategy_code, spec_to_class_name
from autoresearch.codegen.validator import validate_strategy_code, save_strategy, ValidationError
from autoresearch.data.okx_downloader import download_candles
from autoresearch.data.csv_writer import write_depth1_csv
from autoresearch.backtest.config_builder import build_backtest_config, build_strategy_yaml
from autoresearch.backtest.runner import run_backtest
from autoresearch.backtest.result_parser import find_done_csv, parse_done_csv
from autoresearch.backtest.evaluator import compute_metrics
from autoresearch.optimizer.optuna_optimizer import optimize
from autoresearch.promotion.gate import check_promotion
from autoresearch.promotion.deployer import deploy_to_live

import redis as _redis_lib

_CB_KEY = "ar:circuit_breaker:failure_count"


def _cb_rdb():
    r = cfg()["redis"]
    return _redis_lib.Redis(host=r["host"], port=r["port"], db=r["db"],
                            decode_responses=True)


def _record_failure():
    """用 Redis 原子计数器替代进程内全局变量，支持多 worker / 多进程共享熔断状态。"""
    rdb = _cb_rdb()
    count = rdb.incr(_CB_KEY)
    threshold = cfg()["circuit_breaker"]["consecutive_failures_threshold"]
    if count >= threshold:
        raise RuntimeError(f"[CircuitBreaker] 连续失败 {count} 次，停止 pipeline")


def _record_success():
    _cb_rdb().delete(_CB_KEY)


@task(retries=2, retry_delay_seconds=30, cache_key_fn=task_input_hash)
def fetch_papers_task(query: str, n_papers: int) -> list[dict]:
    papers = search_papers(query, max_results=n_papers,
                           categories=["q-fin.TR", "q-fin.CP", "cs.AI"])
    get_run_logger().info(f"找到 {len(papers)} 篇论文")
    return papers


@task(retries=2, retry_delay_seconds=60)
def extract_spec_task(paper: dict, instrument: str) -> dict | None:
    spec = extract_strategy_spec(paper["abstract"], source=paper["source"])
    if spec:
        spec["data_requirements"]["instrument"] = instrument
    return spec


@task(retries=3, retry_delay_seconds=30)
def generate_code_task(spec: dict) -> tuple[str, str]:
    class_name = spec_to_class_name(spec)
    code = generate_strategy_code(spec)
    validate_strategy_code(code, class_name)
    return code, class_name


@task(retries=2, retry_delay_seconds=60)
def download_data_task(instrument: str, start_date: str, end_date: str) -> int:
    candles = download_candles(instrument, "1H", start_date, end_date)
    write_depth1_csv(candles, cfg()["paths"]["csv_quote_base_dir"], instrument)
    return len(candles)


@task
def optimize_task(pipeline_id: str, spec: dict, strategy_file: str,
                  class_name: str, start_date: str, end_date: str,
                  n_trials: int) -> tuple[dict, float]:
    return optimize(pipeline_id, spec, strategy_file, class_name,
                    start_date, end_date, n_trials=n_trials)


@task
def full_backtest_task(spec: dict, strategy_file: str, class_name: str,
                       params: dict, start_date: str, end_date: str) -> dict:
    strat_yaml = build_strategy_yaml(spec, strategy_file, class_name, params=params)
    bt_config, out_dir = build_backtest_config(strat_yaml, start_date, end_date)
    run_backtest(bt_config)
    done_csv = find_done_csv(out_dir)
    return compute_metrics(parse_done_csv(done_csv) if done_csv else [])


@flow(name="single-strategy", log_prints=True)
def single_strategy_flow(paper: dict, instrument: str, start_date: str,
                         end_date: str, n_trials: int,
                         require_approval: bool) -> str | None:
    logger = get_run_logger()

    spec = extract_spec_task(paper, instrument)
    if not spec:
        logger.warning("无法提取策略规格，跳过")
        return None

    try:
        code, class_name = generate_code_task(spec)
    except Exception as e:
        _record_failure()
        logger.error(f"代码生成失败: {e}")
        return None

    strategy_file = save_strategy(code, class_name, cfg()["paths"]["strategy_dir"])
    pipeline_id = catalog.add_pipeline(spec["title"], paper["source"], spec, strategy_file)
    catalog.update_status(pipeline_id, "validated")

    try:
        n = download_data_task(instrument, start_date, end_date)
        logger.info(f"下载 {n} 条 K 线")
        catalog.update_status(pipeline_id, "data_ready")
    except Exception as e:
        catalog.update_status(pipeline_id, "failed")
        return None

    catalog.update_status(pipeline_id, "optimizing")
    try:
        best_params, oos_sharpe = optimize_task(
            pipeline_id, spec, strategy_file, class_name,
            start_date, end_date, n_trials)
        catalog.save_best_params(pipeline_id, best_params, oos_sharpe,
                                 f"study_{class_name}")
    except Exception as e:
        logger.error(f"优化失败，使用默认参数: {e}")
        best_params, oos_sharpe = spec.get("params", {}), 0.0

    catalog.update_status(pipeline_id, "backtesting")
    try:
        metrics = full_backtest_task(
            spec, strategy_file, class_name, best_params, start_date, end_date)
        catalog.add_run(pipeline_id, best_params, start_date, end_date, metrics)
        _record_success()
    except Exception as e:
        _record_failure()
        catalog.update_status(pipeline_id, "failed")
        return None

    promo = check_promotion(metrics, oos_sharpe)
    logger.info(str(promo))
    if not promo.passed:
        catalog.update_status(pipeline_id, "rejected")
        return None

    catalog.update_status(pipeline_id, "pending_approval")

    # 审批门控：轮询 MySQL 状态，而非 pause_flow_run()。
    # 原因：pause_flow_run 需 Prefect Server 发送 resume 信号，
    #       而 Flask approve 路由只更新 MySQL，无法触发 resume，flow 会永久阻塞。
    # 轮询方案更简单：flow 自身等待 DB 状态变为 approved/rejected，
    #       Flask 路由只需更新 DB，不依赖 Prefect Client API。
    if require_approval:
        import time
        logger.info("等待人工审批 — 请访问 http://localhost:46011/research/approval")
        while True:
            current = catalog.get_pipeline(pipeline_id)
            if not current or current["status"] == "rejected":
                logger.info("审批被拒绝，跳过上线")
                return None
            if current["status"] == "approved":
                break
            time.sleep(10)   # 每10秒检查一次，Web 操作后很快响应

    catalog.update_status(pipeline_id, "approved")
    strat_id = f"auto_{class_name.lower()}"
    strat_yaml = build_strategy_yaml(spec, strategy_file, class_name, params=best_params)
    try:
        _, live_yml = deploy_to_live(strategy_file, open(strat_yaml).read(), strat_id)
        catalog.add_deployment(pipeline_id, strat_id, live_yml)
        catalog.update_status(pipeline_id, "live")
        logger.info(f"策略已上线: {live_yml}")
        return strat_id
    except Exception as e:
        catalog.update_status(pipeline_id, "failed")
        return None


@flow(name="research-pipeline", log_prints=True)
def research_pipeline(
    query: str = "cryptocurrency momentum trading strategy",
    instrument: str = "BTC-USDT-SWAP",
    start_date: str = "20240101_0000",
    end_date: str = "20241231_0000",
    n_papers: int = 3,
    n_trials: int = 20,
    require_approval: bool = True,
) -> list[str]:
    catalog.init_db()
    papers = fetch_papers_task(query, n_papers)
    deployed = []
    for paper in papers:
        strat_id = single_strategy_flow(
            paper=paper, instrument=instrument,
            start_date=start_date, end_date=end_date,
            n_trials=n_trials, require_approval=require_approval,
        )
        if strat_id:
            deployed.append(strat_id)
    print(f"共上线 {len(deployed)} 个策略: {deployed}")
    return deployed
