"""mock_exchange 配置加载测试

覆盖四件事：
1. 随包发布的 `config/mock.yml` + `data/okx_instruments.csv` 能被加载，关键开关就是设计值
   （端口/fill_mode/默认账户/合约清单）；
2. 缺键（含整段缺失、整段为 null）回落到 `mockex/config.py` 内的默认值；
3. 字段形状：端口是 int、`default_sz` 是 float、`instIdCode` 是 int、余额是 ccy → 数量的映射；
4. 合约清单 CSV 的解析与报错：表头/instIdCode/重复键/非 live 行。
"""

from pathlib import Path

import pytest
import yaml

from mockex.config import CSV_COLUMNS, MockConfig, load_config

MOCK_YML = Path(__file__).resolve().parents[1] / "config" / "mock.yml"
CSV_PATH = Path(__file__).resolve().parents[1] / "data" / "okx_instruments.csv"

# 引擎 `OkexClient::GetMarketInfo` 会逐个 GetString/GetInt64 读这 16 个键，少一个就崩
ENGINE_INSTRUMENT_KEYS = [
    "instType", "instId", "baseCcy", "quoteCcy", "settleCcy", "ctVal", "ctMult", "ctValCcy",
    "listTime", "expTime", "lever", "tickSz", "lotSz", "minSz", "state", "instIdCode",
]


def _write(tmp_path: Path, text: str) -> Path:
    """把 YAML 文本落盘，返回路径（缺键场景测试用）。"""
    path = tmp_path / "mock.yml"
    path.write_text(text, encoding="utf-8")
    return path


# ── 1. 随包发布的 mock.yml ──────────────────────────────────────────────────────

def test_shipped_mock_yml_exists():
    assert MOCK_YML.is_file(), f"缺少配置文件: {MOCK_YML}"


def test_shipped_mock_yml_loads():
    assert isinstance(load_config(MOCK_YML), MockConfig)


def test_default_path_points_to_shipped_mock_yml():
    """不给路径时默认读 mock_exchange/config/mock.yml。"""
    assert load_config().server.rest_port == 18080


def test_okx_surface_ports():
    cfg = load_config(MOCK_YML)
    assert (cfg.server.rest_port, cfg.server.ws_port, cfg.server.admin_port) == (18080, 18443, 18081)


def test_core_switches():
    cfg = load_config(MOCK_YML)
    assert cfg.tls.cert_dir == "certs"
    # 出厂配置 2026-10-03 起为 orderbook（撮合模式）：引擎委托进簿、能吃对手挂单
    assert cfg.fill_mode == "orderbook"
    assert cfg.injection.default_sz == 1.0
    assert cfg.quotes.throttle_ms == 0
    assert cfg.feed.source == "none"


def test_core_switch_types():
    """端口要给 aiohttp 当 int 用，节流量与默认量也按数字类型归一。"""
    cfg = load_config(MOCK_YML)
    assert isinstance(cfg.server.rest_port, int)
    assert isinstance(cfg.injection.default_sz, float)
    assert isinstance(cfg.quotes.throttle_ms, int)


def test_default_account_is_usable():
    cfg = load_config(MOCK_YML)
    assert len(cfg.accounts) >= 1
    account = cfg.accounts[0]
    assert account.api_key
    assert account.passphrase
    assert account.balances["USDT"] > 0
    assert account.balances["BTC"] > 0


def test_shipped_csv_instruments_load():
    """出厂清单来自 data/okx_instruments.csv（2026-10-03 起），不再是 mock.yml 内联段。"""
    cfg = load_config(MOCK_YML)
    assert cfg.instruments_csv == Path(__file__).resolve().parents[1] / "data" / "okx_instruments.csv"
    assert cfg.instruments_csv.is_file()
    assert len(cfg.instruments) > 1000, "CSV 应包含 OKX 全量合约（SPOT/SWAP/FUTURES）"
    # 唯一键是 (instId, instType)，不得有重复
    keys = [(i.inst_id, i.inst_type) for i in cfg.instruments]
    assert len(keys) == len(set(keys))
    assert ("BTC-USDT", "SPOT") in keys and ("BTC-USDT-SWAP", "SWAP") in keys


def test_shipped_csv_has_no_margin_and_unique_inst_ids():
    """本系统不做杠杆：出厂清单不含 MARGIN，故每个 instId 恰好一种类型。

    MARGIN 是币币杠杆，复用 SPOT 的 instId（266 对逐字段相同）——这正是 `instType` 必须
    进唯一键的原因（引擎侧键为 `(market, instId, instType)`）。清单里移除 MARGIN 后
    同 instId 不再有歧义，但唯一键的**形状**保持不变。
    """
    cfg = load_config(MOCK_YML)
    assert not [i for i in cfg.instruments if i.inst_type == "MARGIN"]
    assert {i.inst_type for i in cfg.instruments} == {"SPOT", "SWAP", "FUTURES"}
    inst_ids = [i.inst_id for i in cfg.instruments]
    assert len(inst_ids) == len(set(inst_ids)), "移除 MARGIN 后每个 instId 应只有一种类型"


def test_btc_usdt_instrument_sample():
    cfg = load_config(MOCK_YML)
    inst = next(i for i in cfg.instruments if (i.inst_id, i.inst_type) == ("BTC-USDT", "SPOT"))
    assert inst.inst_id_code == 3
    assert isinstance(inst.inst_id_code, int)
    assert inst.tick_sz and inst.lot_sz and inst.min_sz
    # 现货没有合约乘数（OKX 真实应答里 ctVal 为空串）；CT 系列字段由合约类标的覆盖
    assert inst.ct_val == ""


def test_csv_header_matches_engine_contract():
    """data/okx_instruments.csv 的列集与引擎读取的 16 个键一致（列序见 `_load_instruments_csv`）。"""
    header = CSV_PATH.read_text(encoding="utf-8").splitlines()[0].split(",")
    assert header == list(CSV_COLUMNS)
    assert sorted(header) == sorted(ENGINE_INSTRUMENT_KEYS)


# ── 2. 缺键回落默认值 ───────────────────────────────────────────────────────────

def test_empty_file_reads_shipped_csv(tmp_path):
    """空 mock.yml：除合约清单（一律走 CSV）外全部走默认值。"""
    cfg = load_config(_write(tmp_path, ""))
    assert cfg.instruments == load_config().instruments


def test_partial_override_keeps_other_defaults(tmp_path):
    cfg = load_config(_write(tmp_path, "server:\n  rest_port: 19090\n"))
    assert cfg.server.rest_port == 19090
    assert cfg.server.ws_port == 18443
    assert cfg.server.admin_port == 18081
    assert cfg.tls.cert_dir == "certs"
    assert cfg.fill_mode == "immediate"
    assert cfg.injection.default_sz == 1.0
    assert cfg.quotes.throttle_ms == 0
    assert cfg.feed.source == "none"


def test_missing_sections_fall_back_to_defaults(tmp_path):
    """只写一段时，其余整段走默认；合约清单始终来自 CSV（不再是 mock.yml 的列表段）。"""
    cfg = load_config(_write(tmp_path, "fill_mode: orderbook\n"))
    assert cfg.fill_mode == "orderbook"
    assert cfg.accounts == MockConfig().accounts
    assert cfg.instruments == load_config().instruments


def test_empty_or_null_sections_fall_back_to_defaults(tmp_path):
    cfg = load_config(_write(tmp_path, "injection: {}\nquotes:\nfeed:\ntls:\n  cert_dir:\n"))
    assert cfg.injection.default_sz == 1.0
    assert cfg.quotes.throttle_ms == 0
    assert cfg.feed.source == "none"
    assert cfg.tls.cert_dir == "certs"


def test_unknown_keys_are_ignored(tmp_path):
    """后续任务会往 mock.yml 加新键（如 feed.synthetic），老配置文件不能被判错。"""
    cfg = load_config(_write(tmp_path, "unknown_top: 1\nquotes:\n  unknown_nested: 2\n"))
    assert isinstance(cfg, MockConfig)


def test_numeric_scalars_are_coerced(tmp_path):
    cfg = load_config(_write(tmp_path, "server:\n  rest_port: '19090'\ninjection:\n  default_sz: 2\n"))
    assert cfg.server.rest_port == 19090 and isinstance(cfg.server.rest_port, int)
    assert cfg.injection.default_sz == 2.0 and isinstance(cfg.injection.default_sz, float)


# ── 3. 列表段：覆盖与字段名映射 ─────────────────────────────────────────────────

def test_accounts_override(tmp_path):
    cfg = load_config(_write(
        tmp_path,
        "accounts:\n  - api_key: k1\n    passphrase: p1\n    balances:\n      USDT: 5\n",
    ))
    assert [a.api_key for a in cfg.accounts] == ["k1"]
    assert cfg.accounts[0].passphrase == "p1"
    assert cfg.accounts[0].balances == {"USDT": 5}


def test_instruments_override_uses_okx_field_names(tmp_path):
    cfg = load_config(_write(
        tmp_path,
        "instruments:\n  - instId: SYN-USDT\n    instType: SPOT\n    instIdCode: 42\n    tickSz: '0.01'\n",
    ))
    inst = cfg.instruments[0]
    assert (inst.inst_id, inst.inst_type, inst.inst_id_code, inst.tick_sz) == ("SYN-USDT", "SPOT", 42, "0.01")
    assert inst.min_sz == ""  # 未给的键 → InstrumentConfig 的默认值（不是 BTC-USDT 样例值）


def test_missing_file_raises(tmp_path):
    """缺键才回落默认值；配置文件本身不存在属于错误，不能静默用默认值跑起来。"""
    with pytest.raises(FileNotFoundError):
        load_config(tmp_path / "no-such-file.yml")


@pytest.mark.parametrize(
    "text, expected",
    [
        ("accounts:\n  - mock-okx-key\n", r"accounts\[0\]"),
        ("instruments:\n  - instId: BTC-USDT\n  - with a typo\n", r"instruments\[1\]"),
        ("accounts: not-a-list\n", r"accounts"),
    ],
)
def test_non_mapping_list_items_raise(tmp_path, text, expected):
    """列表段里的坏条目要响亮报错。

    静默丢弃会让 `accounts: ["k"]` 退化成内置默认账户、照常跑通冒烟测试 —— 配置写错被伪装成绿。
    """
    with pytest.raises(ValueError, match=expected):
        load_config(_write(tmp_path, text))


# ── 4. 合约清单 CSV ────────────────────────────────────────────────────────────

def _write_csv(tmp_path: Path, rows: list[str], header: str | None = None) -> Path:
    """把合约行写成 CSV，返回路径（表头默认与 `CSV_COLUMNS` 一致）。"""
    path = tmp_path / "instruments.csv"
    text = (header if header is not None else ",".join(CSV_COLUMNS)) + "\n"
    text += "".join(f"{row}\n" for row in rows)
    path.write_text(text, encoding="utf-8")
    return path


def _instrument_row(inst_id: str, inst_type: str, code: str = "42", state: str = "live",
                    tick_sz: str = "0.01") -> str:
    """按 `CSV_COLUMNS` 列序拼一行（其余列留空）。"""
    values = {"instType": inst_type, "instId": inst_id, "instIdCode": code,
              "baseCcy": inst_id.split("-")[0], "quoteCcy": "USDT", "state": state, "tickSz": tick_sz}
    return ",".join(values.get(column, "") for column in CSV_COLUMNS)


def _mock_yml_with_csv(tmp_path: Path, csv_path: Path) -> Path:
    """生成只指向给定 CSV 的 mock.yml。"""
    return _write(tmp_path, f"instruments_csv: {csv_path}\n")


def test_csv_path_override_and_relative_resolution(tmp_path):
    """`instruments_csv` 支持绝对路径与相对路径（相对 mock_exchange 根目录）。"""
    csv_file = _write_csv(tmp_path, [_instrument_row("SYN-USDT", "SPOT")])
    cfg = load_config(_mock_yml_with_csv(tmp_path, csv_file))
    assert [i.inst_id for i in cfg.instruments] == ["SYN-USDT"]
    assert cfg.instruments[0].tick_sz == "0.01"

    relative = load_config(_write(tmp_path, "instruments_csv: data/okx_instruments.csv\n"))
    assert len(relative.instruments) > 1000


def test_csv_keeps_both_inst_types_of_same_inst_id(tmp_path):
    """同 instId 的 SPOT/MARGIN 必须各留一条（唯一键是 instId + instType）。"""
    csv_file = _write_csv(tmp_path, [
        _instrument_row("BTC-USDT", "SPOT", code="3", tick_sz="0.1"),
        _instrument_row("BTC-USDT", "MARGIN", code="3", tick_sz="0.01"),
    ])
    instruments = load_config(_mock_yml_with_csv(tmp_path, csv_file)).instruments
    assert [(i.inst_id, i.inst_type, i.tick_sz) for i in instruments] == [
        ("BTC-USDT", "SPOT", "0.1"), ("BTC-USDT", "MARGIN", "0.01")]


def test_csv_duplicate_key_raises(tmp_path):
    """同一 (instId, instType) 出现两次 = 清单写重，必须报错而不是静默取一条。"""
    csv_file = _write_csv(tmp_path, [
        _instrument_row("BTC-USDT", "SPOT"),
        _instrument_row("BTC-USDT", "SPOT"),
    ])
    with pytest.raises(ValueError, match=r"重复出现"):
        load_config(_mock_yml_with_csv(tmp_path, csv_file))


def test_csv_header_must_match_columns(tmp_path):
    csv_file = _write_csv(tmp_path, [_instrument_row("BTC-USDT", "SPOT")],
                          header="instType,instId,instIdCode")
    with pytest.raises(ValueError, match=r"表头不符"):
        load_config(_mock_yml_with_csv(tmp_path, csv_file))


@pytest.mark.parametrize("code", ["", "0", "abc", "-3"])
def test_csv_inst_id_code_must_be_positive_int(tmp_path, code):
    """`instIdCode` 是引擎下单的查询键（0/缺失会被 OKX 拒 51000）。"""
    csv_file = _write_csv(tmp_path, [_instrument_row("SYN-USDT", "SPOT", code=code)])
    with pytest.raises(ValueError, match=r"instIdCode 非法"):
        load_config(_mock_yml_with_csv(tmp_path, csv_file))


def test_csv_skips_unlisted_rows(tmp_path):
    """非 live 行跳过；全是非 live 时整份清单视为不可用（报错而不是空跑）。"""
    csv_file = _write_csv(tmp_path, [
        _instrument_row("SYN-USDT", "SPOT"),
        _instrument_row("OLD-USDT", "SPOT", state="suspend"),
    ])
    assert [i.inst_id for i in load_config(_mock_yml_with_csv(tmp_path, csv_file)).instruments] == ["SYN-USDT"]

    only_unlisted = _write_csv(tmp_path, [_instrument_row("OLD-USDT", "SPOT", state="suspend")])
    with pytest.raises(ValueError, match=r"没有可用的 live 合约"):
        load_config(_mock_yml_with_csv(tmp_path, only_unlisted))


def test_csv_missing_raises(tmp_path):
    """CSV 是唯一数据源：文件缺失必须报错，不能退化成内置样例。"""
    with pytest.raises(FileNotFoundError):
        load_config(_mock_yml_with_csv(tmp_path, tmp_path / "no-such.csv"))
