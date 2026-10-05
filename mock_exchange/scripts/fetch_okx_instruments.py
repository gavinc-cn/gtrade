#!/usr/bin/env python3
"""从 OKX 抓取全量合约规格，落成本地 CSV（mock_exchange 的标的清单数据源）。

设计要点（均来自实证，不猜）：

1. **走官方公开端点** `GET /api/v5/public/instruments?instType=...`：这是 OKX 文档里
   公开合约信息的标准路径（无需签名）。引擎自身用的 `/api/v5/account/instruments`
   在官方文档中不存在（见 `doc_ai/spec/mock_exchange/引擎OKX契约.md`），但**两者应答的
   字段集一致**，CSV 列仍按引擎 `OkexClient::GetMarketInfo` 实际读取的 16 个键排列。
2. **默认经 HTTP 代理**：本容器直连 `www.okx.com` 不通（curl 退出码 000），实际线上
   配置也用代理（`config/config.yml` 注释保留 `home.proxy:10808`），故默认走代理。
3. **凭证只用于签名探测**：脚本先用公开端点取数（实测 200）；若公开路径失败，再尝试
   按 `--account` 指定的账户（`/etc/secret.yml`，默认 `okx_account3_dummy`）签名重试，
   并把实际生效的模式记进日志。默认**不加** `x-simulated-trading` 头 —— 该头在生产
   端点会被拒（见 bug_report/20260929_2028），仅 `--simulated` 时追加。
4. **MARGIN 不抓**：币币杠杆不是独立品种 —— 它与 SPOT 共用 `instId` 与 `instIdCode`
   （实测 266 对逐字段相同），唯一区别是 `instType`。本系统不做杠杆交易，保留它会让
   同一个 `instId` 在清单里出现两次；需要时可用 `--inst-type ... MARGIN` 单独补抓。
5. **OPTION 不抓**：官方对单次 `OPTION` 查询有 500 条上限（实测返回 `50015`，提示按
   `uly`/`expTime` 收敛），全量期权上万条且 mock 无对应撮合语义，需要时再单独扩。

CSV 列与 OKX 应答键逐字一致（`instIdCode` 为 int，其余为字符串），列序即
`mock_exchange/config/mock.yml` 原 `instruments[]` 的字段顺序，便于人工核对与 diff。

用法：

    python3 mock_exchange/scripts/fetch_okx_instruments.py                # 默认全量抓取
    python3 mock_exchange/scripts/fetch_okx_instruments.py --inst-type SPOT SWAP
    python3 mock_exchange/scripts/fetch_okx_instruments.py --no-proxy     # 直连（本机通常不通）
    python3 mock_exchange/scripts/fetch_okx_instruments.py -o /tmp/x.csv --simulated
"""

from __future__ import annotations

import argparse
import base64
import csv
import hashlib
import hmac
import json
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

# ── 常量 ──────────────────────────────────────────────────────────────────────

# 引擎 OkexClient::GetMarketInfo 逐个读取的 16 个键（少一个引擎侧解析即出错），
# 列序与 config/mock.yml 原 instruments[] 段一致。
CSV_COLUMNS = [
    "instType", "instId", "instIdCode", "baseCcy", "quoteCcy", "settleCcy",
    "ctVal", "ctMult", "ctValCcy", "listTime", "expTime", "lever",
    "tickSz", "lotSz", "minSz", "state",
]

# 默认抓取的品种类型。两类不抓：
#   - MARGIN：币币杠杆不是独立品种，它复用 SPOT 的 instId（同 instId 同 instIdCode），
#     只多一个 instType。本系统不做杠杆，清单里留着只会与 SPOT 争同一个 instId；
#   - OPTION：官方单次查询有 500 条上限（实测返回 50015，需按 uly/expTime 收敛），
#     见模块文档第 4 条。
DEFAULT_INST_TYPES = ["SPOT", "SWAP", "FUTURES"]

# instIdCode 必须是正整数：引擎下单按它查合约，0/缺失会被 OKX 拒（错误码 51000）。
INST_ID_CODE_RE = re.compile(r"^[1-9]\d*$")

DEFAULT_OUT = Path(__file__).resolve().parents[1] / "data" / "okx_instruments.csv"
DEFAULT_SECRET = Path("/etc/secret.yml")
DEFAULT_ACCOUNT = "okx_account3_dummy"
DEFAULT_PROXY = "http://home.proxy:10808"
BASE_URL = "https://www.okx.com"

# 默认 UA 走 Cloudflare 会被拦（实测 urllib 默认 UA → HTTP 403 `error code: 1010`，
# 换成 curl/浏览器 UA 同为 200），故显式带一个浏览器 UA。
DEFAULT_HEADERS = {
    "User-Agent": ("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                   "(KHTML, like Gecko) Chrome/124.0 Safari/537.36"),
    "Accept": "application/json",
}


class FetchError(RuntimeError):
    """抓取失败（网络/签名/应答结构异常）——调用方据此决定是否降级重试。"""


# ── 凭证 ──────────────────────────────────────────────────────────────────────

def read_account(secret_path: Path, account: str) -> dict:
    """从 `/etc/secret.yml` 取指定账户的凭证（只做正则提取，无需 PyYAML）。

    Args:
        secret_path: secret.yml 路径。
        account: 顶层账户键名，如 `okx_account3_dummy`。

    Returns:
        dict：`{"market", "key", "secret", "passphrase"}`（缺字段即空串）。

    Raises:
        FileNotFoundError: 文件不存在。
        FetchError: 找不到该账户段。
    """
    text = secret_path.read_text(encoding="utf-8")
    block = re.search(rf"^{re.escape(account)}\s*:\s*$(.*?)(?=^\S|\Z)", text, re.M | re.S)
    if block is None:
        raise FetchError(f"{secret_path} 中找不到账户段: {account}")
    body = block.group(1)
    fields = {}
    for name in ("market", "key", "secret", "passphrase"):
        found = re.search(rf"^\s+{name}\s*:\s*[\"']?([^\"'\n#]*?)[\"']?\s*(?:#.*)?$", body, re.M)
        fields[name] = found.group(1).strip() if found else ""
    if not fields["key"] or not fields["secret"]:
        raise FetchError(f"{account} 缺少 key/secret，无法签名")
    return fields


def build_headers(account: dict, request_path: str, simulated: bool) -> dict:
    """按 OKX v5 规范生成签名头：`Base64(HMAC-SHA256(secret, ts + METHOD + path + body))`。

    Args:
        account: `read_account` 的返回值。
        request_path: 含 query 的请求路径（`/api/v5/public/instruments?instType=SPOT`）。
        simulated: 是否追加 `x-simulated-trading: 1`（仅模拟盘环境接受）。

    Returns:
        dict：OK-ACCESS-* 头；`simulated` 为真时含模拟盘标记。
    """
    timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
    message = f"{timestamp}GET{request_path}"
    signature = base64.b64encode(
        hmac.new(account["secret"].encode("utf-8"), message.encode("utf-8"), hashlib.sha256).digest()
    ).decode("utf-8")
    headers = {
        "OK-ACCESS-KEY": account["key"],
        "OK-ACCESS-SIGN": signature,
        "OK-ACCESS-TIMESTAMP": timestamp,
        "OK-ACCESS-PASSPHRASE": account["passphrase"],
        "Content-Type": "application/json",
    }
    if simulated:
        headers["x-simulated-trading"] = "1"
    return headers


# ── 抓取 ──────────────────────────────────────────────────────────────────────

def http_get_json(url: str, headers: dict, proxy: str, timeout: float) -> dict:
    """GET 一个 JSON 应答。

    Args:
        url: 完整 URL。
        headers: 附加请求头（公开端点可为空）。
        proxy: HTTP 代理地址，空串 = 直连。
        timeout: 超时秒数。

    Returns:
        dict：解析后的 JSON。

    Raises:
        FetchError: 网络失败、HTTP 非 2xx 或应答不是 JSON 对象。
    """
    request = urllib.request.Request(url, headers={**DEFAULT_HEADERS, **headers}, method="GET")
    handlers = [urllib.request.ProxyHandler({"http": proxy, "https": proxy})] if proxy \
        else [urllib.request.ProxyHandler({})]
    opener = urllib.request.build_opener(*handlers)
    try:
        with opener.open(request, timeout=timeout) as response:
            body = response.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        raise FetchError(f"HTTP {exc.code}: {exc.read()[:200]!r}") from exc
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        raise FetchError(f"网络失败: {exc}") from exc
    try:
        payload = json.loads(body)
    except json.JSONDecodeError as exc:
        raise FetchError(f"应答不是 JSON: {body[:200]!r}") from exc
    if not isinstance(payload, dict):
        raise FetchError(f"应答顶层不是对象: {type(payload).__name__}")
    return payload


def fetch_inst_type(inst_type: str, proxy: str, account: dict | None, simulated: bool,
                    timeout: float) -> tuple[list[dict], str]:
    """抓取单个 instType 的全部合约规格。

    Args:
        inst_type: OKX 的 instType，如 `SPOT` / `SWAP` / `FUTURES`（MARGIN/OPTION 默认不抓）。
        proxy: HTTP 代理，空串 = 直连。
        account: 凭证（签名降级用），None = 不降级。
        simulated: 签名降级时是否带模拟盘头。
        timeout: 单次请求超时秒数。

    Returns:
        tuple[list[dict], str]：原始应答行 + 实际生效的模式（`public` / `signed`）。

    Raises:
        FetchError: 公开端点与签名降级都失败，或 OKX 返回 `code != 0`。
    """
    request_path = f"/api/v5/public/instruments?instType={inst_type}"
    attempts = [("public", {})]
    if account:
        attempts.append(("signed", build_headers(account, request_path, simulated)))

    errors = []
    for mode, headers in attempts:
        try:
            payload = http_get_json(f"{BASE_URL}{request_path}", headers, proxy, timeout)
        except FetchError as exc:
            errors.append(f"{mode}: {exc}")
            print(f"  [{inst_type}] {mode} 失败：{exc}", file=sys.stderr)
            continue
        if payload.get("code") != "0":
            errors.append(f"{mode}: code={payload.get('code')} msg={payload.get('msg')}")
            print(f"  [{inst_type}] {mode} 被拒：code={payload.get('code')} msg={payload.get('msg')}",
                  file=sys.stderr)
            continue
        data = payload.get("data")
        if not isinstance(data, list):
            errors.append(f"{mode}: data 不是列表")
            continue
        return data, mode
    raise FetchError(f"{inst_type} 抓取失败：{' | '.join(errors)}")


# ── CSV 落盘 ──────────────────────────────────────────────────────────────────

def normalize_row(row: dict, inst_type: str) -> dict:
    """把一条 OKX 应答行压成 CSV 行（16 列，缺键填空串，`instIdCode` 转 int 字符串）。

    Args:
        row: OKX 应答里的单个合约对象。
        inst_type: 请求的品种类型（应答缺 `instType` 时兜底）。

    Returns:
        dict：键为 `CSV_COLUMNS` 的行。

    Raises:
        FetchError: `instId` 缺失，或 `instIdCode` 不是正整数。
    """
    inst_id = str(row.get("instId") or "").strip()
    if not inst_id:
        raise FetchError(f"应答行缺少 instId: {row!r}")
    code = row.get("instIdCode")
    code_text = "" if code is None else str(code).strip()
    if not INST_ID_CODE_RE.match(code_text):
        raise FetchError(f"{inst_id} 的 instIdCode 非法: {code!r}（引擎下单按它查合约）")
    values = {}
    for column in CSV_COLUMNS:
        if column == "instId":
            values[column] = inst_id
        elif column == "instIdCode":
            values[column] = code_text
        elif column == "instType":
            values[column] = str(row.get("instType") or inst_type)
        else:
            raw = row.get(column)
            values[column] = "" if raw is None else str(raw)
    return values


def write_csv(rows: list[dict], out_path: Path) -> None:
    """按 `CSV_COLUMNS` 列序写 UTF-8 / `\\n` 的 CSV（不写 BOM，便于 diff 与脚本读取）。

    Args:
        rows: `normalize_row` 产出的行序列（调用方已排好序）。
        out_path: 输出路径；父目录不存在时自动创建。
    """
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8", newline="") as fp:
        writer = csv.DictWriter(fp, fieldnames=CSV_COLUMNS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def validate_rows(rows: list[dict]) -> list[str]:
    """校验最终行集，返回统计/告警文本列表（不阻断写盘，供人工确认）。

    校验项：同一 `instId` 被多种 `instType` 复用时，各自的 `instIdCode` **必须一致**
    —— 不一致会让引擎按 instId 查到错合约。（MARGIN 复用 SPOT 的 instId 属 OKX 正常
    现象，即触发本项；现已默认不抓 MARGIN，该校验保留以防人工补抓时踩坑。）
    另统计各 `instType` 行数。

    Args:
        rows: 待写盘的行序列。

    Returns:
        list[str]：统计/告警文本（空列表 = 无异常）。

    Raises:
        FetchError: 同一 `instId` 出现不同的 `instIdCode`（真冲突，必须人工处理）。
    """
    notes = []
    codes_by_id: dict[str, set[str]] = {}
    types_by_id: dict[str, set[str]] = {}
    for row in rows:
        codes_by_id.setdefault(row["instId"], set()).add(row["instIdCode"])
        types_by_id.setdefault(row["instId"], set()).add(row["instType"])
    conflicts = {inst_id: codes for inst_id, codes in codes_by_id.items() if len(codes) > 1}
    if conflicts:
        sample = list(conflicts.items())[:3]
        raise FetchError(f"同一 instId 出现多个 instIdCode（引擎会查错合约）: {sample}")
    reused = sum(1 for types in types_by_id.values() if len(types) > 1)
    if reused:
        notes.append(f"{reused} 个 instId 被多种 instType 复用（instIdCode 一致，引擎侧无歧义）")
    return notes


# ── 入口 ──────────────────────────────────────────────────────────────────────

def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    """解析命令行参数。"""
    parser = argparse.ArgumentParser(description="抓取 OKX 全量合约规格并落成 CSV")
    parser.add_argument("--inst-type", nargs="+", default=DEFAULT_INST_TYPES,
                        help=f"要抓的品种类型（默认 {' '.join(DEFAULT_INST_TYPES)}）")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT,
                        help=f"输出 CSV 路径（默认 {DEFAULT_OUT}）")
    parser.add_argument("--account", default=DEFAULT_ACCOUNT,
                        help=f"签名降级用账户（默认 {DEFAULT_ACCOUNT}）")
    parser.add_argument("--secret-file", type=Path, default=DEFAULT_SECRET,
                        help=f"账户配置文件（默认 {DEFAULT_SECRET}）")
    parser.add_argument("--proxy", default=DEFAULT_PROXY,
                        help=f"HTTP 代理（默认 {DEFAULT_PROXY}，本机直连 OKX 不通）")
    parser.add_argument("--no-proxy", action="store_true", help="直连（不加代理）")
    parser.add_argument("--simulated", action="store_true",
                        help="签名降级时追加 x-simulated-trading: 1（仅模拟盘接受）")
    parser.add_argument("--timeout", type=float, default=30.0, help="单次请求超时秒数（默认 30）")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    """抓取 → 归一 → 写 CSV → 打印统计。返回进程退出码。"""
    args = parse_args(argv)
    proxy = "" if args.no_proxy else args.proxy
    account = None
    if args.secret_file.is_file():
        try:
            account = read_account(args.secret_file, args.account)
        except FetchError as exc:
            print(f"警告：{exc}，仅用公开端点", file=sys.stderr)
    else:
        print(f"警告：{args.secret_file} 不存在，仅用公开端点", file=sys.stderr)

    rows: list[dict] = []
    modes: set[str] = set()
    for inst_type in args.inst_type:
        raw_rows, mode = fetch_inst_type(inst_type, proxy, account, args.simulated, args.timeout)
        modes.add(mode)
        live = 0
        for raw in raw_rows:
            row = normalize_row(raw, inst_type)
            if row["state"] != "live":
                continue                      # 非 live 合约（如预上线）不进 mock 清单
            rows.append(row)
            live += 1
        print(f"{inst_type}: {len(raw_rows)} 条 → live {live} 条（{mode}）")

    if not rows:
        print("抓取结果为空，未写文件", file=sys.stderr)
        return 2

    # instType 按默认顺序分组、组内按 instId 排序：CSV 稳定可 diff
    order = {name: index for index, name in enumerate(DEFAULT_INST_TYPES)}
    rows.sort(key=lambda row: (order.get(row["instType"], len(order)), row["instId"]))

    notes = validate_rows(rows)
    write_csv(rows, args.out)
    by_type: dict[str, int] = {}
    for row in rows:
        by_type[row["instType"]] = by_type.get(row["instType"], 0) + 1
    print(f"已写出 {args.out}：{len(rows)} 行，模式={','.join(sorted(modes))}")
    print(f"分类统计: {by_type}")
    for note in notes:
        print(f"提示: {note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
