# 自动生成，勿手改
# 源: ../../interface/python_structs/Position.xml
import ctypes


class Position(ctypes.Structure):
    _fields_ = [
        ('market', ctypes.c_char * 16),  # 市场
        ('account_id', ctypes.c_char * 32),  # 账户ID
        ('inst_type', ctypes.c_char),  # 标的类型
        ('instrument', ctypes.c_char * 32),  # 标的名称
        ('pos_side', ctypes.c_char),  # 持仓方向[PosSide]
        ('portfolio', ctypes.c_char * 32),  # 组合
        ('margin_mode', ctypes.c_char),  # 保证金模式
        ('avg_px', ctypes.c_double),  # 成交均价
        ('available', ctypes.c_double),  # 可用数量
        ('ex_time', ctypes.c_int64),  # 交易所时间
        ('local_time', ctypes.c_int64),  # 本地时间
        ('datetime', ctypes.c_char * 32),  # 格式化时间
        ('upl', ctypes.c_double),  # 未实现盈亏
        ('upl_ratio', ctypes.c_double),  # 未实现盈亏比率
        ('notional_usd', ctypes.c_double),  # 名义价值(USD)
        ('total_cost', ctypes.c_double),  # 总成本
        ('realized_pnl', ctypes.c_double),  # 已实现盈亏
        ('fee_paid', ctypes.c_double),  # 已支付手续费
        ('pos_source', ctypes.c_char),  # 持仓来源[PosSource]
    ]

    @classmethod
    def from_bytes(cls, data: bytes):
        assert len(data) == ctypes.sizeof(cls), (
            f'Position: expected {ctypes.sizeof(cls)} bytes, got {len(data)}'
        )
        return cls.from_buffer_copy(data)

    def __repr__(self):
        return f'Position(market={self.market.decode('utf-8', errors='replace').rstrip(chr(0))}, account_id={self.account_id.decode('utf-8', errors='replace').rstrip(chr(0))}, inst_type={self.inst_type}, instrument={self.instrument.decode('utf-8', errors='replace').rstrip(chr(0))})'
