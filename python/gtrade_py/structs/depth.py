# 自动生成，勿手改
# 源: ../../interface/python_structs/Depth.xml
import ctypes


class Depth(ctypes.Structure):
    _fields_ = [
        ('ex_time', ctypes.c_int64),  # 交易所时间戳(ns)
        ('local_time', ctypes.c_int64),  # 本地时间戳(ns)
        ('datetime', ctypes.c_char * 32),  # 格式化时间
        ('market', ctypes.c_char * 16),  # 市场
        ('symbol', ctypes.c_char * 32),  # 标的
        ('ask_cnt', ctypes.c_int),  # 卖档数
        ('ask_price', ctypes.c_double * 10),  # 卖价[10]
        ('ask_amount', ctypes.c_double * 10),  # 卖量[10]
        ('bid_cnt', ctypes.c_int),  # 买档数
        ('bid_price', ctypes.c_double * 10),  # 买价[10]
        ('bid_amount', ctypes.c_double * 10),  # 买量[10]
        ('seq_id', ctypes.c_int64),  # 序号
        ('monotonic', ctypes.c_int64),  # 单调时间戳(ns)
    ]

    @classmethod
    def from_bytes(cls, data: bytes):
        assert len(data) == ctypes.sizeof(cls), (
            f'Depth: expected {ctypes.sizeof(cls)} bytes, got {len(data)}'
        )
        return cls.from_buffer_copy(data)

    def __repr__(self):
        return f'Depth(ex_time={self.ex_time}, local_time={self.local_time}, datetime={self.datetime.decode('utf-8', errors='replace').rstrip(chr(0))}, market={self.market.decode('utf-8', errors='replace').rstrip(chr(0))})'
