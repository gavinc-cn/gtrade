# 自动生成，勿手改
# 源: ../../interface/python_structs/Order.xml
import ctypes


class Order(ctypes.Structure):
    _fields_ = [
        ('entno', ctypes.c_int64),  # 委托号
        ('status', ctypes.c_char),  # 委托状态
        ('fmt_time', ctypes.c_int64),  # 格式化时间
        ('market', ctypes.c_char * 16),  # 市场
        ('account_id', ctypes.c_char * 32),  # 账户ID
        ('portfolio', ctypes.c_char * 32),  # 组合
        ('inst_type', ctypes.c_char),  # 标的类型
        ('inst_id', ctypes.c_char * 32),  # 标的名称
        ('inst_id_code', ctypes.c_int64),  # 标的唯一标识代码
        ('policy_no', ctypes.c_char * 32),  # 策略编号
        ('private_no', ctypes.c_char * 64),  # 私有号
        ('bs_side', ctypes.c_char),  # 买卖方向(B/S)
        ('pos_side', ctypes.c_char),  # 持仓方向
        ('oc_side', ctypes.c_char),  # 开平方向
        ('trade_mode', ctypes.c_char),  # 交易模式
        ('price_type', ctypes.c_char),  # 价格类型
        ('price', ctypes.c_double),  # 价格
        ('amount', ctypes.c_double),  # 数量
        ('ent_time', ctypes.c_int64),  # 委托时间(ns)
        ('expire_time', ctypes.c_int64),  # 委托超时时间(ns)
        ('ex_entno', ctypes.c_int64),  # 交易所委托号
        ('filled_px', ctypes.c_double),  # 成交均价
        ('filled', ctypes.c_double),  # 成交数量
        ('remain', ctypes.c_double),  # 剩余数量
        ('confirm_time', ctypes.c_int64),  # 委托确认时间(ns)
        ('filled_time', ctypes.c_int64),  # 成交时间(ns)
        ('update_time', ctypes.c_int64),  # 更新时间(ns)
        ('source', ctypes.c_char),  # 委托来源
        ('err_code', ctypes.c_int),  # 错误码
        ('err_msg', ctypes.c_char * 128),  # 错误消息
        ('drawno', ctypes.c_int64),  # 撤单号
        ('draw_amt', ctypes.c_double),  # 撤单数量
        ('withdraw_time', ctypes.c_int64),  # 撤单时间(ns)
        ('status_id', ctypes.c_int),  # 订单状态id
        ('status_gid', ctypes.c_int64),  # 订单状态全局id
        ('trd_px', ctypes.c_double),  # 最近一笔成交价
        ('trd_qty', ctypes.c_double),  # 最近一笔成交量
    ]

    @classmethod
    def from_bytes(cls, data: bytes):
        assert len(data) == ctypes.sizeof(cls), (
            f'Order: expected {ctypes.sizeof(cls)} bytes, got {len(data)}'
        )
        return cls.from_buffer_copy(data)

    def __repr__(self):
        return f'Order(entno={self.entno}, status={self.status}, fmt_time={self.fmt_time}, market={self.market.decode('utf-8', errors='replace').rstrip(chr(0))})'
