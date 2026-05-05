# 自动生成，勿手改
# 源: ../../interface/python_structs/TimerEventPush.xml
import ctypes


class TimerEventPush(ctypes.Structure):
    _fields_ = [
        ('service_name', ctypes.c_char * 32),  # 服务名（来自 TimerKey）
        ('setter_id', ctypes.c_char * 64),  # 设置者ID（来自 TimerKey）
        ('timer_id', ctypes.c_int),  # 定时器ID（来自 TimerKey）
        ('target_ms', ctypes.c_int64),  # 目标触发时间(ms)
    ]

    @classmethod
    def from_bytes(cls, data: bytes):
        assert len(data) == ctypes.sizeof(cls), (
            f'TimerEventPush: expected {ctypes.sizeof(cls)} bytes, got {len(data)}'
        )
        return cls.from_buffer_copy(data)

    def __repr__(self):
        return f'TimerEventPush(service_name={self.service_name.decode('utf-8', errors='replace').rstrip(chr(0))}, setter_id={self.setter_id.decode('utf-8', errors='replace').rstrip(chr(0))}, timer_id={self.timer_id}, target_ms={self.target_ms})'
