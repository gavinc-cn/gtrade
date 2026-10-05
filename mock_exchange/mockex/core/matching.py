"""撮合引擎：把引擎委托变成成交，并推进订单簿。

两种模式（`config/mock.yml` 的 `fill_mode`），逐条对应 spec 的「下单与成交语义」表：

- **`immediate`**（"单必成"）：订单**不进订单簿**、立即**全量成交**、不校验对手挂单量、
  不校验价格交叉（相当于隐含流动性）、成交价**恒为订单自身价格**，且**完全不影响订单簿**
  （2026-09-25 语义修订，原为"取对手最优价、消耗对手最优档"）：流动性模型是"下单瞬间有
  新对手委托进场把本单吃掉"，不吃簿上既有挂单——对手盘挂单量不变，盘口无变化、无 book
  事件（行情只由管理面注入驱动）。
- **`orderbook`**（M2 起实现）：订单**进簿**，先按**价格-时间优先**吃对手价内挂单
  （多档逐档、可部分成交），成交价 = **对手挂单价**，余量**挂回本侧**成为盘口挂单；
  只吃价格可接受的档位（买单吃价 ≤ 委托价的卖档，卖单反之），不交叉即纯挂单。
  挂单与成交都改变订单簿 → 由调用方（协议层）发布 `book.<instId>`。

两个模式共用同一套入参守卫（只认限价单、方向合法、价量为正），差别只在 `place()` 分派到的
私有实现；`uses_book` 供协议层判断"下单后要不要发盘口事件"。

账户与持仓不在本模块：成交以 `Fill`（含 `side`/`taker_owner`/`maker_owner`）返回给调用方，
由 T4 的 `AccountManager.apply_fill` 结算；事件发布也归调用方，内核不耦合协议层。
"""

import time
from decimal import Decimal

from mockex.core.book import OrderBook
from mockex.core.models import (
    SIDE_BUY,
    SIDE_SELL,
    STATE_FILLED,
    STATE_LIVE,
    STATE_PARTIALLY_FILLED,
    Fill,
    Order,
)

__all__ = ["FILL_MODE_IMMEDIATE", "FILL_MODE_ORDERBOOK", "MatchingEngine"]

# `fill_mode` 取值（对应 config/mock.yml 的同名键）
FILL_MODE_IMMEDIATE = "immediate"
FILL_MODE_ORDERBOOK = "orderbook"

# 合法方向集合（历史名 `_COUNTER_SIDE` 保留：M2 orderbook 模式将恢复"对手侧"语义）
_COUNTER_SIDE = {SIDE_BUY: SIDE_SELL, SIDE_SELL: SIDE_BUY}

# 只支持限价单：仓库内所有策略下单都用 PriceType::Limit（`src/common/dict_mapping.cpp:13`）
_SUPPORTED_ORD_TYPE = "limit"


def _d(value) -> Decimal:
    """float → Decimal（经 `repr`）—— 与 `mockex.core.book._d` 同源，理由见彼处。"""
    return Decimal(repr(float(value)))


def _now_ms() -> int:
    """默认时钟：真实毫秒 epoch（测试注入固定值，M3 的虚拟时钟也从 `clock` 接）。"""
    return int(time.time() * 1000)


class MatchingEngine:
    """撮合引擎：`place(order) -> list[Fill]`、`cancel(cl_ord_id) -> bool`。

    只依赖 `OrderBook`（不引事件总线与账户），故可脱离协议层直接被单测驱动。
    """

    def __init__(self, book: OrderBook, fill_mode: str = FILL_MODE_IMMEDIATE, clock=None) -> None:
        """Args:
            book: 订单簿（唯一真相）。
            fill_mode: 撮合模式；未知取值直接 `ValueError` —— 配置写错（如 "immediate "）
                要在启动时就炸，而不是静默按默认行为跑。
            clock: 取当前毫秒 epoch 的可调用对象，缺省真实时钟；测试注入固定值。
        """
        if fill_mode not in (FILL_MODE_IMMEDIATE, FILL_MODE_ORDERBOOK):
            raise ValueError(
                f"fill_mode 非法: {fill_mode!r}（应为 {FILL_MODE_IMMEDIATE!r} 或 "
                f"{FILL_MODE_ORDERBOOK!r}）"
            )
        self._book = book
        self._fill_mode = fill_mode
        self._clock = clock or _now_ms

    @property
    def fill_mode(self) -> str:
        """当前撮合模式（可观测：`/admin/state` 与排障都用）。"""
        return self._fill_mode

    @property
    def uses_book(self) -> bool:
        """本模式下撮合是否读写订单簿 —— 协议层据此决定下单后要不要发 `book.<instId>`。

        `immediate` 不动簿（无盘口变化）；`orderbook` 挂单与成交都改簿，必须推 bbo。
        """
        return self._fill_mode == FILL_MODE_ORDERBOOK

    def place(self, order: Order) -> list[Fill]:
        """送一张委托进撮合，原地推进委托状态并返回本次成交。

        两个模式共用入参守卫，之后按 `fill_mode` 分派：`immediate` → `_place_immediate`
        （不进簿、按订单价全成）；`orderbook` → `_place_orderbook`（吃对手价内挂单、余量挂单）。

        Args:
            order: 引擎委托；只支持限价单（`ord_type == "limit"`），`side` 取
                `SIDE_BUY`/`SIDE_SELL`，`px`/`sz` 必须为正。

        Returns:
            list[Fill]：本次成交 —— immediate 恒 1 笔（全量成交，成交价唯一）；orderbook
            每个被吃档位一笔，可能是空列表（完全不成交、整单挂进簿）。

        Raises:
            NotImplementedError: 非限价单（市价/PostOnly 等未实现路径，不能静默乱成交）。
            ValueError: `side` 非法、`px`/`sz` 非正。
        """
        if order.ord_type != _SUPPORTED_ORD_TYPE:
            raise NotImplementedError(
                f"只支持限价单，收到 ord_type={order.ord_type!r}（Order.cl_ord_id={order.cl_ord_id!r}）"
            )
        if order.side not in _COUNTER_SIDE:
            raise ValueError(f"Order(cl_ord_id={order.cl_ord_id!r}) 方向非法: {order.side!r}")
        if order.sz <= 0:
            raise ValueError(f"Order(cl_ord_id={order.cl_ord_id!r}) sz 必须为正: {order.sz}")
        if order.px <= 0:
            # 放行 px=0 会在空盘口下成交在 0：累计成交额变成 0，而 codec 的 avgPx
            # 会在远处抛"有量无额"—— 错在源头就该报
            raise ValueError(f"Order(cl_ord_id={order.cl_ord_id!r}) px 必须为正: {order.px}")

        ts = self._clock()
        if self._fill_mode == FILL_MODE_ORDERBOOK:
            return self._place_orderbook(order, ts)
        return self._place_immediate(order, ts)

    def _place_immediate(self, order: Order, ts: int) -> list[Fill]:
        """immediate：不读也不写订单簿，按订单自身价格全量成交（见模块文档）。"""
        # 流动性是下单瞬间凭空出现的隐含对手单（2026-09-25 语义修订，原为"消耗对手最优档"）
        # ——成交价恒为订单自身价格，对手盘挂单量保持不变
        px = order.px

        order.acc_fill_sz = float(_d(order.acc_fill_sz) + _d(order.sz))  # 全量成交
        order.filled_amount = float(_d(order.filled_amount) + _d(px) * _d(order.sz))
        order.state = STATE_FILLED
        order.u_time = ts
        return [
            Fill(
                ord_id=order.ord_id,
                inst_id=order.inst_id,
                px=px,
                sz=order.sz,
                ts=ts,
                taker_owner=order.owner,
                maker_owner="",  # 隐含对手不是簿上真实账户：结算侧据空串跳过对手账户
                side=order.side,  # T4 结算靠它判资金方向，漏填会静默出错
            )
        ]

    def _place_orderbook(self, order: Order, ts: int) -> list[Fill]:
        """orderbook：价格-时间优先吃对手价内挂单（多档、可部分成交），余量挂回本侧。

        成交量以对手档位剩余量为上限（订单簿不凭空产量）；每笔成交价取**该档挂单价**，
        被动方 owner 取该档 FIFO 队首挂单的 owner（同一档跨多张挂单时按 FIFO 逐张吃）。
        不做自成交保护（STP）：同一账户两侧照常各记一笔，净额自然抵消。

        Args:
            order: 已过公共守卫的限价单。
            ts: 成交/变更时间（毫秒 epoch）。

        Returns:
            list[Fill]：每吃一档一笔；完全挂单（无成交）时为空列表。
        """
        counter_side = _COUNTER_SIDE[order.side]   # 被吃的一侧（买单吃卖档）
        fills: list[Fill] = []
        remaining = _d(order.sz)
        while remaining > 0:
            counter = self._acceptable_best(order.inst_id, counter_side, order.px)
            if counter is None:      # 对手侧空 or 最优档已超出委托价 → 余量转挂单
                break
            px, level_sz = counter
            take = min(remaining, _d(level_sz))
            taken_px, maker_owner = self._book.consume_best(
                order.inst_id, counter_side, float(take), ts)
            if taken_px is None:     # 理论不可达（take>0 时档位必被吃）：只防死循环
                break
            order.acc_fill_sz = float(_d(order.acc_fill_sz) + take)
            order.filled_amount = float(_d(order.filled_amount) + take * _d(px))
            fills.append(
                Fill(
                    ord_id=order.ord_id,
                    inst_id=order.inst_id,
                    px=px,
                    sz=float(take),
                    ts=ts,
                    taker_owner=order.owner,
                    maker_owner=maker_owner,  # 真实对手：结算侧会同时结算挂单方账户
                    side=order.side,
                )
            )
            remaining -= take

        order.u_time = ts
        if remaining <= 0:
            order.state = STATE_FILLED
            return fills
        order.state = STATE_PARTIALLY_FILLED if fills else STATE_LIVE
        self._book.place(order)   # 余量挂回本侧：档位量由 sz - acc_fill_sz 自然算出
        return fills

    def _acceptable_best(self, inst_id: str, side: str, limit_px: float) -> tuple[float, float] | None:
        """对手侧「价格可接受」的最优档 `(px, 剩余量)`；无挂单或超出限价时 None。

        Args:
            inst_id: 合约。
            side: **被吃的一侧**（引擎买入 → `SIDE_SELL`）。
            limit_px: 主动方限价 —— 卖档价高于买价（或买档价低于卖价）即不成交。
        """
        bid, ask = self._book.best(inst_id)
        counter = ask if side == SIDE_SELL else bid
        if counter is None:
            return None
        px, sz = counter
        if sz <= 0:
            return None
        if side == SIDE_SELL and px > limit_px:   # 卖一高于买价：买不成交
            return None
        if side == SIDE_BUY and px < limit_px:    # 买一低于卖价：卖不成交
            return None
        return counter

    def cancel(self, cl_ord_id: str) -> bool:
        """撤单（透传到订单簿），返回是否确有挂单被撤。

        `orderbook` 下引擎委托的余量就在簿上，撤得到；`immediate` 下引擎委托从没进过簿，
        故恒为 False —— 调用方（T7）据此回撤单失败。
        """
        return self._book.cancel(cl_ord_id)
