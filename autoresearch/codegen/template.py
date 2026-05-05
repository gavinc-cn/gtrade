"""gtrade_py 策略代码模板，供 Claude 生成代码时参考。"""

STRATEGY_TEMPLATE = '''"""
{title}

自动生成自: {source}
策略规格: {signal_logic}
"""

from gtrade_py import StrategyBase


class {class_name}(StrategyBase):
    """{title}

    {signal_logic}
    """

    def on_init(self, config: dict) -> bool:
        self.market   = config["market"]
        self.inst     = config["instrument"]
        self.acct     = config["account_id"]

        # === 策略参数（从 config 读取，允许外部覆盖）===
        {param_assignments}

        # === 状态变量 ===
        self.in_pos = False
        self._alpha_history: list[float] = []  # 信号历史，用于回测后 IC 计算

        # === 订阅行情 ===
        # coeff/scale 与 StrategyBase.subscribe_kline_close 参数名保持一致
        kline_coeff = config.get("kline_coeff", 1)   # 数值，如 1、4、1
        kline_scale = config.get("kline_scale", "H") # 单位，如 'H'、'D'、'm'
        self.subscribe_kline_close(self.market, self.inst,
                                   int(kline_coeff), kline_scale)
        self.subscribe_trade(self.market, self.acct, self.inst)
        return True

    def compute_alpha(self, kline) -> float:
        """Alpha 信号层：仅做信号计算，不下单。
        返回正值=看多信号，负值=看空，0=无信号。
        TODO: 由 Claude 根据策略逻辑实现
        """
        return 0.0

    def on_kline_close(self, kline) -> None:
        # === 第一层：计算 Alpha 信号 ===
        alpha = self.compute_alpha(kline)
        self._alpha_history.append(alpha)

        # === 第二层：执行层，根据信号决定开平仓 ===
        # TODO: 由 Claude 根据 alpha 值实现开平仓逻辑
        pass

    def on_trade_push(self, trade) -> None:
        pass

    def on_place_order_confirm(self, private_no: str, entno: int, status: str) -> None:
        # private_no 用于与 place_order 返回值对账
        # status == "9" 表示废单，需重置仓位标志
        if status == "9":
            self.in_pos = False

    def on_stop(self) -> bool:
        return True
'''

YAML_TEMPLATE = """strat_id: {strat_id}
account_id: main_account
market: {market}
instrument: {instrument}
fee: 0.0005

auto_restart: true
max_restart_count: 3
restart_interval_ms: 5000
heartbeat_timeout_ms: 3000

python_file: {strategy_file}
python_class: {class_name}
python_interpreter: python3
"""
