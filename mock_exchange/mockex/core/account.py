"""账户结算：由成交驱动余额与持仓（M1 是现货 cash 语义）。

**结算只认 `Fill`**（`mockex.core.models`）—— 撮合引擎不持有账户，成交事实由调用方转交：

    for fill in engine.place(order):
        changed = accounts.apply_fill(fill)   # 返回被改动的账户名，供调用方发事件

现货语义：买入 → base ccy 增 `sz`、quote ccy 减 `px*sz`；卖出反之。持仓按合约净额（`pos`）
记账，**负数是合法的**（卖空与 M2 的衍生品同一条路径），不做保证金校验。

四处刻意的取舍：

1. **base/quote 只从合约规格取**（构造时传入的 `instruments`），不拆 `instId` 字符串 ——
   `BTC-USDT-SWAP` 这类会拆错结算币种；未知合约当场 `ValueError`，不猜；
2. **不持有事件总线**：`apply_fill` 返回被改动的账户名，由调用方（T7 的 ws_private）据此
   发布 `account.<account>` —— 与 T3「内核不耦合协议层」的裁定一致；
3. **未在配置里的 owner 按需建账户**（余额从 0 起）：注入方 "mock_mm" 是合法对手方，
   其余额可能为负（注入的流动性不预扣资金），可见胜过静默丢弃对手侧结算；
4. **时间只取 `Fill.ts`**（毫秒 epoch），初始余额 `uTime` 为 0 —— 无墙上时钟依赖，
   同一状态下快照逐字节稳定，测试可精确断言。

`snapshot()` 的字段结构逐字对齐 OKX `balance_and_position` 推送的 `data[]` 元素（T7 直接
拿它拼帧；引擎读法见 `src/websocket/okx_trade.cpp:538` 起）：`balData[].{ccy,cashBal,uTime}`、
`posData[].{instId,instType,mgnMode,posSide,pos,uTime,avgPx}`。**所有数值一律字符串** —— 引擎
用 `GetString()` 读，数字在 Debug 构建直接 abort。core 层不能 import 协议层（`codec.num`
里有同一表达式），故照 `book._d` 的先例在本文件另留一份。

持仓均价（`avgPx`）是开仓方向的加权均价：加仓按量加权、减仓不动、持平归 0、穿仓（正负翻转）
按本笔成交价重置。
"""

from dataclasses import dataclass
from decimal import Decimal

from mockex.core.instrument_index import InstrumentSpecIndex
from mockex.core.models import SIDE_BUY, SIDE_SELL

__all__ = ["AccountManager"]

# 主动方 → 被动方方向（`Fill.side` 是主动方方向，maker 按反向结算）
_COUNTER_SIDE = {SIDE_BUY: SIDE_SELL, SIDE_SELL: SIDE_BUY}

# 持仓帧里的固定值：M1 只有 SPOT cash/net（M2 的合约由 Order 的 tdMode/posSide 驱动）
_MGN_MODE = "cash"
_POS_SIDE = "net"


def _d(value) -> Decimal:
    """float / str / Decimal → Decimal（经 `repr`，与 `book._d` 同源同理由）。"""
    return Decimal(repr(float(value)))


def _num(value) -> str:
    """数量 / 金额 → OKX 字符串（十进制展开、无科学计数法、整数去掉 `.0`）。

    与 `codec.num` 同规则：`1e-08` 这类科学计数法漏进报文引擎不能读；`_num(Decimal(0))`
    要出 `"0"` 而不是 `"-0"`。core 不能反向 import 协议层（`codec` 已 import `core.models`），
    故与 `book._d`、`matching._d` 同理在本文件另留一份。
    """
    text = format(Decimal(repr(float(value))), "f")
    if text.endswith(".0"):
        text = text[:-2]
    return "0" if text == "-0" else text


@dataclass
class _Balance:
    """一个币种的现金余额与最后变动时间（毫秒 epoch；从未变动过为 0）。"""

    cash: Decimal
    u_time: int = 0


@dataclass
class _Position:
    """一个合约的净持仓（正 = 多，负 = 空）与开仓均价。"""

    inst_type: str
    pos: Decimal
    avg_px: Decimal
    u_time: int = 0


class AccountManager:
    """多账户账本：`apply_fill` 结算、`snapshot` 出快照、`reset` 回初始。

    账户以 owner 为键（引擎侧即 `api_key`）；全部状态都派生自成交，不存在第二份真相。
    """

    def __init__(self, accounts=(), instruments=()) -> None:
        """Args:
            accounts: `mockex.config.AccountConfig` 序列（api_key + 初始余额）。
            instruments: `mockex.config.InstrumentConfig` 序列 —— 结算必须知道合约的
                base/quote 币种与 `instType`，故未在此登记的合约会拒绝结算（不猜）。
        """
        # inst_id → (base_ccy, quote_ccy, inst_type)
        # 结算币种按 instId 单键取：MARGIN 与 SPOT 共用 instId 且 base/quote 相同（OKX 语义），
        # 故同 instId 只留一条即可；用 InstrumentSpecIndex 保证留下的是首选规格（SPOT 优先），
        # 否则现货成交会把持仓帧的 instType 标成 MARGIN（`_apply_position` 首次建仓才写 inst_type）。
        self._inst: dict[str, tuple[str, str, str]] = {}
        for spec in InstrumentSpecIndex(instruments).preferred():
            self._inst[spec.inst_id] = (spec.base_ccy, spec.quote_ccy, spec.inst_type)
        # 初始余额冻结一份：reset 从这里重建，调用方事后改配置不影响已建账本
        self._seed: dict[str, dict[str, Decimal]] = {
            account.api_key: {ccy: _d(cash) for ccy, cash in (account.balances or {}).items()}
            for account in accounts
        }
        self._balances: dict[str, dict[str, _Balance]] = {}
        self._positions: dict[str, dict[str, _Position]] = {}
        self.reset()

    def reset(self) -> None:
        """回到构造时的初始状态（`/admin/reset` 用；按需建出的账户一并丢弃）。"""
        self._balances = {
            name: {ccy: _Balance(cash) for ccy, cash in balances.items()}
            for name, balances in self._seed.items()
        }
        self._positions = {name: {} for name in self._seed}

    def apply_fill(self, fill) -> list[str]:
        """结算一笔成交的**两侧**，返回被改动的账户名（去重，主动方在前）。

        Args:
            fill: `mockex.core.models.Fill`。`side` 是**主动方**方向，被动方按反向结算；
                `maker_owner` 为空串 = 空盘口下的隐含流动性（无真实对手），只结算主动方。

        Returns:
            list[str]：本次余额/持仓有变化的账户名（供调用方发布 `account.<account>`）。

        Raises:
            ValueError: `taker_owner` 为空（主动方必然存在：`Order.owner` 默认空串、撮合原样透传，
                漏盖 owner 会让"成交了但账户没动"静默发生，故当场报）、`side` 非 buy/sell、
                `px`/`sz` 非正（含 NaN/inf）、或 `inst_id` 未登记（结算币种未知 = 账要记错）。
        """
        # 主动方缺失 = 这笔成交没有任何账户该记账。空 `maker_owner` 才是合法的（immediate 空盘口
        # 隐含流动性，见类文档），两者不能套同一个守卫：静默返回 [] 会产出最难查的账实不一致。
        if not fill.taker_owner:
            raise ValueError(
                f"Fill(ord_id={fill.ord_id!r}) 缺少主动方 owner：成交必须有账户承接，"
                "请在构造 Order / Fill 时盖上 owner（引擎账户的 api_key）"
            )
        if fill.side not in _COUNTER_SIDE:
            raise ValueError(f"Fill(ord_id={fill.ord_id!r}) 方向非法: {fill.side!r}")
        px, sz = _d(fill.px), _d(fill.sz)
        # `is_finite` 一并挡住 NaN/inf：它们过得了 `<= 0`（NaN 比较恒 False，inf 恒正），
        # 却会把 "NaN"/"Infinity" 这种字符串漏进推送帧
        if not (px.is_finite() and sz.is_finite() and px > 0 and sz > 0):
            raise ValueError(
                f"Fill(ord_id={fill.ord_id!r}) 价量必须为正的有限值: px={fill.px} sz={fill.sz}"
            )
        spec = self._inst.get(fill.inst_id)
        if spec is None:
            raise ValueError(
                f"未登记的合约 {fill.inst_id!r}：结算需要 base/quote 币种，请加进 instruments"
            )
        base, quote, inst_type = spec

        # 主动方必存在（上面已守卫）：先结算主动方，再按需结算被动方
        self._settle(fill.taker_owner, fill.inst_id, inst_type, base, quote,
                     fill.side, px, sz, fill.ts)
        changed = [fill.taker_owner]
        if fill.maker_owner:
            # 自成交（同一账户两侧）照常两笔记账、净额自然抵消，只是改动账户只报一次
            self._settle(fill.maker_owner, fill.inst_id, inst_type, base, quote,
                         _COUNTER_SIDE[fill.side], px, sz, fill.ts)
            if fill.maker_owner not in changed:
                changed.append(fill.maker_owner)
        return changed

    def snapshot(self, account: str | None = None):
        """账户快照，结构对齐 OKX `balance_and_position` 的 `data[]` 元素。

        Args:
            account: 账户名；省略则返回全部账户 `{账户名: 元素}`（`/admin/state` 用）。

        Returns:
            dict | dict[str, dict]：`{"uTime", "balData":[{"ccy","cashBal","uTime"}],
            "posData":[{"instId","instType","mgnMode","posSide","pos","uTime","avgPx"}]}`，
            数值一律字符串，币种/合约按名字排序。T7 拼推送帧的写法：
            `{"arg": {"channel": "balance_and_position"}, "data": [snapshot(account)]}`。

        Raises:
            KeyError: 账户不存在（调用方据此判"这个 api_key 没有账户"）。
        """
        if account is not None:
            return self._element(account)
        return {name: self._element(name) for name in sorted(self._balances)}

    def _element(self, account: str) -> dict:
        """单账户的 `data[]` 元素（每次新建字典，调用方改它不会污染账本）。"""
        balances = self._balances.get(account)
        if balances is None:
            raise KeyError(f"未知账户: {account!r}")
        positions = self._positions[account]
        bal_data = [{"ccy": ccy, "cashBal": _num(balances[ccy].cash), "uTime": str(balances[ccy].u_time)}
                    for ccy in sorted(balances)]
        pos_data = [{"instId": inst_id, "instType": positions[inst_id].inst_type, "mgnMode": _MGN_MODE,
                     "posSide": _POS_SIDE, "pos": _num(positions[inst_id].pos),
                     "uTime": str(positions[inst_id].u_time), "avgPx": _num(positions[inst_id].avg_px)}
                    for inst_id in sorted(positions)]
        u_times = [bal.u_time for bal in balances.values()]
        u_times += [pos.u_time for pos in positions.values()]
        return {"uTime": str(max(u_times, default=0)), "balData": bal_data, "posData": pos_data}

    def _settle(self, owner, inst_id, inst_type, base, quote, side, px, sz, ts) -> None:
        """结算某账户的一侧成交：改 base/quote 两个币种的余额 + 该合约持仓。"""
        balances = self._balances.get(owner)
        if balances is None:  # 未配置的 owner（注入方）：按需建账户，余额从 0 起
            balances = self._balances[owner] = {}
            self._positions[owner] = {}
        notional = px * sz
        delta = sz if side == SIDE_BUY else -sz
        self._credit(balances, base, delta, ts)
        self._credit(balances, quote, -notional if side == SIDE_BUY else notional, ts)
        self._apply_position(self._positions[owner], inst_id, inst_type, delta, px, ts)

    @staticmethod
    def _credit(balances: dict[str, _Balance], ccy: str, delta: Decimal, ts: int) -> None:
        """改一个币种的余额与最后变动时间（币种首次出现就建条目，可为负）。"""
        balance = balances.get(ccy)
        if balance is None:
            balances[ccy] = _Balance(delta, ts)
        else:
            balance.cash += delta
            balance.u_time = ts

    @staticmethod
    def _apply_position(positions: dict[str, _Position], inst_id: str, inst_type: str,
                        delta: Decimal, px: Decimal, ts: int) -> None:
        """推进净持仓与开仓均价（规则见模块文档；除法取 Decimal 默认 28 位精度）。"""
        position = positions.get(inst_id)
        if position is None:
            positions[inst_id] = _Position(inst_type, delta, px, ts)
            return
        old_pos, old_avg = position.pos, position.avg_px
        new_pos = old_pos + delta
        position.pos = new_pos
        position.u_time = ts
        if new_pos == 0:
            position.avg_px = Decimal(0)  # 平仓：均价归 0，避免策略侧留下过期成本
        elif (old_pos > 0) == (new_pos > 0):
            if abs(new_pos) > abs(old_pos):  # 同向加仓：按量加权
                position.avg_px = (abs(old_pos) * old_avg + abs(delta) * px) / abs(new_pos)
            # 同向减仓：均价不动（成本价不因平仓改变）
        else:  # 穿仓（多空翻转）：新方向按本笔成交价重新起算
            position.avg_px = px
