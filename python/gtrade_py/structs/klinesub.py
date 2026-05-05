# 自动生成，勿手改
# 源: ../../interface/python_structs/KLineSub.xml
import ctypes


class KLineSub(ctypes.Structure):
    _fields_ = [
        ('market', ctypes.c_char * 16),  # 市场
        ('instrument', ctypes.c_char * 32),  # 标的
        ('coefficient', ctypes.c_int),  # 时间周期系数
        ('scale', ctypes.c_char),  # 时间周期单位(d/H/M/S)
        ('strat_id', ctypes.c_char * 64),  # 策略ID
    ]

    @classmethod
    def from_bytes(cls, data: bytes):
        assert len(data) == ctypes.sizeof(cls), (
            f'KLineSub: expected {ctypes.sizeof(cls)} bytes, got {len(data)}'
        )
        return cls.from_buffer_copy(data)

    def __repr__(self):
        return f'KLineSub(market={self.market.decode('utf-8', errors='replace').rstrip(chr(0))}, instrument={self.instrument.decode('utf-8', errors='replace').rstrip(chr(0))}, coefficient={self.coefficient}, scale={self.scale})'
