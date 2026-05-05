"""autoresearch CLI — 投研系统命令行入口。"""

import click
from autoresearch.config import load_config


@click.group()
@click.option("--config", default=None, help="配置文件路径（默认读 autoresearch_config.yml）")
def cli(config):
    load_config(config)


@cli.command()
@click.option("--query", default="cryptocurrency momentum trading")
@click.option("--instrument", default="BTC-USDT-SWAP")
@click.option("--start", default="20240101_0000")
@click.option("--end", default="20241231_0000")
@click.option("--papers", default=3, type=int)
@click.option("--trials", default=20, type=int)
@click.option("--auto", is_flag=True, help="跳过人工审批（全自动）")
def run(query, instrument, start, end, papers, trials, auto):
    """运行完整投研 pipeline（通过 Prefect）。"""
    from autoresearch.pipeline.flows import research_pipeline
    research_pipeline(query=query, instrument=instrument,
                      start_date=start, end_date=end,
                      n_papers=papers, n_trials=trials,
                      require_approval=not auto)


@cli.command("list-strategies")
@click.option("--status", default=None, help="按状态过滤，如 live/pending_approval")
def list_strategies(status):
    """列出所有已发现的策略。"""
    from autoresearch.db import catalog
    catalog.init_db()
    for p in catalog.list_pipelines(status):
        click.echo(f"[{p['status']:16}] {p['pipeline_id'][:8]} {p['name'][:40]}")


@cli.command()
@click.argument("query")
@click.option("--n", default=5, type=int, help="返回论文数量")
def search(query, n):
    """在 arxiv 上搜索量化策略论文。"""
    from autoresearch.research.arxiv_fetcher import search_papers
    for p in search_papers(query, max_results=n):
        click.echo(f"\n{p['title']}\n  {p['source']} | {p['abstract'][:150]}...")


@cli.command()
@click.argument("strat_ids", nargs=-1, required=True)
def monitor(strat_ids):
    """启动实盘监控（回撤超阈值自动熔断）。"""
    from autoresearch.monitor.tracker import monitor_loop
    from autoresearch.monitor.circuit_breaker import pause_strategy
    monitor_loop(list(strat_ids), on_breach=pause_strategy)


@cli.command("dead-queue")
def dead_queue():
    """查看回测失败的死信队列。"""
    import json
    import redis
    from autoresearch.config import get as cfg
    r = cfg()["redis"]
    rdb = redis.Redis(host=r["host"], port=r["port"], db=r["db"], decode_responses=True)
    items = rdb.lrange("ar:backtest_dead", 0, 19)
    click.echo(f"死信队列 {len(items)} 个任务")
    for i in items:
        t = json.loads(i)
        click.echo(f"  pid={t.get('pipeline_id', '?')[:8]} retries={t.get('_retries', 0)}")


if __name__ == "__main__":
    cli()
