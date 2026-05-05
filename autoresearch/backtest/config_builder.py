"""构建 gtrade_bt 所需的 backtest_config.yml 和策略 YAML 配置。"""

import re
import yaml
import os
import uuid
from autoresearch.config import get as cfg
from autoresearch.codegen.template import YAML_TEMPLATE


def build_backtest_config(
    strategy_yaml_path: str,
    start_date: str,
    end_date: str,
    out_dir: str | None = None,
) -> tuple[str, str]:
    """生成 backtest_config.yml，返回 (config_path, run_out_dir)。"""
    conf = cfg()
    out_dir = out_dir or conf["paths"]["backtest_out_dir"]
    os.makedirs(out_dir, exist_ok=True)

    run_id = str(uuid.uuid4())[:8]
    run_out_dir = os.path.join(out_dir, run_id)
    os.makedirs(run_out_dir, exist_ok=True)

    bt_config = {
        "strategy_config": [strategy_yaml_path],
        "start_date": start_date,
        "end_date": end_date,
        "csv_quote_base_dir": conf["paths"]["csv_quote_base_dir"],
        "backtest_out_dir": run_out_dir,
        "fill_mode": conf["backtest"]["fill_mode"],
    }
    config_path = os.path.join(run_out_dir, "backtest_config.yml")
    with open(config_path, "w") as f:
        yaml.dump(bt_config, f, allow_unicode=True)
    return config_path, run_out_dir


def build_strategy_yaml(
    spec: dict,
    strategy_file: str,
    class_name: str,
    params: dict | None = None,
    out_dir: str | None = None,
) -> str:
    """生成策略 YAML 配置，返回文件路径。"""
    conf = cfg()
    out_dir = out_dir or conf["paths"]["backtest_out_dir"]
    os.makedirs(out_dir, exist_ok=True)

    data_req = spec.get("data_requirements", {})
    strat_id = f"auto_{class_name.lower()}_{str(uuid.uuid4())[:6]}"

    yaml_content = YAML_TEMPLATE.format(
        strat_id=strat_id,
        market=data_req.get("market", "okx"),
        instrument=data_req.get("instrument", "BTC-USDT-SWAP"),
        strategy_file=strategy_file,
        class_name=class_name,
    )

    # 解析 kline_interval（如 "1H", "4H", "1D"）→ kline_coeff + kline_scale
    # 与 StrategyBase.subscribe_kline_close(coeff, scale) 保持一致
    raw_kline = data_req.get("kline_interval", "1H")
    m = re.match(r"^(\d+)([A-Za-z]+)$", raw_kline)
    if m:
        yaml_content += f"kline_coeff: {m.group(1)}\n"
        yaml_content += f"kline_scale: {m.group(2).upper()}\n"

    # 追加参数覆盖
    if params:
        for k, v in params.items():
            yaml_content += f"{k}: {v}\n"

    yaml_path = os.path.join(out_dir, f"{strat_id}.yml")
    with open(yaml_path, "w") as f:
        f.write(yaml_content)
    return yaml_path
