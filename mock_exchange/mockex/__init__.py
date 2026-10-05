"""
mock_exchange：本地假 OKX 交易所（供 gtrade 引擎与策略在无外网环境下测试）

设计见 `doc_ai/spec/mock_exchange/模拟交易所.md`；引擎字段契约见 `引擎OKX契约.md`。
"""

from mockex.config import MockConfig, load_config

__all__ = ["MockConfig", "load_config"]
