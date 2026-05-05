"""将策略部署到实盘配置目录。"""

import os
import shutil
import yaml
from autoresearch.config import get as cfg


def deploy_to_live(
    strategy_file: str,
    strategy_yaml_content: str,
    strat_id: str,
) -> tuple[str, str]:
    """将策略文件和配置复制到实盘目录。

    Args:
        strategy_file: 策略 Python 文件路径
        strategy_yaml_content: 策略 YAML 配置内容字符串
        strat_id: 策略 ID（用于命名配置文件）

    Returns:
        (live_strategy_file, live_yaml_path)
    """
    live_dir = cfg()["paths"]["live_strategy_config_dir"]
    strategy_live_dir = os.path.join(os.path.dirname(live_dir), "live_strategies")
    os.makedirs(strategy_live_dir, exist_ok=True)

    # 复制策略文件
    dest_py = os.path.join(strategy_live_dir, os.path.basename(strategy_file))
    shutil.copy2(strategy_file, dest_py)

    # 写 YAML 配置（更新 python_file 路径）
    config = yaml.safe_load(strategy_yaml_content)
    config["python_file"] = dest_py
    config["strat_id"] = strat_id

    dest_yml = os.path.join(live_dir, f"{strat_id}.yml")
    with open(dest_yml, "w") as f:
        yaml.dump(config, f, allow_unicode=True)

    return dest_py, dest_yml
