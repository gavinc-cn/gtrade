"""`InstrumentSpecIndex` 的规格定位规则测试。

覆盖三件事：

1. **双键唯一性**：`(instId, instType)` 才唯一 —— MARGIN 与 SPOT 复用 instId 时靠 instType 区分；
2. **委托帧的解析顺序**（引擎下单报文不带 `instType`）：instId 唯一即直取，
   多候选才用 `tdMode` 消歧，无法消歧即报错（不猜，见 `resolve` 文档）；
3. **`tdMode` 与 `instType` 的自洽校验**：`tdMode` 对现货表示"现货/币币杠杆"，对合约表示
   保证金模式 —— 曾经把 `isolated` 一律映射成 MARGIN，导致合约单被标错类型（靠单键回落
   才没炸）。现在反过来：SPOT 收 `cash`，SWAP/FUTURES 收 `cross`/`isolated`，越界报错。

用出厂 CSV 做真实数据断言，另用手工构造的索引覆盖"同 instId 多类型"（出厂清单已无 MARGIN）。
"""

from pathlib import Path

import pytest

from mockex.config import InstrumentConfig, load_config
from mockex.core.instrument_index import InstrumentSpecIndex, SpecLookupError

MOCK_YML = Path(__file__).resolve().parents[1] / "config" / "mock.yml"


@pytest.fixture(scope="module")
def shipped() -> InstrumentSpecIndex:
    """出厂清单（1900 条，每 instId 一种类型）建出的索引。"""
    return InstrumentSpecIndex(load_config(MOCK_YML).instruments)


def _spec(inst_id: str, inst_type: str) -> InstrumentConfig:
    """构造一条最小规格（tick/lot 给不同值，便于断言取到的是哪一条）。"""
    return InstrumentConfig(inst_id=inst_id, inst_type=inst_type, inst_id_code=1,
                            tick_sz="0.1", lot_sz="0.01", min_sz="0.01")


# ── 1. 出厂清单：instId 唯一，tdMode 只做校验 ──────────────────────────────────

def test_resolve_without_td_mode_uses_unique_candidate(shipped):
    """委托帧不带 instType/tdMode（引擎的实际形态）→ 按 instId 直取。"""
    spec = shipped.resolve("BTC-USDT")
    assert (spec.inst_id, spec.inst_type) == ("BTC-USDT", "SPOT")


def test_resolve_swap_without_td_mode(shipped):
    spec = shipped.resolve("BTC-USDT-SWAP")
    assert (spec.inst_id, spec.inst_type) == ("BTC-USDT-SWAP", "SWAP")


def test_resolve_exact_pair(shipped):
    """显式给了 instType 就精确取，不再看 tdMode。"""
    spec = shipped.resolve("BTC-USDT", inst_type="SPOT", td_mode="cash")
    assert spec.inst_type == "SPOT"


def test_resolve_rejects_unknown_inst_id(shipped):
    with pytest.raises(SpecLookupError) as exc:
        shipped.resolve("NOPE-USDT")
    assert exc.value.unknown_inst is True
    assert "未知合约" in exc.value.reason


def test_resolve_rejects_inst_type_not_in_register(shipped):
    """MARGIN 已从出厂清单移除 → 显式指定它也算不在册。"""
    with pytest.raises(SpecLookupError) as exc:
        shipped.resolve("BTC-USDT", inst_type="MARGIN")
    assert exc.value.unknown_inst is True


# ── 2. tdMode 与 instType 的自洽校验 ───────────────────────────────────────────

def test_resolve_rejects_margin_td_mode_on_spot(shipped):
    """现货 + cross/isolated 表示币币杠杆 —— 本模拟盘不提供，必须报错而不是当现货成交。"""
    for td_mode in ("cross", "isolated"):
        with pytest.raises(SpecLookupError) as exc:
            shipped.resolve("BTC-USDT", td_mode=td_mode)
        assert exc.value.unknown_inst is False        # 是参数错，不是未知合约
        assert "不提供币币杠杆" in exc.value.reason


def test_resolve_accepts_isolated_on_swap(shipped):
    """合约 + isolated 是保证金模式，合法 —— 这正是旧实现错标成 MARGIN 的那条路径。"""
    spec = shipped.resolve("BTC-USDT-SWAP", td_mode="isolated")
    assert spec.inst_type == "SWAP"
    assert shipped.resolve("BTC-USDT-SWAP", td_mode="cross").inst_type == "SWAP"


def test_resolve_rejects_cash_on_contract(shipped):
    """合约 + cash 不自洽：现货模式不能用于 SWAP/FUTURES。"""
    with pytest.raises(SpecLookupError) as exc:
        shipped.resolve("BTC-USDT-SWAP", td_mode="cash")
    assert exc.value.unknown_inst is False
    assert "cross" in exc.value.reason


def test_resolve_ignores_empty_td_mode(shipped):
    """tdMode 缺失是合法缺省（协议层会补默认值），不校验也不报错。"""
    assert shipped.resolve("BTC-USDT", td_mode="").inst_type == "SPOT"
    assert shipped.resolve("BTC-USDT", inst_type="SPOT", td_mode="").inst_type == "SPOT"


# ── 3. 同 instId 多类型（人工补抓 MARGIN 的场景）：tdMode 消歧 ─────────────────

@pytest.fixture
def with_margin() -> InstrumentSpecIndex:
    """手工造一份"SPOT 与 MARGIN 共用 BTC-USDT"的索引，验证多候选路径。"""
    return InstrumentSpecIndex([
        _spec("BTC-USDT", "SPOT"),
        _spec("BTC-USDT", "MARGIN"),
        _spec("BTC-USDT-SWAP", "SWAP"),
    ])


def test_multi_candidate_disambiguates_by_td_mode(with_margin):
    assert with_margin.resolve("BTC-USDT", td_mode="cash").inst_type == "SPOT"
    assert with_margin.resolve("BTC-USDT", td_mode="cross").inst_type == "MARGIN"


def test_multi_candidate_without_td_mode_is_an_error(with_margin):
    """候选不唯一时不猜：不同 instType 的 tickSz/lotSz 可能不同，猜错就是按错精度下单。"""
    with pytest.raises(SpecLookupError) as exc:
        with_margin.resolve("BTC-USDT")
    assert exc.value.unknown_inst is True
    assert "类型不唯一" in exc.value.reason


def test_multi_candidate_explicit_inst_type_wins(with_margin):
    assert with_margin.resolve("BTC-USDT", inst_type="MARGIN").inst_type == "MARGIN"
    assert with_margin.resolve("BTC-USDT", inst_type="MARGIN", td_mode="cross").inst_type == "MARGIN"


def test_multi_candidate_td_mode_not_matching_any_type():
    """在册候选里没有 tdMode 指向的那一类 → 报错（不猜）。"""
    only_contracts = InstrumentSpecIndex([_spec("X-USDT", "SWAP"), _spec("X-USDT", "FUTURES")])
    with pytest.raises(SpecLookupError) as exc:
        only_contracts.resolve("X-USDT", td_mode="cash")     # cash 只对 SPOT 合法
    assert "无法定位" in exc.value.reason


def test_multi_candidate_td_mode_matching_two_types_is_an_error():
    """tdMode 同时匹配多种候选（如 MARGIN 与 SWAP 都收 cross）→ 仍无法唯一定位，报错。"""
    ambiguous = InstrumentSpecIndex([_spec("X-USDT", "MARGIN"), _spec("X-USDT", "SWAP")])
    with pytest.raises(SpecLookupError) as exc:
        ambiguous.resolve("X-USDT", td_mode="cross")
    assert "无法定位" in exc.value.reason


# ── 4. 其它 ────────────────────────────────────────────────────────────────────

def test_preferred_picks_spot_first(with_margin):
    """`preferred()` 每个 instId 一条：同 instId 时现货优先，避免持仓帧被标成杠杆。"""
    preferred = {(s.inst_id, s.inst_type) for s in with_margin.preferred()}
    assert preferred == {("BTC-USDT", "SPOT"), ("BTC-USDT-SWAP", "SWAP")}


def test_empty_instruments_raises():
    with pytest.raises(ValueError):
        InstrumentSpecIndex([])
