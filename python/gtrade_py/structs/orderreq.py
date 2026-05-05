# 自动生成，勿手改
# 源: ../../interface/python_structs/OrderReq.xml
import ctypes


class OrderReq(ctypes.Structure):
    _fields_ = [
        ('market', ctypes.c_char * 16),  # 市场
        ('account_id', ctypes.c_char * 32),  # 账户ID
        ('portfolio', ctypes.c_char * 32),  # 组合
        ('inst_id', ctypes.c_char * 32),  # 标的
        ('inst_id_code', ctypes.c_int64),  # 标的唯一标识代码
        ('policy_no', ctypes.c_char * 32),  # 策略编号
        ('private_no', ctypes.c_char * 64),  # 私有号
        ('entno', ctypes.c_int64),  # 委托号
        ('bs_side', ctypes.c_char),  # 买卖方向 b/s
        ('pos_side', ctypes.c_char),  # 持仓方向 l/s/n
        ('oc_side', ctypes.c_char),  # 开平方向 o/c
        ('price_type', ctypes.c_char),  # 价格类型 l/m
        ('trade_mode', ctypes.c_char),  # 交易模式 c/i
        ('price', ctypes.c_double),  # 价格
        ('amount', ctypes.c_double),  # 数量
        ('expire_time', ctypes.c_int64),  # 超时时间(ns)
        ('ent_time', ctypes.c_int64),  # 委托时间(ns)
        ('quote_monotonic', ctypes.c_int64),  # 行情单调时间戳
    ]

    @classmethod
    def from_bytes(cls, data: bytes):
        assert len(data) == ctypes.sizeof(cls), (
            f'OrderReq: expected {ctypes.sizeof(cls)} bytes, got {len(data)}'
        )
        return cls.from_buffer_copy(data)

    def __repr__(self):
        return f'OrderReq(market={self.market.decode('utf-8', errors='replace').rstrip(chr(0))}, account_id={self.account_id.decode('utf-8', errors='replace').rstrip(chr(0))}, portfolio={self.portfolio.decode('utf-8', errors='replace').rstrip(chr(0))}, inst_id={self.inst_id.decode('utf-8', errors='replace').rstrip(chr(0))})'
