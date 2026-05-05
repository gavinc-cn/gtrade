# 自动生成，勿手改
# 源: ../../interface/python_structs/TimerKey.xml
import ctypes


class TimerKey(ctypes.Structure):
    _fields_ = [
        ('service_name', ctypes.c_char * 32),  # 服务名
        ('setter_id', ctypes.c_char * 64),  # 设置者ID
        ('timer_id', ctypes.c_int),  # 定时器ID
    ]

    @classmethod
    def from_bytes(cls, data: bytes):
        assert len(data) == ctypes.sizeof(cls), (
            f'TimerKey: expected {ctypes.sizeof(cls)} bytes, got {len(data)}'
        )
        return cls.from_buffer_copy(data)

    def __repr__(self):
        return f'TimerKey(service_name={self.service_name.decode('utf-8', errors='replace').rstrip(chr(0))}, setter_id={self.setter_id.decode('utf-8', errors='replace').rstrip(chr(0))}, timer_id={self.timer_id})'
