# 自动生成，勿手改
# 源: ../../interface/python_structs/TradeSub.xml
import ctypes


class TradeSub(ctypes.Structure):
    _fields_ = [
        ('market', ctypes.c_char * 16),  # 市场
        ('account_id', ctypes.c_char * 32),  # 账户ID
        ('inst_id', ctypes.c_char * 32),  # 标的
        ('strat_id', ctypes.c_char * 64),  # 策略ID
    ]

    @classmethod
    def from_bytes(cls, data: bytes):
        assert len(data) == ctypes.sizeof(cls), (
            f'TradeSub: expected {ctypes.sizeof(cls)} bytes, got {len(data)}'
        )
        return cls.from_buffer_copy(data)

    def __repr__(self):
        return f'TradeSub(market={self.market.decode('utf-8', errors='replace').rstrip(chr(0))}, account_id={self.account_id.decode('utf-8', errors='replace').rstrip(chr(0))}, inst_id={self.inst_id.decode('utf-8', errors='replace').rstrip(chr(0))}, strat_id={self.strat_id.decode('utf-8', errors='replace').rstrip(chr(0))})'
