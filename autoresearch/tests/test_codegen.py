"""测试策略代码验证逻辑。"""

import os
import pytest
from autoresearch.codegen.validator import validate_strategy_code, ValidationError, save_strategy

VALID_CODE = """
from gtrade_py import StrategyBase

class MACrossover(StrategyBase):
    def on_init(self, config):
        self.fast = config.get("fast_period", 10)
        self.closes = []
        self.subscribe_kline_close(config["market"], config["instrument"], 1, "H")
        return True

    def on_kline_close(self, kline):
        self.closes.append(kline.close)
        if len(self.closes) >= self.fast:
            ma = sum(self.closes[-self.fast:]) / self.fast
            if kline.close > ma and not self.in_pos:
                self.in_pos = True
"""


def test_valid_code_passes():
    validate_strategy_code(VALID_CODE, "MACrossover")  # should not raise


def test_missing_class_raises():
    with pytest.raises(ValidationError, match="未找到类"):
        validate_strategy_code(VALID_CODE, "WrongClassName")


def test_missing_base_raises():
    bad = VALID_CODE.replace("(StrategyBase)", "")
    with pytest.raises(ValidationError, match="未继承 StrategyBase"):
        validate_strategy_code(bad, "MACrossover")


def test_missing_method_raises():
    bad = VALID_CODE.replace("def on_kline_close", "def on_tick")
    with pytest.raises(ValidationError, match="缺少必要方法"):
        validate_strategy_code(bad, "MACrossover")


def test_syntax_error_raises():
    with pytest.raises(ValidationError, match="语法错误"):
        validate_strategy_code("def (broken:", "X")


def test_save_strategy(tmp_path):
    path = save_strategy(VALID_CODE, "MACrossover", str(tmp_path))
    assert os.path.exists(path)
    assert "macrossover" in path
    assert open(path).read() == VALID_CODE
