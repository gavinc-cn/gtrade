"""
Python 策略基类

用户策略继承此类，实现行情回调方法（on_depth1、on_kline_close 等），
并通过实例方法（subscribe_quote、place_order 等）与引擎交互。

所有与引擎的交互均通过 StrategyShm.write_request() 将 ctypes.Structure
序列化为原始字节写入 Channel B，与 C++ runner 协议完全一致。
"""

import uuid
import ctypes
from .shm import StrategyShm
from .msg_id import MsgId
from .structs import (
    OrderReq as RawOrderReq,
    QuoteSub, KLineSub, TradeSub,
    WithdrawReq, SetTimerReq, TimerKey,
)


class StrategyBase:
    """所有 Python 策略的基类。

    子类在 on_init() 中订阅行情、初始化状态；
    在 on_kline_close()、on_depth1() 等回调中实现交易逻辑。
    """

    def __init__(self, shm: StrategyShm, strat_id: str):
        self._shm = shm
        self._strat_id = strat_id

    # ── 生命周期（子类可覆盖）────────────────────────────────────────────────

    def on_init(self, config: dict) -> bool:
        """策略初始化：注册订阅、初始化状态。返回 False 则策略不启动。"""
        return True

    def on_start(self) -> bool:
        """策略启动（on_init 成功后调用）。"""
        return True

    def on_stop(self) -> bool:
        """策略停止（SIGTERM 或引擎发送 kSubprocStratStop 时调用）。"""
        return True

    def on_pause(self) -> bool:
        """策略暂停（引擎发送 kSubprocStratPause 时调用）。"""
        return True

    def on_resume(self) -> bool:
        """策略恢复（引擎发送 kSubprocStratResume 时调用）。"""
        return True

    # ── 行情回调（子类实现）──────────────────────────────────────────────────

    def on_depth1(self, depth) -> None:
        """Level-1 盘口行情回调（kDepth1）。depth 为 Depth ctypes 对象。"""

    def on_kline_close(self, kline) -> None:
        """K线收盘回调（kIndicatorKLineClosePush）。kline 为 KLine ctypes 对象。"""

    def on_trade_push(self, trade) -> None:
        """成交推送回调（kTradePush）。trade 为 Trade ctypes 对象。"""

    def on_position_push(self, pos) -> None:
        """持仓推送回调（kPositionPush）。pos 为 Position ctypes 对象。"""

    def on_balance_push(self, balance) -> None:
        """资金推送回调（kBalancePush）。balance 为 Balance ctypes 对象。"""

    def on_timer(self, timer_id: int) -> None:
        """定时器触发回调（kTimerEvent）。"""

    def on_place_order_confirm(self, private_no: str, entno: int, status: str) -> None:
        """委托确认回调（kPlaceOrderConfirm）。

        Args:
            private_no: 委托私有号（由 place_order 返回）
            entno:      交易所委托号
            status:     委托状态（CharCs 单字节，如 '0'=已提交，'9'=废单）
        """

    # ── 操作接口（写 Channel B）──────────────────────────────────────────────

    def subscribe_quote(self, channel: str, market: str, instrument: str) -> None:
        """订阅行情。

        Args:
            channel:    频道名称（如 'books5'）
            market:     市场（如 'okx'）
            instrument: 标的（如 'BTC-USDT'）
        """
        sub = QuoteSub()
        sub.channel  = channel.encode()
        sub.market   = market.encode()
        sub.inst_id  = instrument.encode()
        sub.strat_id = self._strat_id.encode()
        self._shm.write_request(MsgId.kStratSubscribeQuote, bytes(sub))

    def subscribe_kline_close(self, market: str, instrument: str,
                              coeff: int, scale: str) -> None:
        """订阅 K线收盘事件。

        Args:
            market:     市场（如 'okx'）
            instrument: 标的（如 'BTC-USDT-SWAP'）
            coeff:      周期系数（如 1）
            scale:      周期单位（'d'=日, 'H'=小时, 'M'=分, 'S'=秒）
        """
        sub = KLineSub()
        sub.market      = market.encode()
        sub.instrument  = instrument.encode()
        sub.coefficient = coeff
        sub.scale       = scale.encode()
        sub.strat_id    = self._strat_id.encode()
        self._shm.write_request(MsgId.kStratSubscribeKLineClose, bytes(sub))

    def subscribe_trade(self, market: str, account_id: str, instrument: str) -> None:
        """订阅成交/持仓/资金推送。

        Args:
            market:     市场（如 'okx'）
            account_id: 账户ID（如 'main_account'）
            instrument: 标的（如 'BTC-USDT-SWAP'）
        """
        sub = TradeSub()
        sub.market     = market.encode()
        sub.account_id = account_id.encode()
        sub.inst_id    = instrument.encode()
        sub.strat_id   = self._strat_id.encode()
        self._shm.write_request(MsgId.kStratSubscribeTrade, bytes(sub))

    def place_order(self, market: str, account_id: str, inst_id: str,
                    bs_side: str, pos_side: str, oc_side: str,
                    price_type: str, trade_mode: str,
                    amount: float, price: float = 0.0,
                    private_no: str = '') -> str:
        """发送委托请求。

        Args:
            market:     市场（如 'okx'）
            account_id: 账户ID
            inst_id:    标的（如 'BTC-USDT-SWAP'）
            bs_side:    买卖方向 'b'(买) / 's'(卖)
            pos_side:   持仓方向 'l'(多) / 's'(空) / 'n'(净)
            oc_side:    开平方向 'o'(开) / 'c'(平)
            price_type: 价格类型 'l'(限价) / 'm'(市价)
            trade_mode: 交易模式 'c'(全仓) / 'i'(逐仓)
            amount:     委托数量
            price:      委托价格（市价单传 0）
            private_no: 自定义私有号（空则自动生成 UUID）

        Returns:
            private_no 字符串，用于在 on_place_order_confirm 中对账
        """
        if not private_no:
            private_no = str(uuid.uuid4())

        raw = RawOrderReq()
        raw.market     = market.encode()
        raw.account_id = account_id.encode()
        raw.policy_no  = self._strat_id.encode()
        raw.inst_id    = inst_id.encode()
        raw.private_no = private_no.encode()
        # 单字节字段：仅取首字符编码
        raw.bs_side    = bs_side[0].encode()
        raw.pos_side   = pos_side[0].encode()
        raw.oc_side    = oc_side[0].encode()
        raw.price_type = price_type[0].encode()
        raw.trade_mode = trade_mode[0].encode()
        raw.price      = price
        raw.amount     = amount
        self._shm.write_request(MsgId.kPlaceOrder, bytes(raw))
        return private_no

    def cancel_order(self, market: str, account_id: str,
                     instrument: str, entno: int) -> None:
        """发送撤单请求。

        Args:
            market:     市场
            account_id: 账户ID
            instrument: 标的
            entno:      要撤销的委托号
        """
        req = WithdrawReq()
        req.market     = market.encode()
        req.account_id = account_id.encode()
        req.instrument = instrument.encode()
        req.entno      = entno
        self._shm.write_request(MsgId.kCancelOrder, bytes(req))

    def set_timer(self, timer_id: int, delay_ms: int,
                  repeat: bool = False) -> None:
        """注册定时器。

        Args:
            timer_id:  定时器ID（由策略自定义，用于在 on_timer 中区分）
            delay_ms:  触发延迟（毫秒）
            repeat:    True 则周期触发，False 则单次触发
        """
        req = SetTimerReq()
        req.setter_id  = self._strat_id.encode()
        req.timer_id   = timer_id
        req.delay_ms   = delay_ms
        req.repeat     = repeat
        self._shm.write_request(MsgId.kSetTimer, bytes(req))

    def kill_timer(self, timer_id: int) -> None:
        """取消定时器。

        Args:
            timer_id: 要取消的定时器ID
        """
        req = TimerKey()
        req.setter_id = self._strat_id.encode()
        req.timer_id  = timer_id
        self._shm.write_request(MsgId.kKillTimer, bytes(req))
