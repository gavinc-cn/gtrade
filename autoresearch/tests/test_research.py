"""测试策略提取逻辑（mock Claude，不需要真实 API key）。"""

import json
import os
import pytest
from unittest.mock import patch, MagicMock
from autoresearch.research.strategy_extractor import extract_strategy_spec
from pathlib import Path
from dotenv import load_dotenv

# 加载 .env 文件
env_path = Path(__file__).parent.parent.parent / '.env'
if env_path.exists():
    load_dotenv(env_path)

MOCK_SPEC = {
    "title": "MACrossover",
    "signal_logic": "短期均线上穿长期均线时做多",
    "entry_conditions": ["fast_ma > slow_ma"],
    "exit_conditions": ["fast_ma < slow_ma"],
    "params": {"fast_period": 10, "slow_period": 30},
    "param_ranges": {"fast_period": [5, 20], "slow_period": [20, 60]},
    "data_requirements": {
        "channel": "depth1",
        "instrument": "BTC-USDT-SWAP",
        "market": "okx",
        "kline_interval": "1H"
    },
    "risk_notes": "趋势反转时可能有较大回撤",
    "implementable": True,
    "source": "test:mock"
}


@pytest.fixture
def mock_cfg(monkeypatch):
    import autoresearch.config as cfg_mod
    monkeypatch.setattr(cfg_mod, "_CONFIG", {
        "anthropic_api_key": os.environ.get("TEST_ANTHROPIC_API_KEY", ""),
        "claude_model": "claude-sonnet-4-6"
    })


def test_extract_strategy_spec_success(mock_cfg):
    mock_msg = MagicMock()
    mock_msg.content = [MagicMock(text=json.dumps(MOCK_SPEC))]

    with patch("anthropic.Anthropic") as MockClient:
        MockClient.return_value.messages.create.return_value = mock_msg
        spec = extract_strategy_spec(
            "This paper proposes a moving average crossover strategy...",
            source="test:mock"
        )

    assert spec is not None
    assert spec["title"] == "MACrossover"
    assert "fast_period" in spec["params"]


def test_extract_strategy_spec_not_implementable(mock_cfg):
    not_impl = {**MOCK_SPEC, "implementable": False}
    mock_msg = MagicMock()
    mock_msg.content = [MagicMock(text=json.dumps(not_impl))]

    with patch("anthropic.Anthropic") as MockClient:
        MockClient.return_value.messages.create.return_value = mock_msg
        spec = extract_strategy_spec("Some unrelated content")

    assert spec is None
