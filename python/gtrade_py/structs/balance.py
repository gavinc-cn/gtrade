# 自动生成，勿手改
# 源: ../../interface/python_structs/Balance.xml
import ctypes


class Balance(ctypes.Structure):
    _fields_ = [
        ('ex_time', ctypes.c_int64),  # 交易所时间(ns)
        ('local_time', ctypes.c_int64),  # 本地时间(ns)
        ('datetime', ctypes.c_char * 32),  # 格式化时间
        ('market', ctypes.c_char * 16),  # 市场
        ('account_id', ctypes.c_char * 32),  # 账户ID
        ('currency', ctypes.c_char * 8),  # 币种
        ('available', ctypes.c_double),  # 可用余额
        ('frozen', ctypes.c_double),  # 冻结余额
        ('total', ctypes.c_double),  # 总余额
    ]

    @classmethod
    def from_bytes(cls, data: bytes):
        assert len(data) == ctypes.sizeof(cls), (
            f'Balance: expected {ctypes.sizeof(cls)} bytes, got {len(data)}'
        )
        return cls.from_buffer_copy(data)

    def __repr__(self):
        return f'Balance(ex_time={self.ex_time}, local_time={self.local_time}, datetime={self.datetime.decode('utf-8', errors='replace').rstrip(chr(0))}, market={self.market.decode('utf-8', errors='replace').rstrip(chr(0))})'
