# 自动生成，勿手改
# 源: ../../interface/python_structs/WithdrawReq.xml
import ctypes


class WithdrawReq(ctypes.Structure):
    _fields_ = [
        ('market', ctypes.c_char * 16),  # 市场
        ('account_id', ctypes.c_char * 32),  # 账户ID
        ('instrument', ctypes.c_char * 32),  # 标的
        ('entno', ctypes.c_int64),  # 委托号
    ]

    @classmethod
    def from_bytes(cls, data: bytes):
        assert len(data) == ctypes.sizeof(cls), (
            f'WithdrawReq: expected {ctypes.sizeof(cls)} bytes, got {len(data)}'
        )
        return cls.from_buffer_copy(data)

    def __repr__(self):
        return f'WithdrawReq(market={self.market.decode('utf-8', errors='replace').rstrip(chr(0))}, account_id={self.account_id.decode('utf-8', errors='replace').rstrip(chr(0))}, instrument={self.instrument.decode('utf-8', errors='replace').rstrip(chr(0))}, entno={self.entno})'
