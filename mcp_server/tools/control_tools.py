"""
Phase 2 - 控制类工具（有副作用：写 DB、写文件、操作引擎）

工具列表：
  - create_strategy   创建策略实例（生成 YAML + Flask CRUD + C++ 热加载）
  - update_strategy   更新策略参数
  - start_strategy    启动策略（C++ 引擎）
  - stop_strategy     停止策略（C++ 引擎）
  - restart_strategy  重启策略（C++ 引擎）
  - delete_strategy   删除策略（引擎卸载 + DB 删除 + YAML 清理）
"""

import json
import logging
from pathlib import Path
import yaml
from mcp.server.fastmcp import FastMCP

import flask_client
import gw_client
from config import STRATEGY_CONFIG_DIR

logger = logging.getLogger(__name__)


def _generate_strategy_yaml(
    strategy_name: str,
    strategy_type: str,
    params: dict,
    so_path: str | None = None,
) -> str:
    """
    根据策略名称、类型和参数生成 YAML 配置内容。
    若指定 so_path，则生成动态库加载方式的配置（无需静态注册）。
    """
    cfg: dict = {
        "strat_template_id": strategy_type,
        "logger": {"async": False, "log_level": "trace,info,error"},
    }
    if so_path:
        cfg["so_path"] = so_path
    cfg.update(params)
    return yaml.dump(cfg, allow_unicode=True, default_flow_style=False)


def _yaml_path(strategy_name: str) -> Path:
    """返回策略 YAML 配置文件路径。"""
    return STRATEGY_CONFIG_DIR / f"{strategy_name}.yml"


def register_control_tools(mcp: FastMCP) -> None:
    """将所有控制工具注册到 MCP server 实例。"""

    # ── create_strategy ──────────────────────────────────────────────────────────

    @mcp.tool()
    def create_strategy(
        strategy_type: str,
        strategy_name: str,
        params: dict,
        so_path: str | None = None,
    ) -> str:
        """
        创建新策略实例。执行流程：
        1. 生成 YAML 配置文件（strategy_config/{strategy_name}.yml）
        2. 写入 DB（Flask POST /api/strategies）
        3. 通知 C++ 引擎热加载（POST /api/strategy/add）

        Args:
            strategy_type: 策略类型 ID，如 "SpotGrid"。
                           可从 list_strategy_types() 返回结果获取。
            strategy_name: 策略实例名称，如 "btc_grid_01"。全局唯一。
            params: 策略参数 key-value 对。必填参数参考 list_strategy_types() 返回。
                    示例: {"account_id": "okx1", "market": "okx",
                            "instrument": "BTC-USDT", "price_lower": 90000, ...}
            so_path: （可选）.so 动态库路径。仅 AI 生成的新策略类型需要此参数。
                     示例: "bin/strategy_plugins/libstrategy_my_momentum_live.so"
                     若为 None，则使用静态注册的内置策略类型。
        """
        yaml_path = _yaml_path(strategy_name)

        try:
            # Step 1: 生成 YAML 配置文件
            yaml_content = _generate_strategy_yaml(
                strategy_name, strategy_type, params, so_path
            )
            STRATEGY_CONFIG_DIR.mkdir(parents=True, exist_ok=True)
            yaml_path.write_text(yaml_content, encoding="utf-8")
            logger.info(f"已生成策略配置文件: {yaml_path}")

            # Step 2: 写入数据库
            db_payload = {
                "strat_name": strategy_name,
                "strat_template": strategy_type,
                "param": params,
                "indicator": {},
                "status": 0,
            }
            db_result = flask_client.post("/api/strategies", db_payload)
            strategy_id = db_result.get("data", {}).get("id")
            logger.info(f"策略已写入 DB，ID: {strategy_id}")

            # Step 3: 通知 C++ 引擎热加载
            gw_result = gw_client.add_strategy(str(yaml_path.absolute()))
            logger.info(f"C++ 引擎热加载结果: {gw_result}")

            return json.dumps(
                {
                    "success": True,
                    "strategy_id": strategy_id,
                    "strategy_name": strategy_name,
                    "config_path": str(yaml_path),
                    "engine_result": gw_result,
                    "message": f"策略 {strategy_name} 创建并加载成功",
                },
                ensure_ascii=False,
                indent=2,
            )

        except Exception as e:
            # 回滚：删除已写入的 YAML 文件
            if yaml_path.exists():
                yaml_path.unlink()
            logger.error(f"创建策略失败: {e}")
            return json.dumps({"success": False, "error": str(e)}, ensure_ascii=False)

    # ── update_strategy ──────────────────────────────────────────────────────────

    @mcp.tool()
    def update_strategy(strategy_id: int, params: dict) -> str:
        """
        更新策略参数（通常在策略停止状态下修改，重启后生效）。

        Args:
            strategy_id: 策略 ID（整数）。
            params: 要更新的参数字段（支持部分更新）。
        """
        try:
            result = flask_client.put(
                f"/api/strategies/{strategy_id}",
                {"param": params},
            )
            return json.dumps(
                {"success": True, "message": "策略参数已更新", "result": result},
                ensure_ascii=False,
                indent=2,
            )
        except Exception as e:
            return json.dumps({"success": False, "error": str(e)}, ensure_ascii=False)

    # ── start_strategy ───────────────────────────────────────────────────────────

    @mcp.tool()
    def start_strategy(strategy_id: str) -> str:
        """
        启动策略（通知 C++ 引擎运行该策略）。
        strategy_id 为字符串类型（C++ 引擎使用字符串 ID）。

        Args:
            strategy_id: 策略名称（即 strat_name 字段），如 "btc_grid_01"。
        """
        try:
            result = gw_client.start_strategy(strategy_id)
            return json.dumps(
                {"success": True, "strategy_id": strategy_id, "result": result},
                ensure_ascii=False,
                indent=2,
            )
        except Exception as e:
            return json.dumps({"success": False, "error": str(e)}, ensure_ascii=False)

    # ── stop_strategy ────────────────────────────────────────────────────────────

    @mcp.tool()
    def stop_strategy(strategy_id: str) -> str:
        """
        停止运行中的策略（不删除数据）。

        Args:
            strategy_id: 策略名称（即 strat_name 字段），如 "btc_grid_01"。
        """
        try:
            result = gw_client.stop_strategy(strategy_id)
            return json.dumps(
                {"success": True, "strategy_id": strategy_id, "result": result},
                ensure_ascii=False,
                indent=2,
            )
        except Exception as e:
            return json.dumps({"success": False, "error": str(e)}, ensure_ascii=False)

    # ── restart_strategy ─────────────────────────────────────────────────────────

    @mcp.tool()
    def restart_strategy(strategy_id: str) -> str:
        """
        重启策略（通常在修改参数后调用，使新参数生效）。

        Args:
            strategy_id: 策略名称（即 strat_name 字段），如 "btc_grid_01"。
        """
        try:
            result = gw_client.restart_strategy(strategy_id)
            return json.dumps(
                {"success": True, "strategy_id": strategy_id, "result": result},
                ensure_ascii=False,
                indent=2,
            )
        except Exception as e:
            return json.dumps({"success": False, "error": str(e)}, ensure_ascii=False)

    # ── delete_strategy ──────────────────────────────────────────────────────────

    @mcp.tool()
    def delete_strategy(strategy_id: int, strategy_name: str) -> str:
        """
        删除策略实例（引擎卸载 + DB 删除 + YAML 文件清理）。
        注意：此操作不可逆，请确认策略已停止后再删除。

        Args:
            strategy_id: 策略 DB ID（整数），用于删除 DB 记录。
            strategy_name: 策略名称，用于从引擎卸载并删除 YAML 文件。
        """
        errors = []

        # Step 1: 通知引擎停止并卸载
        try:
            gw_client.stop_strategy(strategy_name)
        except Exception as e:
            errors.append(f"引擎停止失败（忽略）: {e}")

        try:
            gw_client.delete_strategy(strategy_name)
        except Exception as e:
            errors.append(f"引擎卸载失败（忽略）: {e}")

        # Step 2: 删除 DB 记录
        try:
            flask_client.delete(f"/api/strategies/{strategy_id}")
        except Exception as e:
            errors.append(f"DB 删除失败: {e}")
            return json.dumps(
                {"success": False, "error": "; ".join(errors)}, ensure_ascii=False
            )

        # Step 3: 删除 YAML 配置文件
        yaml_path = _yaml_path(strategy_name)
        if yaml_path.exists():
            yaml_path.unlink()
            logger.info(f"已删除配置文件: {yaml_path}")

        return json.dumps(
            {
                "success": True,
                "message": f"策略 {strategy_name} 已删除",
                "warnings": errors if errors else None,
            },
            ensure_ascii=False,
            indent=2,
        )
