"""
Phase 1 - 查询类工具（无副作用，只读）

工具列表：
  - list_strategy_types  列出所有可用策略类型及参数说明（读本地 YAML）
  - list_strategies      查询当前所有策略实例（对接 Flask /api/strategies）
  - get_strategy         查询单个策略详情（对接 Flask /api/strategies/{id}）
  - get_strategy_indicators 查询策略实时指标
"""

import json
import logging
from pathlib import Path
import yaml
from mcp.server.fastmcp import FastMCP

import flask_client
from config import STRATEGY_PARAM_DIR

logger = logging.getLogger(__name__)


def register_query_tools(mcp: FastMCP) -> None:
    """将所有查询工具注册到 MCP server 实例。"""

    # ── list_strategy_types ──────────────────────────────────────────────────────

    @mcp.tool()
    def list_strategy_types() -> str:
        """
        列出所有可用策略类型及其参数说明。
        创建策略前必须调用此工具，了解每种策略需要哪些参数。
        数据来自本地 src/strategy_param/*.yml 文件。
        """
        strategy_types = []

        for yml_file in sorted(STRATEGY_PARAM_DIR.glob("*.yml")):
            try:
                with open(yml_file, "r", encoding="utf-8") as f:
                    data = yaml.safe_load(f)

                type_id = yml_file.stem  # 文件名即策略类型 ID
                entry = {
                    "type_id": type_id,
                    "params": data.get("param", []),
                    "indicators": data.get("indicator", []),
                }
                strategy_types.append(entry)

            except Exception as e:
                logger.warning(f"读取策略参数文件失败 {yml_file}: {e}")

        return json.dumps(
            {"strategy_types": strategy_types},
            ensure_ascii=False,
            indent=2,
        )

    # ── list_strategies ──────────────────────────────────────────────────────────

    @mcp.tool()
    def list_strategies() -> str:
        """
        查询当前所有策略实例及其运行状态、参数和最新指标。
        数据来自 Flask web_server（/api/strategies）。
        status 字段: 0=停止, 1=运行中。
        """
        try:
            result = flask_client.get("/api/strategies")
            return json.dumps(result, ensure_ascii=False, indent=2)
        except Exception as e:
            return json.dumps({"error": str(e)}, ensure_ascii=False)

    # ── get_strategy ─────────────────────────────────────────────────────────────

    @mcp.tool()
    def get_strategy(strategy_id: int) -> str:
        """
        查询单个策略的完整详情，包含参数配置和当前指标。

        Args:
            strategy_id: 策略 ID（整数），可从 list_strategies 返回结果中获取。
        """
        try:
            result = flask_client.get(f"/api/strategies/{strategy_id}")
            return json.dumps(result, ensure_ascii=False, indent=2)
        except Exception as e:
            return json.dumps({"error": str(e)}, ensure_ascii=False)

    # ── get_strategy_indicators ──────────────────────────────────────────────────

    @mcp.tool()
    def get_strategy_indicators(strategy_id: int) -> str:
        """
        查询策略当前实时指标（PnL、持仓、成交等）。
        从策略详情中提取 indicator 字段。

        Args:
            strategy_id: 策略 ID（整数）。
        """
        try:
            result = flask_client.get(f"/api/strategies/{strategy_id}")
            # 提取关键字段
            indicator_data = {
                "strategy_id": strategy_id,
                "strat_name": result.get("strat_name"),
                "strat_template": result.get("strat_template"),
                "status": result.get("status"),
                "indicators": result.get("indicator", {}),
                "update_time": result.get("update_time"),
            }
            return json.dumps(indicator_data, ensure_ascii=False, indent=2)
        except Exception as e:
            return json.dumps({"error": str(e)}, ensure_ascii=False)
