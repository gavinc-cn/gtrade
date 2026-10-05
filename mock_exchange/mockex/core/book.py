"""多档订单簿：价格优先 + 同价 FIFO —— mock 交易所的**唯一真相**。

盘口（bbo 推送、REST 查盘口、`/admin/state`）全从这里派生，不存在第二份行情状态。
本模块交易所无关：只认 `mockex.core.models` 的 `Order`，不认识任何 OKX 拼写。

结构 `inst_id → side → 价格 → [挂单]`，列表顺序即 FIFO（下标 0 最先成交）。mock 场景下
价位数与挂单数都是个位数量级，故最优价直接 `max()`/`min()`，不引跳表/堆。

三条不变量（`place`/`cancel`/`remove_owner`/`consume_best`/`clear` 共同维护）：

1. `_by_cl_ord_id` 与 `_levels` 同生共死 —— 撤单靠索引 O(1) 命中，不会"索引里有、簿上没有"；
2. 档位量恒等于 Σ(挂单 `sz - acc_fill_sz`)，**不单独记账**，故被吃/被撤后盘口自适应；
3. 簿上只有未吃完的挂单 —— 吃完（`acc_fill_sz >= sz`）时**档位与撤单索引两处一起摘**
   （只摘一处就会让 `cancel`/`remove_owner` 击穿 KeyError 或误标 `canceled`）。
"""

from decimal import Decimal

from mockex.core.models import (
    SIDE_BUY,
    SIDE_SELL,
    STATE_CANCELED,
    STATE_FILLED,
    STATE_PARTIALLY_FILLED,
    Order,
)

__all__ = ["Level", "OrderBook"]

# 盘口一档 `(px, sz)`：sz 是该价位挂单的**剩余**量之和
Level = tuple[float, float]


def _d(value) -> Decimal:
    """float → Decimal（经 `repr`，即"最短往返"十进制），用于量价累加。

    `Decimal(repr(0.3))` 就是 `0.3`，而 `Decimal(0.3)` 是 0.2999999999999999888…；
    直接 float 乘加会留长尾（0.1+0.2 → 0.30000000000000004）。内核层不能 import
    协议层（`codec.num` 里有同一表达式），故这个小工具在两处各留一份。
    """
    return Decimal(repr(float(value)))


class OrderBook:
    """多档订单簿：一个实例管全部合约。

    写入方：管理面（`/admin/order`、`/admin/depth` 注入）与撮合引擎。`orderbook` 模式下引擎
    委托的未成交余量也是挂单；`immediate` 模式下引擎委托**不进也不吃簿**，只有撤单（撤簿上
    的挂单）会写它。
    """

    def __init__(self) -> None:
        # inst_id → side → px → 该价位挂单（FIFO 序）
        self._levels: dict[str, dict[str, dict[float, list[Order]]]] = {}
        self._by_cl_ord_id: dict[str, Order] = {}

    # ── 挂单 / 撤单 ──────────────────────────────────────────────────────────

    def place(self, order: Order) -> None:
        """把一张委托挂进订单簿（注入行情与 `/admin/order` 造盘口走这里）。

        Args:
            order: 待挂委托；`side` 取 `SIDE_BUY`/`SIDE_SELL`，`px`/`sz` 必须为正。

        Raises:
            ValueError: 方向非法、价量非正、或 `cl_ord_id` 已在簿 —— 重复 clOrdId 会让
                撤单索引指向两张单，宁可响亮报错（真实 OKX 也拒）。
        """
        if order.side not in (SIDE_BUY, SIDE_SELL):
            raise ValueError(f"Order(cl_ord_id={order.cl_ord_id!r}) 方向非法: {order.side!r}")
        if order.px <= 0 or order.sz <= 0:
            raise ValueError(
                f"Order(cl_ord_id={order.cl_ord_id!r}) 价量必须为正: px={order.px} sz={order.sz}"
            )
        if order.cl_ord_id in self._by_cl_ord_id:
            raise ValueError(f"clOrdId 重复: {order.cl_ord_id!r} 已在簿上")
        sides = self._levels.setdefault(order.inst_id, {SIDE_BUY: {}, SIDE_SELL: {}})
        sides[order.side].setdefault(order.px, []).append(order)
        self._by_cl_ord_id[order.cl_ord_id] = order

    def cancel(self, cl_ord_id: str) -> bool:
        """按 clOrdId 撤单：命中则摘出订单簿并把 `state` 标成 `canceled`，返回是否命中。

        `u_time` 由调用方补 —— 账簿不持有时钟（`MatchingEngine.cancel` 只是透传到这里）。

        Args:
            cl_ord_id: 客户端委托号。

        Returns:
            bool：True = 确有挂单被撤；False = 未命中（已成交/从未挂过/已撤过）。
        """
        order = self._by_cl_ord_id.pop(cl_ord_id, None)
        if order is None:
            return False
        levels = self._levels.get(order.inst_id, {}).get(order.side, {})
        level = levels.get(order.px)
        if level is None:
            return False  # 索引脏了（不该发生）：当未命中，索引已顺手丢掉，state 不动
        for idx, resting in enumerate(level):
            if resting is order:  # 按身份摘除（dataclass 的 == 是逐字段比较，会误伤同形单）
                del level[idx]
                if not level:
                    del levels[order.px]
                order.state = STATE_CANCELED  # 只有真摘掉了才标终态
                return True
        return False  # 档位里没有这张单（不该发生）：同样当未命中，绝不改 state

    def remove_owner(self, owner: str, inst_id: str | None = None) -> int:
        """整批撤掉某 owner 的挂单，返回撤掉的张数。

        `/admin/depth` 的替换语义（先撤同源上一批、再挂新的）与 `/admin/reset` 靠它。

        Args:
            owner: 归属标识，如注入方 "mock_mm"。
            inst_id: 限定合约；省略 = 该 owner 的全部合约。
        """
        doomed = [o.cl_ord_id for o in self._by_cl_ord_id.values()
                  if o.owner == owner and (inst_id is None or o.inst_id == inst_id)]
        for cl_ord_id in doomed:  # 先取快照再撤（cancel 会改 _by_cl_ord_id）
            self.cancel(cl_ord_id)
        return len(doomed)

    def clear(self) -> None:
        """清空订单簿与撤单索引（`/admin/reset` 回到干净环境）。"""
        self._levels.clear()
        self._by_cl_ord_id.clear()

    # ── 盘口查询 ─────────────────────────────────────────────────────────────

    def best(self, inst_id: str) -> tuple[Level | None, Level | None]:
        """最优一档 `(bid, ask)`，某侧无挂单时该侧为 None。

        消费方：bbo 推送（T6）、REST 查盘口（T8）。顺序恒为买前卖后。
        """
        return self._top(inst_id, SIDE_BUY), self._top(inst_id, SIDE_SELL)

    def depth(self, inst_id: str, n: int | None = 10) -> dict[str, list[Level]]:
        """`{"bids": [...], "asks": [...]}`：买降序、卖升序，各取前 n 档（None = 全部）。"""
        bids = self._sorted_levels(inst_id, SIDE_BUY)
        asks = self._sorted_levels(inst_id, SIDE_SELL)
        if n is not None:
            bids, asks = bids[:n], asks[:n]
        return {"bids": bids, "asks": asks}

    def snapshot(self) -> dict[str, dict[str, list[Level]]]:
        """全部合约的全深度盘口，按 instId 排序 —— `/admin/state` 直接 `json.dumps` 它。"""
        return {inst_id: self.depth(inst_id, None) for inst_id in sorted(self._levels)}

    # ── 撮合侧接口：消耗对手档位 ─────────────────────────────────────────────

    def consume_best(self, inst_id: str, side: str, sz: float, ts: int) -> tuple[float | None, str]:
        """按 FIFO 吃掉 `side` 侧最优档，最多吃掉该档全部剩余量（**不跨档**）。

        撮合侧"严格吃单"原语（M2 `orderbook` 模式的地基；M1 immediate 已不使用——引擎成交
        不动订单簿）：只吃最优档、不跨档；被吃的挂单按自身价格推进
        `acc_fill_sz`/`filled_amount` 并标 `filled`/`partially_filled`，吃完即移除。

        Args:
            inst_id: 合约。
            side: **被吃的一侧**（引擎买入 → 吃 `SIDE_SELL` 侧）。
            sz: 想吃掉的数量。超过该档剩余量时按剩余量截断 —— 订单簿不会凭空产量，
                immediate 里多出来的成交量属隐含流动性，由 `MatchingEngine` 记在主动方头上。
            ts: 成交时间（毫秒 epoch），写进被吃挂单的 `u_time`。

        Returns:
            `(最优档价, 对手方 owner)`；`sz <= 0` 或该侧无挂单时 `(None, "")`。
            owner 取 FIFO 队首挂单的 owner（同价多 owner 时即最先成交的那一张）。
        """
        levels = self._levels.get(inst_id, {}).get(side)
        if sz <= 0 or not levels:
            return None, ""
        px = max(levels) if side == SIDE_BUY else min(levels)
        orders = levels[px]
        maker_owner = orders[0].owner

        remaining = _d(sz)
        survivors: list[Order] = []
        for idx, resting in enumerate(orders):
            if remaining <= 0:
                survivors.extend(orders[idx:])  # 没轮到，原样保留（FIFO 序不变）
                break
            take = min(_d(resting.sz) - _d(resting.acc_fill_sz), remaining)
            resting.acc_fill_sz = float(_d(resting.acc_fill_sz) + take)
            resting.filled_amount = float(_d(resting.filled_amount) + take * _d(resting.px))
            resting.u_time = ts
            if _d(resting.acc_fill_sz) >= _d(resting.sz):
                resting.state = STATE_FILLED  # 吃完即离开簿：档位与撤单索引两处一起摘
                self._by_cl_ord_id.pop(resting.cl_ord_id, None)
            else:
                resting.state = STATE_PARTIALLY_FILLED
                survivors.append(resting)
            remaining -= take

        if survivors:
            levels[px] = survivors
        else:
            del levels[px]
        return px, maker_owner

    # ── 内部：档位视图 ───────────────────────────────────────────────────────

    def _top(self, inst_id: str, side: str) -> Level | None:
        """该侧最优档 `(px, 剩余量)`；无挂单为 None。"""
        levels = self._levels.get(inst_id, {}).get(side)
        if not levels:
            return None
        px = max(levels) if side == SIDE_BUY else min(levels)
        return (px, self._level_size(levels[px]))

    def _sorted_levels(self, inst_id: str, side: str) -> list[Level]:
        """该侧全部档位，按优劣排序（买降序 / 卖升序）。"""
        levels = self._levels.get(inst_id, {}).get(side, {})
        prices = sorted(levels, reverse=(side == SIDE_BUY))
        return [(px, self._level_size(levels[px])) for px in prices]

    @staticmethod
    def _level_size(orders: list[Order]) -> float:
        """档位剩余量 = Σ(`sz - acc_fill_sz`)，走 Decimal 免得 0.1+0.2 冒长尾。"""
        total = Decimal(0)
        for order in orders:
            total += _d(order.sz) - _d(order.acc_fill_sz)
        return float(total)
