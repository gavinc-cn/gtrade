"""
gtrade_py — gtrade Python 策略框架

用户策略继承 StrategyBase，实现行情回调逻辑。

示例：
    from gtrade_py import StrategyBase

    class MyStrategy(StrategyBase):
        def on_init(self, config: dict) -> bool:
            self.subscribe_kline_close(config['market'], config['instrument'], 1, 'H')
            return True

        def on_kline_close(self, kline):
            print(f'close={kline.close}')
"""

from .strategy_base import StrategyBase
from .structs import (
    Depth, KLine, Trade, Position, Balance, Order,
    OrderReq, QuoteSub, KLineSub, TradeSub, WithdrawReq,
    SetTimerReq, TimerKey, TimerEventPush,
)
from .msg_id import MsgId

__all__ = [
    'StrategyBase',
    'Depth', 'KLine', 'Trade', 'Position', 'Balance', 'Order',
    'OrderReq', 'QuoteSub', 'KLineSub', 'TradeSub', 'WithdrawReq',
    'SetTimerReq', 'TimerKey', 'TimerEventPush',
    'MsgId',
]
