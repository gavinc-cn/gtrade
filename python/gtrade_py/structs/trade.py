# 自动生成，勿手改
# 源: ../../interface/python_structs/Trade.xml
import ctypes


class Trade(ctypes.Structure):
    _fields_ = [
        ('tdno', ctypes.c_int64),  # 成交号
        ('market', ctypes.c_char * 16),  # 市场
        ('account_id', ctypes.c_char * 32),  # 账户ID
        ('portfolio', ctypes.c_char * 32),  # 组合
        ('instrument', ctypes.c_char * 32),  # 标的名称
        ('strat_id', ctypes.c_char * 32),  # 策略编号
        ('private_no', ctypes.c_char * 64),  # 私有号
        ('ordno', ctypes.c_int64),  # 委托号
        ('td_side', ctypes.c_char),  # 买卖方向(B/S)
        ('pos_side', ctypes.c_char),  # 持仓方向
        ('px_type', ctypes.c_char),  # 价格类型
        ('td_px', ctypes.c_double),  # 成交价格
        ('td_qty', ctypes.c_double),  # 成交数量
        ('td_val', ctypes.c_double),  # 成交金额
        ('filled_time', ctypes.c_int64),  # 成交时间(ns)
        ('ord_status_id', ctypes.c_int64),  # 委托状态id
        ('margin_mode', ctypes.c_char),  # 保证金模式
    ]

    @classmethod
    def from_bytes(cls, data: bytes):
        assert len(data) == ctypes.sizeof(cls), (
            f'Trade: expected {ctypes.sizeof(cls)} bytes, got {len(data)}'
        )
        return cls.from_buffer_copy(data)

    def __repr__(self):
        return f'Trade(tdno={self.tdno}, market={self.market.decode('utf-8', errors='replace').rstrip(chr(0))}, account_id={self.account_id.decode('utf-8', errors='replace').rstrip(chr(0))}, portfolio={self.portfolio.decode('utf-8', errors='replace').rstrip(chr(0))})'
