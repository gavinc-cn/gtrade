"""
mock_exchange 配置加载：`config/mock.yml` → dataclass

容错原则：**文件里缺任何键（含整段缺失 / 整段为 null）都回落到本模块的默认值**，
默认值即设计文档《模拟交易所》「配置项」表的默认列；只有配置文件本身不存在才报错。

合约清单（2026-10-03 起）不再写在 mock.yml 里，而是读 `data/okx_instruments.csv`
（`.csv` 便于人工查看/diff；内容由 `scripts/fetch_okx_instruments.py` 从 OKX 官方
`GET /api/v5/public/instruments` 抓取）。CSV 列沿用 OKX 字段名（instId / instIdCode /
tickSz …），因为它就是 `GET /api/v5/account/instruments` 的应答内容，同名可少一层
人工翻译 —— 加载时按 camelCase 边界自动转 snake_case。mock.yml 仍可写 `instruments[]`
整段覆盖（单测与临时场景用），写了即以它为准、不再读 CSV。
"""

import csv
import re
from dataclasses import dataclass, field, fields
from pathlib import Path
from typing import Any

import yaml

DEFAULT_CONFIG_PATH = Path(__file__).resolve().parents[1] / "config" / "mock.yml"

# 合约清单 CSV 默认路径（相对 config/ 目录，与 mock.yml 的 `instruments_csv` 同基准）
DEFAULT_INSTRUMENTS_CSV = Path(__file__).resolve().parents[1] / "data" / "okx_instruments.csv"

# 引擎 OkexClient::GetMarketInfo 逐个 GetString/GetInt64 读这 16 个键；CSV 表头必须逐字一致。
# 同一份清单同时喂 OkxRest（instruments 应答）与 AccountManager（结算要 base/quote 币种），
# 所以列缺一不可 —— 少一列直接报错，不做静默补齐。
CSV_COLUMNS = [
    "instType", "instId", "instIdCode", "baseCcy", "quoteCcy", "settleCcy",
    "ctVal", "ctMult", "ctValCcy", "listTime", "expTime", "lever",
    "tickSz", "lotSz", "minSz", "state",
]

# instIdCode 必须是正整数：引擎下单按它查合约，0/缺失会被 OKX 拒（51000）
_INST_ID_CODE = re.compile(r"^[1-9]\d*$")

# immediate：立即全量成交（见设计文档「下单与成交语义」）；orderbook：进订单簿严格撮合（M2）
DEFAULT_FILL_MODE = "immediate"

_CAMEL_BOUNDARY = re.compile(r"(?<!^)(?=[A-Z])")


@dataclass
class ServerConfig:
    """OKX 协议面与管理面的监听端口（管理面由运行层强制只绑 127.0.0.1）。"""

    rest_port: int = 18080
    ws_port: int = 18443
    admin_port: int = 18081


@dataclass
class TlsConfig:
    """WS 必须走 `wss://`（引擎硬编码 websocketpp asio_tls_client）；证书缺失时由运行层生成。"""

    cert_dir: str = "certs"


@dataclass
class InjectionConfig:
    """`/admin/depth` 注入盘口时的默认参数。"""

    default_sz: float = 1.0


@dataclass
class QuotesConfig:
    """盘口推送节流：0 表示每次变化都推。"""

    throttle_ms: int = 0


@dataclass
class FeedConfig:
    """行情源：`none`（纯订单驱动）/ `synthetic`（合成行情，后续阶段）。"""

    source: str = "none"


@dataclass
class AccountConfig:
    """一个 mock 账户：OKX 凭据 + 初始余额（ccy → 数量）。

    凭据只用于内存匹配（M1 默认宽容放行），不写日志、不落盘。
    """

    api_key: str = ""
    secret: str = ""
    passphrase: str = ""
    balances: dict[str, float] = field(default_factory=dict)


@dataclass
class InstrumentConfig:
    """一个合约规格，字段与 OKX `account/instruments` 应答一一对应（数量/价格都是字符串）。"""

    inst_id: str = ""
    inst_type: str = "SPOT"
    inst_id_code: int = 0
    base_ccy: str = ""
    quote_ccy: str = ""
    settle_ccy: str = ""
    ct_val: str = "1"
    ct_mult: str = "1"
    ct_val_ccy: str = ""
    list_time: str = ""
    exp_time: str = ""
    lever: str = ""
    tick_sz: str = ""
    lot_sz: str = ""
    min_sz: str = ""
    state: str = "live"


def default_accounts() -> list[AccountConfig]:
    """默认账户：mock.yml 缺 `accounts` 段时使用（凭据为占位值，不是真实密钥）。"""
    return [AccountConfig(
        api_key="mock-okx-key",
        secret="mock-okx-secret",
        passphrase="mock-okx-pass",
        balances={"USDT": 1_000_000.0, "BTC": 100.0},
    )]


def default_instruments() -> list[InstrumentConfig]:
    """默认合约：BTC-USDT 现货；`instIdCode=3` 是 OKX 线上真实整数（引擎下单必填）。"""
    return [InstrumentConfig(
        inst_id="BTC-USDT",
        inst_type="SPOT",
        inst_id_code=3,
        base_ccy="BTC",
        quote_ccy="USDT",
        settle_ccy="USDT",
        ct_val="1",
        ct_mult="1",
        ct_val_ccy="BTC",
        list_time="0",  # 占位：引擎只把它打进日志，不用真实上市时间
        exp_time="",
        lever="",
        tick_sz="0.1",
        lot_sz="0.00000001",
        min_sz="0.00001",
        state="live",
    )]


@dataclass
class MockConfig:
    """mock_exchange 全量配置：mock.yml 的镜像 + 各段默认值。

    `instruments` 由 `load_config` 从 CSV（或 mock.yml 的覆盖段）填入，不是字段默认值 ——
    这里是 CSV 找不到时的最后兜底（BTC-USDT 一条），保证 dataclass 自身可用。
    """

    server: ServerConfig = field(default_factory=ServerConfig)
    tls: TlsConfig = field(default_factory=TlsConfig)
    fill_mode: str = DEFAULT_FILL_MODE
    injection: InjectionConfig = field(default_factory=InjectionConfig)
    quotes: QuotesConfig = field(default_factory=QuotesConfig)
    feed: FeedConfig = field(default_factory=FeedConfig)
    accounts: list[AccountConfig] = field(default_factory=default_accounts)
    instruments: list[InstrumentConfig] = field(default_factory=default_instruments)
    instruments_csv: Path = field(default_factory=lambda: DEFAULT_INSTRUMENTS_CSV)


def _snake(name: str) -> str:
    """camelCase → snake_case（`instIdCode` → `inst_id_code`）。"""
    return _CAMEL_BOUNDARY.sub("_", name).lower()


def _coerce(declared: Any, value: Any) -> Any:
    """按字段声明的标量类型归一（YAML 里 `rest_port: "18080"` 这种字符串也认）。"""
    if isinstance(value, bool):
        return value
    if declared is int:
        return int(value)
    if declared is float:
        return float(value)
    if declared is str:
        return str(value)
    return value


def _build(cls: type, raw: Any) -> Any:
    """用 raw 里的同名键覆盖 dataclass 的默认值。

    缺键保留默认、未知键忽略、null 视同缺键；raw 不是映射时整段用默认值。
    """
    if not isinstance(raw, dict):
        return cls()
    known = {f.name: f for f in fields(cls)}
    kwargs = {}
    for key, value in raw.items():
        spec = known.get(_snake(str(key)))
        if spec is None or value is None:
            continue
        kwargs[spec.name] = _coerce(spec.type, value)
    return cls(**kwargs)


def _text(raw: dict, key: str, default: str) -> str:
    """取顶层字符串标量；缺失或 null 回落默认值。"""
    value = raw.get(key)
    return default if value is None else str(value)


def _records(raw: dict, key: str, cls: type, fallback) -> list:
    """取列表段：非空列表才覆盖，缺失 / null / 空列表回落默认列表。

    `key` 写了但写坏（不是列表、或列表项不是映射）一律抛 `ValueError` ——
    静默丢弃坏条目会让 `accounts: ["k"]` 退化成内置默认账户、照常跑通冒烟测试，
    把配置写错伪装成绿；只有"没写"才允许静默用默认值。
    """
    items = raw.get(key)
    if items is None:
        return fallback()
    if not isinstance(items, list):
        raise ValueError(f"{key} 必须是列表，实际是 {type(items).__name__}")
    records = []
    for index, item in enumerate(items):
        if not isinstance(item, dict):
            raise ValueError(f"{key}[{index}] 必须是映射，实际是 {type(item).__name__}")
        records.append(_build(cls, item))
    return records or fallback()


def _load_instruments_csv(path: Path, *, skip_unlisted: bool = True) -> list[InstrumentConfig]:
    """读合约清单 CSV（`scripts/fetch_okx_instruments.py` 的产物）。

    唯一键是 `(instId, instType)`：`instType` 是标的唯一性的一部分 —— 币币杠杆（MARGIN）
    复用 SPOT 的 `instId`（如 BTC-USDT 同时存在于两类），只有 instType 能区分，所以双键
    必须保留。下游 `OkxRest`（按 instType 过滤应答）、`AccountManager`（结算币种）、
    `WsPrivate`（下单精度）均按此双键取值。

    出厂 CSV 已不含 MARGIN（本系统不做杠杆，见
    `doc_ai/plan/202610/20261003_1750_标的类型纳入联合主键并移除MARGIN.md`），故当前
    每个 instId 只有一种类型；若人工补抓 MARGIN 回来，本函数的重复键校验与索引的双键
    查询仍然成立。

    Args:
        path: CSV 路径。
        skip_unlisted: 是否跳过 `state != live` 的行（默认跳过；mock 只认可交易合约，
            同时容忍人工编辑 CSV 时留下的非 live 行）。

    Returns:
        list[InstrumentConfig]：按 CSV 行序构造的合约规格（同 instId 的不同 instType 各一条）。

    Raises:
        FileNotFoundError: CSV 不存在（数据源缺失不能静默退化成内置样例）。
        ValueError: 表头与 `CSV_COLUMNS` 不一致、`instIdCode` 非正整数、文件无 live 行，
            或同一 `(instId, instType)` 重复出现（清单里同一标的同一类型只能有一条）。
    """
    with path.open("r", encoding="utf-8", newline="") as fp:
        reader = csv.DictReader(fp)
        header = reader.fieldnames or []
        if header != CSV_COLUMNS:
            raise ValueError(f"{path} 表头不符：期望 {CSV_COLUMNS}，实际 {header}")
        instruments: list[InstrumentConfig] = []
        seen: dict[tuple[str, str], str] = {}
        unlisted = 0
        for line_no, row in enumerate(reader, start=2):
            inst_id = (row.get("instId") or "").strip()
            if not inst_id:
                raise ValueError(f"{path}:{line_no} 缺少 instId")
            if skip_unlisted and (row.get("state") or "").strip() != "live":
                unlisted += 1
                continue
            code_text = (row.get("instIdCode") or "").strip()
            if not _INST_ID_CODE.match(code_text):
                raise ValueError(f"{path}:{line_no} {inst_id} 的 instIdCode 非法: {code_text!r}")
            key = (inst_id, (row.get("instType") or "").strip())
            if key in seen:
                raise ValueError(f"{path}:{line_no} {key} 重复出现（同一标的同一类型只能有一条）")
            seen[key] = code_text
            # 列名 camelCase → 字段 snake_case，复用 YAML 路径同一套归一逻辑
            kwargs = {_snake(name): (_coerce(int, row[name]) if name == "instIdCode" else str(row[name] or ""))
                      for name in CSV_COLUMNS}
            instruments.append(InstrumentConfig(**kwargs))
    if not instruments:
        raise ValueError(f"{path} 没有可用的 live 合约（跳过 {unlisted} 条非 live）")
    return instruments


def _read_instruments(raw: dict, base_dir: Path) -> tuple[list[InstrumentConfig], Path]:
    """取合约清单：mock.yml 的 `instruments[]` 覆盖优先，否则读 `instruments_csv`。

    Args:
        raw: mock.yml 解析出的顶层映射。
        base_dir: CSV 相对路径的基准目录（`load_config` 传入 mock_exchange 根目录，
            与 `cert_dir` 等运行层路径同基准：`run.py` 从项目根启动）。

    Returns:
        tuple[list[InstrumentConfig], Path]：合约清单（至少一条）+ CSV 实际路径
        （走 `instruments[]` 覆盖时仍回填配置里的 CSV 路径，仅为可观测性）。

    Raises:
        FileNotFoundError: CSV 路径不存在（mock.yml 未写 `instruments[]` 且 CSV 缺失）。
        ValueError: CSV 内容或 mock.yml 的 `instruments[]` 段写坏。
    """
    csv_path = _text(raw, "instruments_csv", str(DEFAULT_INSTRUMENTS_CSV))
    csv_file = Path(csv_path)
    if not csv_file.is_absolute():
        csv_file = base_dir / csv_file
    if raw.get("instruments") is not None:
        return _records(raw, "instruments", InstrumentConfig, default_instruments), csv_file
    return _load_instruments_csv(csv_file), csv_file


def load_config(path: str | Path = DEFAULT_CONFIG_PATH) -> MockConfig:
    """读取 mock.yml 并补齐默认值。

    Args:
        path: 配置文件路径，默认 `mock_exchange/config/mock.yml`。

    Returns:
        MockConfig：各段已实例化；`accounts` 至少有一条默认项，`instruments` 来自
        `instruments_csv`（默认 `data/okx_instruments.csv`，实测 2000+ 条）或 mock.yml 的
        `instruments[]` 覆盖段。

    Raises:
        FileNotFoundError: 配置文件不存在，或未写 `instruments[]` 且 CSV 缺失。
        ValueError: 配置结构写坏（顶层不是映射；`accounts` / `instruments` 不是列表、
            列表项不是映射），或 CSV 表头/字段非法。
        yaml.YAMLError: YAML 语法错误。
    """
    config_path = Path(path)
    with config_path.open("r", encoding="utf-8") as fp:
        raw = yaml.safe_load(fp)
    if raw is None:  # 空文件
        raw = {}
    if not isinstance(raw, dict):
        raise ValueError(f"配置文件顶层必须是映射: {config_path}")
    instruments, csv_file = _read_instruments(raw, Path(__file__).resolve().parents[1])
    return MockConfig(
        server=_build(ServerConfig, raw.get("server")),
        tls=_build(TlsConfig, raw.get("tls")),
        fill_mode=_text(raw, "fill_mode", DEFAULT_FILL_MODE),
        injection=_build(InjectionConfig, raw.get("injection")),
        quotes=_build(QuotesConfig, raw.get("quotes")),
        feed=_build(FeedConfig, raw.get("feed")),
        accounts=_records(raw, "accounts", AccountConfig, default_accounts),
        instruments=instruments,
        instruments_csv=csv_file,
    )
