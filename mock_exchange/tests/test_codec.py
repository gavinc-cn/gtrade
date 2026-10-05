"""mock_exchange OKX 报文编码测试（TDD：先红后绿）

验收核心不是"字段齐了"，而是**线上类型**：引擎 sonic_json 解析没有 numbers-as-strings
开关，凡它用 `GetString()` 读的字段必须是 JSON string、`instIdCode` 走 `GetInt64()`
必须是 JSON number —— 类型不符在 Debug 构建下直接 `abort()`。故所有断言都先过一层
`json.dumps → json.loads`（下文的 `wire()`），验"真正上线的报文"；Python dict 里是 str、
序列化后变 number 的坑藏不住。字段清单逐字对照 `doc_ai/spec/mock_exchange/引擎OKX契约.md`。
"""

import json
import time

import pytest

from mockex.config import InstrumentConfig, load_config
from mockex.core.models import (
    SIDE_BUY,
    SIDE_SELL,
    STATE_CANCELED,
    STATE_FILLED,
    Fill,
    Order,
)
from mockex.exchanges.okx import codec

# 引擎 OkexClient::GetMarketInfo 逐个读这 16 个键（顺序即源码里的读取顺序）
ENGINE_INSTRUMENT_KEYS = [
    "instType", "instId", "baseCcy", "quoteCcy", "settleCcy", "ctVal", "ctMult", "ctValCcy",
    "listTime", "expTime", "lever", "tickSz", "lotSz", "minSz", "state", "instIdCode",
]

# 引擎 OkxTrade::OnPlaceOrderConfirm 按 GetString() 读这些键；任何一个变成
# JSON number 都会被 catch 成 kJsonParseError（策略当异常单处理）
ENGINE_ORDER_STR_KEYS = [
    "clOrdId", "ordId", "accFillSz", "state", "posSide", "avgPx", "cTime", "uTime", "fillTime",
    "fillPx", "fillSz", "code", "msg", "instType", "instId", "side", "tdMode", "ordType", "px", "sz",
]


def wire(payload):
    """报文过一遍真实 JSON 往返，返回引擎实际会解析到的结构。"""
    return json.loads(json.dumps(payload))


def make_order(**overrides):
    """一张已成交的 BTC-USDT 买单 —— 默认值即 immediate 模式下的典型形态。"""
    fields = dict(
        cl_ord_id="E10001",
        ord_id="1",
        inst_id="BTC-USDT",
        side=SIDE_BUY,
        px=60000.1,
        sz=0.5,
        acc_fill_sz=0.5,
        filled_amount=30000.05,  # = 60000.1 × 0.5，累计成交额
        state=STATE_FILLED,
        owner="mock-okx-key",
        c_time=1_700_000_000_000,
        u_time=1_700_000_000_123,
    )
    fields.update(overrides)
    return Order(**fields)


def make_fill(**overrides):
    fields = dict(
        ord_id="1",
        inst_id="BTC-USDT",
        px=60000.1,
        sz=0.5,
        ts=1_700_000_000_123,
        taker_owner="mock-okx-key",
        maker_owner="mock_mm",
    )
    fields.update(overrides)
    return Fill(**fields)


# ── 领域模型：默认值与协议透传字段 ────────────────────────────────────────────

def test_order_defaults():
    order = Order("E1", "1", "BTC-USDT", SIDE_BUY, 60000.0, 0.1)
    assert (order.acc_fill_sz, order.filled_amount) == (0.0, 0.0)
    assert (order.state, order.owner, order.c_time, order.u_time) == ("live", "", 0, 0)
    # 引擎 orders 帧每次都会读 posSide/tdMode/ordType；SPOT cash 限价单的默认值
    assert (order.inst_type, order.td_mode, order.ord_type, order.pos_side) == ("SPOT", "cash", "limit", "net")


def test_fill_carries_side_for_account_settlement():
    """T4 的 `apply_fill(fill)` 只吃 Fill —— 没有 side 就判不出资金增减方向。"""
    fill = make_fill(side=SIDE_SELL)
    assert (fill.ord_id, fill.taker_owner, fill.maker_owner, fill.side) == ("1", "mock-okx-key", "mock_mm", SIDE_SELL)
    assert Fill("1", "BTC-USDT", 1.0, 1.0, 2, "a", "b").side == ""


# ── to_str_ms ────────────────────────────────────────────────────────────────

def test_to_str_ms_is_millisecond_string():
    assert codec.to_str_ms(1_700_000_000_123) == "1700000000123"
    assert isinstance(codec.to_str_ms(1_700_000_000_123), str)
    # `time.time() * 1000` 是 float，直接 str() 会吐出 "1700000000123.456"
    assert codec.to_str_ms(1_700_000_000_123.456) == "1700000000123"


# ── bbo-tbt 推送帧 ───────────────────────────────────────────────────────────

def test_bbo_frame_shape_and_wire_types():
    """契约硬约束：`ts` 数字会命中引擎 `GetString()` 断言，价量必须是字符串。"""
    frame = wire(codec.bbo_frame("BTC-USDT", bid=(60000.1, 0.5), ask=(60000.2, 1.25),
                                 ts_ms=1_700_000_000_123, seq=7))
    assert frame["arg"] == {"channel": "bbo-tbt", "instId": "BTC-USDT"}
    assert len(frame["data"]) == 1
    item = frame["data"][0]
    assert item["ts"] == "1700000000123" and isinstance(item["ts"], str)
    assert item["seqId"] == 7 and isinstance(item["seqId"], int)
    assert item["asks"][0] == ["60000.2", "1.25"]
    assert item["bids"][0] == ["60000.1", "0.5"]
    assert all(isinstance(x, str) for x in item["asks"][0] + item["bids"][0])
    # 只有单边挂单时另一侧给空数组（引擎按 `asks.Empty()` 记 0 档）
    one_side = wire(codec.bbo_frame("BTC-USDT", bid=(60000.1, 0.5), ask=None, ts_ms=1, seq=1))["data"][0]
    assert one_side["asks"] == [] and one_side["bids"] == [["60000.1", "0.5"]]


def test_bbo_numbers_have_no_scientific_notation():
    """`1e-08` 这种科学计数法不是 OKX 的写法，落盘给引擎也会失真。"""
    item = wire(codec.bbo_frame("BTC-USDT", bid=(60000.0, 1e-08), ask=(0.00001, 2), ts_ms=1, seq=1))["data"][0]
    assert item["bids"][0] == ["60000", "0.00000001"]
    assert item["asks"][0] == ["0.00001", "2"]


# ── 下单 / 撤单回执 ──────────────────────────────────────────────────────────

def test_order_ack_ok():
    ack = wire(codec.order_ack("order", 10001, "E10001", "1", ts_ms=1_700_000_000_123))
    assert (ack["op"], ack["id"], ack["code"], ack["msg"]) == ("order", 10001, "0", "")
    assert isinstance(ack["code"], str)
    data = ack["data"][0]
    assert (data["clOrdId"], data["ordId"], data["sCode"], data["sMsg"]) == ("E10001", "1", "0", "")
    assert isinstance(data["sCode"], str)
    assert data["ts"] == "1700000000123" and isinstance(data["ts"], str)
    # 撤单回执同构，只有 op 不同
    cancel = wire(codec.order_ack("cancel-order", 10001, "E10001", "1"))
    assert (cancel["op"], cancel["code"], cancel["data"][0]["sCode"]) == ("cancel-order", "0", "0")
    # 不传 ts 时用当前毫秒 —— 引擎把它当委托确认时间，0 会让策略侧时间倒挂
    before = int(time.time() * 1000)
    now = int(codec.order_ack("order", 1, "E1", "1")["data"][0]["ts"])
    assert before <= now <= int(time.time() * 1000) + 1000


def test_order_ack_failure_keeps_string_s_code():
    """sCode 必须仍是字符串：引擎拿它和 "0" 字符串比较，数字 51008 会走错分支。"""
    ack = wire(codec.order_ack("order", 1, "E1", "", s_code="51008", s_msg="Parameter instIdCode error"))
    assert ack["data"][0]["sCode"] == "51008" and isinstance(ack["data"][0]["sCode"], str)
    assert ack["data"][0]["sMsg"] == "Parameter instIdCode error"
    assert ack["code"] == "1"  # OKX：单笔失败时顶层 code 为 "1"
    # 调用方传 int 0 也不能产坏包（编码边界强制字符串）
    assert wire(codec.order_ack("order", 1, "E1", "1", s_code=0))["data"][0]["sCode"] == "0"


# ── orders 帧的数据行 ────────────────────────────────────────────────────────

def test_order_row_string_fields_seen_by_engine():
    row = wire(codec.order_row(make_order(), [make_fill()]))
    for key in ENGINE_ORDER_STR_KEYS:
        assert isinstance(row[key], str), f"{key} 必须是 JSON string，实际 {type(row[key]).__name__}"


def test_order_row_values():
    row = wire(codec.order_row(make_order(), [make_fill()]))
    assert row["instId"] == "BTC-USDT" and row["instType"] == "SPOT"
    assert row["px"] == "60000.1" and row["sz"] == "0.5" and row["accFillSz"] == "0.5"
    assert row["state"] == STATE_FILLED and row["posSide"] == "net"
    assert row["cTime"] == "1700000000000" and row["uTime"] == "1700000000123"
    # instIdCode 只在 instruments 应答里（且必须是 number），委托行里没有它
    assert "instIdCode" not in row


def test_order_row_derives_fill_fields_from_fills():
    """avgPx 是整单累计均价；fillPx/fillSz/fillTime 是最后一批的最后一笔成交。"""
    fills = [make_fill(px=60000.5, sz=0.5, ts=1_700_000_000_100),
             make_fill(px=60001.5, sz=0.5, ts=1_700_000_000_200)]
    row = wire(codec.order_row(make_order(sz=1.0, acc_fill_sz=1.0, filled_amount=60001.0), fills))
    assert row["avgPx"] == "60001"
    assert (row["fillPx"], row["fillSz"], row["fillTime"]) == ("60001.5", "0.5", "1700000000200")


def test_order_row_avg_px_is_cumulative_not_batch():
    """M2 分次推送部分成交的回归护栏：增量 fills 不得污染 avgPx（整单累计口径）。"""
    order = make_order(sz=1.0, acc_fill_sz=1.0, filled_amount=60001.0)
    row = wire(codec.order_row(order, [make_fill(px=70000.0, sz=0.5)]))
    assert row["avgPx"] == "60001"    # 整单累计，与本批成交价无关
    assert row["fillPx"] == "70000"   # 本批最后一笔（两者语义不同，不能混用）


def test_order_row_without_fills():
    """未成交/仅挂单：avgPx 是 "0"，fillPx/fillSz/fillTime 按 OKX 惯例给空串。"""
    row = wire(codec.order_row(make_order(state="live", acc_fill_sz=0.0, filled_amount=0.0, u_time=0), []))
    assert row["avgPx"] == "0"
    assert (row["fillPx"], row["fillSz"], row["fillTime"]) == ("", "", "")
    assert row["accFillSz"] == "0" and row["uTime"] == "0"  # 时间也得是字符串 "0"


def test_order_row_avg_px_guard_raises_when_amount_missing():
    """有累计成交量却没有累计成交额 = 调用方漏维护 → 响亮报错，不静默算错均价。"""
    with pytest.raises(ValueError, match="filled_amount"):
        codec.order_row(make_order(acc_fill_sz=0.5, filled_amount=0.0), [make_fill()])


def test_order_row_canceled_state():
    row = wire(codec.order_row(
        make_order(state=STATE_CANCELED, acc_fill_sz=0.0, filled_amount=0.0, side=SIDE_SELL), []))
    assert row["state"] == "canceled" and row["side"] == "sell"


# ── orders 推送帧 ────────────────────────────────────────────────────────────

def test_order_update_is_orders_channel_frame():
    order, fills = make_order(), [make_fill()]
    frame = wire(codec.order_update(order, fills))
    assert frame["arg"]["channel"] == "orders"
    assert frame["arg"]["instId"] == "BTC-USDT"
    assert frame["data"] == [codec.order_row(order, fills)]


# ── instruments 应答行 ───────────────────────────────────────────────────────

def test_instrument_row_matches_engine_field_list():
    # 清单来自 CSV（按 instId 排序），显式取 BTC-USDT/SPOT 而不是依赖运行时行序
    inst = next(i for i in load_config().instruments if (i.inst_id, i.inst_type) == ("BTC-USDT", "SPOT"))
    row = wire(codec.instrument_row(inst))
    assert list(row) == ENGINE_INSTRUMENT_KEYS
    assert isinstance(row["instIdCode"], int) and row["instIdCode"] == 3
    for key in ENGINE_INSTRUMENT_KEYS[:-1]:
        assert isinstance(row[key], str), f"{key} 必须是 JSON string，实际 {type(row[key]).__name__}"


def test_instrument_row_from_shipped_config():
    # 唯一键是 (instId, instType)：instType 是标的唯一性的一部分，必须按双键取。
    # 字段值以 OKX 真实应答为准（2026-10-03 抓取）：现货没有 settleCcy/ctVal 等 CT 系列字段
    # （空串），老 mock.yml 手工样例里的 "USDT"/"1" 与线上不一致。
    inst = next(i for i in load_config().instruments if (i.inst_id, i.inst_type) == ("BTC-USDT", "SPOT"))
    row = wire(codec.instrument_row(inst))
    assert row["instType"] == "SPOT" and row["instId"] == "BTC-USDT"
    assert (row["baseCcy"], row["quoteCcy"], row["settleCcy"]) == ("BTC", "USDT", "")
    assert row["tickSz"] == "0.1" and row["lotSz"] == "0.00000001" and row["minSz"] == "0.00001"
    assert row["state"] == "live"


def test_instrument_row_carries_contract_fields_for_swap():
    """合约类（SWAP）必须带 CT 系列字段：引擎逐字段取值，缺一个都读不到规格。"""
    inst = next(i for i in load_config().instruments if (i.inst_id, i.inst_type) == ("BTC-USDT-SWAP", "SWAP"))
    row = wire(codec.instrument_row(inst))
    assert inst.inst_id_code == 10459
    assert (row["ctVal"], row["ctMult"], row["ctValCcy"], row["settleCcy"]) == ("0.01", "1", "BTC", "USDT")
    assert row["lever"] == "100"


def test_instrument_row_coerces_config_scalars():
    """配置里手写成 number 也要在编码边界被归一：除 instIdCode 外全是字符串。"""
    inst = InstrumentConfig(inst_id="SYN-USDT", inst_id_code="42", ct_val=1, tick_sz=0.01)
    row = wire(codec.instrument_row(inst))
    assert row["instIdCode"] == 42 and isinstance(row["instIdCode"], int)
    assert row["ctVal"] == "1" and row["tickSz"] == "0.01"


# ── REST 应答外壳 ────────────────────────────────────────────────────────────

def test_rest_ok_envelope():
    body = wire(codec.rest_ok([codec.instrument_row(load_config().instruments[0])]))
    assert body["code"] == "0" and isinstance(body["code"], str)
    assert body["msg"] == "" and isinstance(body["data"], list)
    assert "ts" not in body  # 只有 books 需要顶层 ts


def test_rest_ok_books_ts_is_string():
    """`/api/v5/market/books` 顶层 `ts` 必须是字符串（引擎 `GetString()` 读）。"""
    body = wire(codec.rest_ok([{"asks": [["60000.1", "0.5"]], "bids": []}], ts_ms=1_700_000_000_123))
    assert body["ts"] == "1700000000123" and isinstance(body["ts"], str)


# ── 数值编码边界 ─────────────────────────────────────────────────────────────

@pytest.mark.parametrize(
    "value, expected",
    [
        (60000.0, "60000"),          # 整数浮点去掉尾部 .0
        (60000.1, "60000.1"),        # 最短往返，不引入误差
        (1e-08, "0.00000001"),       # 不小写成科学计数法
        (0, "0"),
        (0.0, "0"),
        ("60000.10", "60000.10"),    # 字符串原样透传（保留下单报文的精度）
    ],
)
def test_num_encoding(value, expected):
    """REST 的 K 线/余额行与盘口档位都复用 `num`，边界行为在此钉死。"""
    assert codec.num(value) == expected
    assert isinstance(codec.num(value), str) and isinstance(codec.level(value, value)[0], str)


def test_bbo_frame_uses_num_encoding():
    item = wire(codec.bbo_frame("BTC-USDT", bid=(1e-08, 60000.0), ask=None, ts_ms=1, seq=1))["data"][0]
    assert item["bids"][0] == ["0.00000001", "60000"]


def test_num_rejects_bool():
    """bool 是 int 子类，漏进去会编码成 "True" —— 静默产坏包，不如响亮报错。"""
    with pytest.raises(TypeError):
        codec.bbo_frame("BTC-USDT", bid=(True, 1.0), ask=None, ts_ms=1, seq=1)
