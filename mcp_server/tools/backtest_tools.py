"""
Phase 3 - 回测集成工具

工具列表：
  - run_backtest           启动回测（后台运行 gtrade_bt，返回 run_id）
  - get_backtest_status    查询回测是否完成
  - get_backtest_results   读取回测结果（委托/成交 CSV + 日志）

输出文件约定（来自 src/backtest/dummy_trade.h）：
  {out_dir}/{start_date}_{end_date}_entrust_{ts}.csv  委托记录
  {out_dir}/{start_date}_{end_date}_done_{ts}.csv     成交记录
  {out_dir}/validator_result.txt                      探针策略验证结果（PASS/FAIL）
  {run_dir}/backtest.log                              回测日志
"""

import csv
import json
import logging
import subprocess
import time
from datetime import datetime
from pathlib import Path

import yaml
from mcp.server.fastmcp import FastMCP

from config import BACKTEST_BIN, BACKTEST_TEMP_DIR, STRATEGY_CONFIG_DIR

logger = logging.getLogger(__name__)

# 进行中的回测进程表：{run_id: subprocess.Popen}
_running_processes: dict[str, subprocess.Popen] = {}


def _make_run_id() -> str:
    """生成唯一 run_id（当前 UTC 时间戳）。"""
    return datetime.utcnow().strftime("%Y%m%d_%H%M%S")


def _run_dir(run_id: str) -> Path:
    return BACKTEST_TEMP_DIR / run_id


def register_backtest_tools(mcp: FastMCP) -> None:
    """将所有回测工具注册到 MCP server 实例。"""

    # ── run_backtest ─────────────────────────────────────────────────────────────

    @mcp.tool()
    def run_backtest(
        strategy_type: str,
        strategy_params: dict,
        start_date: str,
        end_date: str,
        csv_base_dir: str = "/root/data_repo",
        fill_mode: str = "i",
        so_path: str | None = None,
        account_config: str = "/etc/secret.yml",
        db_config: str = "/etc/secret.db.yml",
    ) -> str:
        """
        启动一次回测，后台运行 gtrade_bt，立即返回 run_id。
        使用 get_backtest_status(run_id) 轮询完成状态，
        再用 get_backtest_results(run_id) 获取结果。

        Args:
            strategy_type:   策略类型 ID，如 "SpotGrid"。
            strategy_params: 策略参数（与 create_strategy 格式相同）。
            start_date:      回测开始，格式 "YYYYmmdd_HHMM"，如 "20260101_0000"。
            end_date:        回测结束，格式 "YYYYmmdd_HHMM"，如 "20260201_0000"。
            csv_base_dir:    CSV 行情数据目录，默认 "/root/data_repo"。
            fill_mode:       成交模式，"i"=即时成交（默认），"s"=模拟撮合。
            so_path:         （可选）.so 动态库路径，AI 生成的新策略需要此参数。
            account_config:  账户配置文件路径，默认 "/etc/secret.yml"。
            db_config:       数据库配置文件路径，默认 "/etc/secret.db.yml"。
        """
        if not BACKTEST_BIN.exists():
            return json.dumps(
                {
                    "success": False,
                    "error": f"gtrade_bt 未找到: {BACKTEST_BIN}\n请先编译项目。",
                },
                ensure_ascii=False,
            )

        run_id = _make_run_id()
        run_dir = _run_dir(run_id)
        out_dir = run_dir / "output"
        run_dir.mkdir(parents=True, exist_ok=True)
        out_dir.mkdir(parents=True, exist_ok=True)

        # 生成策略配置 YAML
        strat_cfg_path = run_dir / "strat_config.yml"
        strat_cfg: dict = {
            "strat_template_id": strategy_type,
            "logger": {"async": False, "log_level": "info,error"},
        }
        if so_path:
            strat_cfg["so_path"] = so_path
        strat_cfg.update(strategy_params)
        strat_cfg_path.write_text(
            yaml.dump(strat_cfg, allow_unicode=True, default_flow_style=False),
            encoding="utf-8",
        )

        # 生成回测主配置 YAML
        bt_cfg_path = run_dir / "backtest_config.yml"
        bt_cfg = {
            "strategy_config": [str(strat_cfg_path)],
            "account_config": account_config,
            "db_config": db_config,
            "logger": {
                "log_file": str(run_dir / "backtest.log"),
                "async": False,
                "show_level": "info",
                "log_level": "info,error",
            },
            "query_processor_num": 2,
            "start_date": start_date,
            "end_date": end_date,
            "backtest_rate": 1,
            "backtest_interval": 1,
            "csv_quote_base_dir": csv_base_dir,
            "backtest_out_dir": str(out_dir),
            "fill_mode": fill_mode,
        }
        bt_cfg_path.write_text(
            yaml.dump(bt_cfg, allow_unicode=True, default_flow_style=False),
            encoding="utf-8",
        )

        # 在后台启动 gtrade_bt
        try:
            proc = subprocess.Popen(
                [str(BACKTEST_BIN), "--config", str(bt_cfg_path)],
                stdout=open(run_dir / "stdout.log", "w"),
                stderr=open(run_dir / "stderr.log", "w"),
            )
            _running_processes[run_id] = proc
            logger.info(f"回测已启动，run_id={run_id}, pid={proc.pid}")
        except Exception as e:
            return json.dumps(
                {"success": False, "error": f"启动 gtrade_bt 失败: {e}"},
                ensure_ascii=False,
            )

        return json.dumps(
            {
                "success": True,
                "run_id": run_id,
                "status": "running",
                "pid": proc.pid,
                "run_dir": str(run_dir),
                "message": f"回测已启动，使用 get_backtest_status('{run_id}') 查询进度。",
            },
            ensure_ascii=False,
            indent=2,
        )

    # ── get_backtest_status ──────────────────────────────────────────────────────

    @mcp.tool()
    def get_backtest_status(run_id: str) -> str:
        """
        查询回测运行状态。

        Args:
            run_id: 由 run_backtest() 返回的唯一标识符。

        Returns:
            status: "running" | "completed" | "failed" | "not_found"
        """
        run_dir = _run_dir(run_id)
        if not run_dir.exists():
            return json.dumps(
                {"run_id": run_id, "status": "not_found"}, ensure_ascii=False
            )

        proc = _running_processes.get(run_id)
        if proc is not None:
            ret = proc.poll()
            if ret is None:
                # 仍在运行
                return json.dumps(
                    {"run_id": run_id, "status": "running", "pid": proc.pid},
                    ensure_ascii=False,
                )
            # 已结束，从表中移除
            del _running_processes[run_id]
            status = "completed" if ret == 0 else "failed"
            return json.dumps(
                {"run_id": run_id, "status": status, "exit_code": ret},
                ensure_ascii=False,
                indent=2,
            )

        # 进程不在内存表（MCP server 重启后），通过目录判断
        # 若有 output 目录且不为空，视为已完成
        out_dir = run_dir / "output"
        if out_dir.exists() and any(out_dir.iterdir()):
            return json.dumps(
                {"run_id": run_id, "status": "completed", "note": "进程已不在内存，通过文件判断"},
                ensure_ascii=False,
                indent=2,
            )

        return json.dumps(
            {"run_id": run_id, "status": "unknown", "note": "无法确定状态"},
            ensure_ascii=False,
        )

    # ── get_backtest_results ─────────────────────────────────────────────────────

    @mcp.tool()
    def get_backtest_results(run_id: str) -> str:
        """
        读取回测结果，返回委托/成交统计和日志摘要。
        请先调用 get_backtest_status(run_id) 确认状态为 "completed"。

        Args:
            run_id: 由 run_backtest() 返回的唯一标识符。
        """
        run_dir = _run_dir(run_id)
        if not run_dir.exists():
            return json.dumps(
                {"success": False, "error": f"run_id={run_id} 不存在"},
                ensure_ascii=False,
            )

        out_dir = run_dir / "output"
        result: dict = {"run_id": run_id, "success": True}

        # ── 验证结果（validator_result.txt）────────────────────────────────────
        validator_file = out_dir / "validator_result.txt"
        if validator_file.exists():
            result["validator_result"] = validator_file.read_text(encoding="utf-8").strip()
        else:
            result["validator_result"] = None

        # ── 成交记录（done_*.csv）─────────────────────────────────────────────
        done_files = sorted(out_dir.glob("*_done_*.csv"))
        done_summary = _summarize_done_csv(done_files)
        result["done_summary"] = done_summary

        # ── 委托记录（entrust_*.csv）─────────────────────────────────────────
        entrust_files = sorted(out_dir.glob("*_entrust_*.csv"))
        result["entrust_count"] = _count_csv_rows(entrust_files)

        # ── 输出文件列表 ────────────────────────────────────────────────────────
        result["output_files"] = [str(f.name) for f in sorted(out_dir.iterdir())]

        # ── 日志末 100 行 ───────────────────────────────────────────────────────
        log_file = run_dir / "backtest.log"
        if log_file.exists():
            lines = log_file.read_text(encoding="utf-8", errors="replace").splitlines()
            result["log_tail"] = "\n".join(lines[-100:])
        else:
            stderr_file = run_dir / "stderr.log"
            if stderr_file.exists():
                lines = stderr_file.read_text(encoding="utf-8", errors="replace").splitlines()
                result["log_tail"] = "\n".join(lines[-50:])

        return json.dumps(result, ensure_ascii=False, indent=2)


# ── 辅助函数 ─────────────────────────────────────────────────────────────────────

def _count_csv_rows(files: list[Path]) -> int:
    """统计 CSV 文件总行数（不含表头）。"""
    total = 0
    for f in files:
        try:
            with open(f, "r", encoding="utf-8") as fh:
                total += sum(1 for line in fh) - 1  # 减去表头
        except Exception:
            pass
    return max(total, 0)


def _summarize_done_csv(files: list[Path]) -> dict:
    """
    汇总成交记录 CSV，计算总成交笔数、买/卖数量。
    CSV 列名以实际文件为准，此处只做行数统计以保证兼容性。
    """
    total_rows = 0
    buy_count = 0
    sell_count = 0

    for f in files:
        try:
            with open(f, "r", encoding="utf-8", newline="") as fh:
                reader = csv.DictReader(fh)
                for row in reader:
                    total_rows += 1
                    side = (row.get("side") or row.get("Side") or "").lower()
                    if side in ("buy", "b"):
                        buy_count += 1
                    elif side in ("sell", "s"):
                        sell_count += 1
        except Exception as e:
            logger.warning(f"解析成交 CSV 失败 {f}: {e}")

    return {
        "total_trades": total_rows,
        "buy_count": buy_count,
        "sell_count": sell_count,
        "files": [f.name for f in files],
    }
