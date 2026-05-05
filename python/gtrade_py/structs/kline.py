# 自动生成，勿手改
# 源: ../../interface/python_structs/KLine.xml
import ctypes


class KLine(ctypes.Structure):
    _fields_ = [
        ('ex_time', ctypes.c_int64),  # 交易所时间戳(ns)
        ('local_time', ctypes.c_int64),  # 本地接收时间戳(ns)
        ('datetime', ctypes.c_char * 32),  # 格式化时间字符串
        ('market', ctypes.c_char * 16),  # 市场
        ('instrument', ctypes.c_char * 32),  # 标的
        ('coefficient', ctypes.c_int),  # 时间周期系数
        ('scale', ctypes.c_char),  # 时间周期单位(d/H/M/S)
        ('open', ctypes.c_double),  # 开盘价
        ('high', ctypes.c_double),  # 最高价
        ('low', ctypes.c_double),  # 最低价
        ('close', ctypes.c_double),  # 收盘价
        ('volume', ctypes.c_double),  # 成交量
    ]

    @classmethod
    def from_bytes(cls, data: bytes):
        assert len(data) == ctypes.sizeof(cls), (
            f'KLine: expected {ctypes.sizeof(cls)} bytes, got {len(data)}'
        )
        return cls.from_buffer_copy(data)

    def __repr__(self):
        return f'KLine(ex_time={self.ex_time}, local_time={self.local_time}, datetime={self.datetime.decode('utf-8', errors='replace').rstrip(chr(0))}, market={self.market.decode('utf-8', errors='replace').rstrip(chr(0))})'
