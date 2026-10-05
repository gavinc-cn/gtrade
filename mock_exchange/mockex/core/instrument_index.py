"""合约规格索引：唯一键是 `(instId, instType)`，委托帧缺 `instType` 时按规则解析。

**为什么需要双键**：`instType` 是标的唯一性的一部分 —— 币币杠杆（MARGIN）复用 SPOT 的
`instId` **和** `instIdCode`（实测 266 对逐字段相同，如 `BTC-USDT` 两边都是 3），只有
`instType` 能区分。老实现 `{spec.inst_id: spec}` 是后写覆盖先写，SPOT 与 MARGIN 同 instId 时
每个查询点各留一条，导致委托帧的 `instType` 被标错、精度校验用错 `tickSz/lotSz/minSz`。

本 mock 的出厂清单已不含 MARGIN（本系统不做杠杆，见
`doc_ai/plan/202610/20261003_1750_标的类型纳入联合主键并移除MARGIN.md`），双键仍然保留：
它是 `(market, instId, instType)` 三级唯一性在 mock 侧的落点，也是将来补抓品种时的护栏。

**委托帧为什么需要解析规则**：引擎的 WS 下单报文只带
`instId/tdMode/clOrdId/side/posSide/ordType/sz/px/instIdCode`，**不带 `instType`**
（见 `tests/test_tls.py` 的 `ENGINE_ORDER`），所以必须能从 `instId`（必要时加 `tdMode`）
定位到唯一规格。顺序见 `InstrumentSpecIndex.resolve`。

**tdMode 的两套语义（这里曾出过错）**：对 SPOT，`tdMode` 区分"现货(cash) vs 币币杠杆
(cross/isolated)"；对 SWAP/FUTURES，它区分的是**合约保证金模式**（cross/isolated），
与 `instType` 无关。所以不能简单地把 `{"cross": "MARGIN"}` 当作 tdMode→instType 映射 ——
合约策略发的 `tdMode=isolated` 会被错标成 MARGIN。正确做法是**先按 instId 定候选，
tdMode 只用于消歧与合法性校验**。

调用方：
- `exchanges/okx/ws_private.py`：委托帧 → 规格（`resolve`）；
- `protocol/admin.py`：管理面注入/查单 → 规格（`resolve`）；
- `core/account.py`：结算币种表按 instId 单键建映射，取 `preferred()`。
"""

from __future__ import annotations

from mockex.config import InstrumentConfig

__all__ = ["InstrumentSpecIndex", "SpecLookupError", "TD_MODE_ALLOWED_TYPES"]

# instType 优先级：同 instId 有多条时用于确定性取首选（现货优先，与"同 instId 时假定现货"一致）
_TYPE_PRIORITY = {"SPOT": 0, "SWAP": 1, "FUTURES": 2, "MARGIN": 3, "OPTION": 4}

# 各 instType 允许的 tdMode：
#   SPOT         —— 只有 cash；cross/isolated 表示币币杠杆，本 mock 不提供（协议层报错）
#   SWAP/FUTURES —— 合约保证金模式，只能 cross/isolated
#   MARGIN       —— 币币杠杆，出厂清单不含；若人工补抓了 MARGIN，按杠杆语义收 cross/isolated
#   OPTION       —— 同合约
_ALLOWED_TD_MODES = {
    "SPOT": frozenset({"cash"}),
    "MARGIN": frozenset({"cross", "isolated"}),
    "SWAP": frozenset({"cross", "isolated"}),
    "FUTURES": frozenset({"cross", "isolated"}),
    "OPTION": frozenset({"cross", "isolated"}),
}

# 反查表：tdMode → 它允许的 instType 集合（多候选消歧用，由 _ALLOWED_TD_MODES 派生）
TD_MODE_ALLOWED_TYPES: dict[str, frozenset[str]] = {
    td_mode: frozenset(it for it, modes in _ALLOWED_TD_MODES.items() if td_mode in modes)
    for td_mode in {m for modes in _ALLOWED_TD_MODES.values() for m in modes}
}


class SpecLookupError(ValueError):
    """定位不到合约规格：未知 instId / instType 不在册 / tdMode 与 instType 冲突。

    `reason` 是可直接回给调用方的一句话（要能定位到肇事字段）；`unknown_inst` 为 True 时
    协议层按"未知合约"错误码回，否则按"参数错误"回。
    """

    def __init__(self, reason: str, *, unknown_inst: bool = False) -> None:
        super().__init__(reason)
        self.reason = reason
        self.unknown_inst = unknown_inst


class InstrumentSpecIndex:
    """`(instId, instType)` → 规格 的索引，附委托帧的解析规则。"""

    def __init__(self, instruments) -> None:
        """Args:
            instruments: `InstrumentConfig` 序列（CSV 清单或 mock.yml 覆盖段）。

        Raises:
            ValueError: `instruments` 为空 —— 调用方（私有频道/管理面）必须知道哪些合约在册。
        """
        # (inst_id, inst_type) → spec
        self._by_pair: dict[tuple[str, str], InstrumentConfig] = {}
        # inst_id → 该 instId 的全部规格（按 instType 优先级排序，首个即首选）
        self._by_id: dict[str, list[InstrumentConfig]] = {}
        for spec in instruments:
            self._by_pair[(spec.inst_id, spec.inst_type)] = spec
            self._by_id.setdefault(spec.inst_id, []).append(spec)
        if not self._by_pair:
            raise ValueError("instruments 不能为空：下单/注入必须知道合约的 inst_id 与 instType")
        for specs in self._by_id.values():
            specs.sort(key=lambda spec: _TYPE_PRIORITY.get(spec.inst_type, len(_TYPE_PRIORITY)))

    def resolve(self, inst_id, inst_type: str = "", td_mode: str = "") -> InstrumentConfig:
        """定位委托规格。解析顺序（先按 instId 定候选，tdMode 只做消歧与校验）：

        1. `inst_type` 给定 → 精确取 `(instId, instType)`，不在册即报错；
        2. 该 `instId` **只有一种候选类型** → 直接用它（出厂清单删掉 MARGIN 后 100% 走这条）；
        3. 多种候选 → 用 `td_mode` 消歧（`cash`→SPOT，`cross`/`isolated`→合约类）；
        4. 多种候选且 `td_mode` 无法唯一定位 → 报错（不同 instType 的 `tickSz/lotSz` 可能不同，
           猜错就是按错精度下单，明确失败优于猜测）。

        拿到规格后一律校验 `td_mode` 与 `instType` 是否自洽，见 `_check_td_mode`。

        Args:
            inst_id: 合约 ID；非字符串（含 None）一律视为未知合约。
            inst_type: 合约类型，可空（委托帧常不带）。
            td_mode: 交易模式（`cash`/`cross`/`isolated`），可空。

        Returns:
            命中的规格。**不会返回 None** —— 定位失败一律抛 `SpecLookupError`。

        Raises:
            SpecLookupError: 未知 instId、`(instId, instType)` 不在册、多候选无法消歧，
                或 `td_mode` 与该 `instType` 不匹配。
        """
        if not isinstance(inst_id, str) or not inst_id:
            raise SpecLookupError(f"未知合约: {inst_id!r}（在册: {self._in_register_text()}）",
                                  unknown_inst=True)
        if inst_type:
            spec = self._by_pair.get((inst_id, str(inst_type)))
            if spec is None:
                raise SpecLookupError(f"未知合约: {inst_id}/{inst_type}（在册: {self._in_register_text()}）",
                                      unknown_inst=True)
            self._check_td_mode(spec, td_mode)
            return spec
        candidates = self._by_id.get(inst_id)
        if not candidates:
            raise SpecLookupError(f"未知合约: {inst_id!r}（在册: {self._in_register_text()}）",
                                  unknown_inst=True)
        if len(candidates) == 1:
            self._check_td_mode(candidates[0], td_mode)
            return candidates[0]
        # 同一 instId 有多种类型（如人工补抓 MARGIN 后 BTC-USDT 同时有 SPOT/MARGIN）：按 tdMode 消歧
        allowed = TD_MODE_ALLOWED_TYPES.get(td_mode)
        matched = [spec for spec in candidates if allowed and spec.inst_type in allowed]
        if len(matched) == 1:
            return matched[0]
        types = "/".join(spec.inst_type for spec in candidates)
        if not td_mode:
            raise SpecLookupError(f"合约类型不唯一: {inst_id}（在册 {types}），请显式带 instType 或 tdMode",
                                  unknown_inst=True)
        raise SpecLookupError(f"tdMode={td_mode} 无法定位 {inst_id} 的合约类型（在册 {types}）")

    def inst_ids(self) -> list[str]:
        """在册的全部 `instId`（去重、排序）—— 管理面报错文案与订阅列表用。"""
        return sorted(self._by_id)

    def preferred(self) -> list[InstrumentConfig]:
        """每个 `instId` 的首选规格（按优先级；用于按 instId 单键建映射的调用方）。

        `AccountManager` 的结算币种表就是这种用法：同 instId 取一条即可（SPOT 与 MARGIN 的
        base/quote 相同），取首选（SPOT）可避免持仓帧 `instType` 被标成杠杆。
        """
        return [specs[0] for specs in self._by_id.values()]

    # ── 内部 ────────────────────────────────────────────────────────────────────

    def _in_register_text(self) -> str:
        """在册 instId 列表文案；太长时截断，避免把 1900 个 instId 灌进一条报错。"""
        ids = self.inst_ids()
        head = ", ".join(ids[:10])
        return head if len(ids) <= 10 else f"{head} … 共 {len(ids)} 个"

    @staticmethod
    def _check_td_mode(spec: InstrumentConfig, td_mode: str) -> None:
        """校验 `td_mode` 与规格的 `instType` 自洽；不自洽抛 `SpecLookupError`。

        `td_mode` 为空时不校验（协议层会补默认值）—— 委托帧不带 tdMode 是合法缺省。
        SPOT + cross/isolated 是最容易误用的一种：它表示币币杠杆，而本模拟盘不提供杠杆，
        静默当现货成交会掩盖"以为在下杠杆单、其实下的是现货"。
        """
        if not td_mode:
            return
        allowed = _ALLOWED_TD_MODES.get(spec.inst_type)
        if allowed is None or td_mode in allowed:
            return
        if spec.inst_type == "SPOT":
            raise SpecLookupError(f"tdMode={td_mode} 不能用于现货 {spec.inst_id}："
                                  f"本模拟盘不提供币币杠杆，请用 tdMode=cash")
        raise SpecLookupError(f"tdMode={td_mode} 不能用于 {spec.inst_type} {spec.inst_id}："
                              f"合约请用 tdMode=cross / isolated")
