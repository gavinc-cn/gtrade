"""OKX v5 报文编码：领域对象 → 可直接 `json.dumps` 的 dict。

**本模块是字段类型的唯一出口。** 引擎侧类型不符的后果不是"解析降级"而是
Debug 构建 `abort()`，所以三条硬约束在这里集中兜住：

1. 引擎按 `GetString()` 读的字段（px/sz/accFillSz/ts/state/code/sCode/…）必须是 JSON string
   （sonic 解析没有 numbers-as-strings 开关，数字会被 catch 成 kJsonParseError）；
2. `instIdCode` 引擎按 `GetInt64()` 读，必须是 JSON number；
3. `code` / `sCode` 固定字符串 `"0"`。

函数只吐 str/int/list/dict，绝不把 float / Decimal 漏进报文（漏了 `json.dumps`
会直接抛 TypeError，tests 用一次真实 JSON 往返兜住）。字段清单逐字对照
`doc_ai/spec/mock_exchange/引擎OKX契约.md`。
"""

import time
from decimal import Decimal

from mockex.core.models import Order

# 时间字段统一毫秒：引擎读后自己 *= 1e6 转纳秒，mock 绝不预乘。
# `num` 是价量 → 字符串的唯一入口：REST 的 K 线/余额行、盘口档位都复用它，
# 免得各端点自己 `str(float)` 把 `1e-08` 这种科学计数法漏进报文。
__all__ = [
    "bbo_frame",
    "instrument_row",
    "level",
    "num",
    "order_ack",
    "order_row",
    "order_update",
    "rest_ok",
    "to_str_ms",
]


def to_str_ms(ms: int) -> str:
    """毫秒 epoch → OKX 时间字符串（`"1700000000123"`）。

    `time.time() * 1000` 是 float，直接 `str()` 会吐出 `"1700000000123.456"` ——
    引擎能转但契约要求毫秒整数字符串，这里统一截断。
    """
    return str(int(ms))


def num(value) -> str:
    """价 / 量 / 金额 → OKX 字符串（金额与数量统一按字符串上报）。

    - `str` 原样透传（保留下单报文里读到的精度，不做二次舍入）；
    - `int` → `str`；bool 是 int 子类，显式拒绝（漏进去会编码成 `"True"`）；
    - `float` → 十进制展开，**绝不出现科学计数法**（`1e-08` → `"0.00000001"`），
      整数值去掉尾部 `.0`（`60000.0` → `"60000"`）。
    """
    if isinstance(value, str):
        return value
    if isinstance(value, bool):
        raise TypeError(f"数值字段不接受 bool: {value!r}")
    if isinstance(value, int):
        return str(value)
    text = format(Decimal(repr(float(value))), "f")
    if text.endswith(".0"):
        text = text[:-2]
    return "0" if text == "-0" else text


def _now_ms() -> int:
    """当前毫秒 epoch（调用方未显式给时间时的兜底）。"""
    return int(time.time() * 1000)


def level(px, sz) -> list[str]:
    """盘口一档 `(px, sz)` → 档位数组 `["60000.1", "0.5"]`（bbo 与 books 同形）。

    真实 OKX 档位还有尾部元数据（挂单数/强平标记），引擎只读 `[0]`/`[1]`，
    契约也只承诺 `[px, sz, …]`，故只发两元素。
    """
    return [num(px), num(sz)]


def bbo_frame(inst_id: str, bid, ask, ts_ms: int, seq: int) -> dict:
    """`bbo-tbt` 推送帧：`{"arg":{...},"data":[{ts,seqId,asks,bids}]}`。

    Args:
        inst_id: 合约，如 "BTC-USDT"。
        bid: 买一 `(px, sz)`；该侧无挂单传 None（编码成空数组）。
        ask: 卖一 `(px, sz)`；同上。
        ts_ms: 交易所时间（毫秒 epoch），编码成字符串 —— 数字会命中引擎
            `GetString()` 断言。
        seq: 序号，JSON number（引擎走 `GetInt64` 路径）。

    Returns:
        dict：可直接 `json.dumps` 的推送帧。
    """
    return {
        "arg": {"channel": "bbo-tbt", "instId": inst_id},
        "data": [{
            "ts": to_str_ms(ts_ms),
            "seqId": int(seq),
            "asks": [] if ask is None else [level(*ask)],
            "bids": [] if bid is None else [level(*bid)],
        }],
    }


def order_ack(op: str, req_id, cl_ord_id: str, ord_id: str, s_code: str = "0", s_msg: str = "",
              ts_ms: int | None = None) -> dict:
    """下单 / 撤单指令回执（引擎 `OnPlaceOrderRsp` / `OnCancelOrderRsp` 逐字段读）。

    Args:
        op: 原样的操作名 —— "order" 或 "cancel-order"（引擎按它分发）。
        req_id: 请求里的 `id`（引擎的 entno），原样回显。
        cl_ord_id: 客户端委托号。
        ord_id: 交易所委托号；失败时给空串。
        s_code: 单笔结果码，**始终字符串**；非 "0" 时顶层 `code` 置 "1"（OKX 语义）。
        s_msg: 单笔结果说明。
        ts_ms: 回执时间（毫秒 epoch），缺省用当前时间 —— 引擎把它当委托确认时间，
            给 0 会让策略侧时间倒挂。

    Returns:
        dict：`{"id","op","code","msg","data":[{clOrdId,ordId,ts,sCode,sMsg}]}`。
    """
    s_code = str(s_code)
    return {
        "id": req_id,
        "op": op,
        "code": "0" if s_code == "0" else "1",
        "msg": "",
        "data": [{
            "clOrdId": cl_ord_id,
            "ordId": ord_id,
            "ts": to_str_ms(_now_ms() if ts_ms is None else ts_ms),
            "sCode": s_code,
            "sMsg": str(s_msg),
        }],
    }


def _avg_px(order: Order) -> str:
    """整单累计成交均价：`filled_amount / acc_fill_sz`；未成交返回 `"0"`。

    契约里的 `avgPx` 是**整单累计**语义（引擎 `entrust.filled_px ← ex_order["avgPx"]`），
    不是本批成交的均价 —— M2 分次推送部分成交（只带增量成交）时，按增量算会把错误的
    成交价静默写进策略，故一律取委托上的累计量。

    Raises:
        ValueError: 有累计成交量却没有累计成交额 —— 调用方漏维护 `Order.filled_amount`，
            此时算出来的任何均价都是错的，宁可响亮报错也不静默产坏包。
    """
    if order.acc_fill_sz == 0:
        return "0"
    if order.filled_amount <= 0:
        raise ValueError(
            f"Order(ord_id={order.ord_id!r}) 的 acc_fill_sz={order.acc_fill_sz} 但 "
            f"filled_amount={order.filled_amount}：撮合侧必须同步维护累计成交额，"
            "否则 avgPx 无法计算"
        )
    return num(order.filled_amount / order.acc_fill_sz)


def order_row(order: Order, fills=()) -> dict:
    """一张委托 → `orders` 频道 / REST 查单应答的 `data[]` 元素。

    引擎 sonic 解析本行，全部按 `GetString()` 读 → 数值与时间字段一律字符串。
    刻意**不含** `instIdCode`：它只在 instruments 应答里，且必须是 JSON number。

    Args:
        order: 委托（`px`/`sz`/`acc_fill_sz`/`filled_amount` 已在撮合侧推进）。
        fills: 本次推送携带的成交（`MatchingEngine.place` 的返回值），只用于推导
            `fillPx`/`fillSz`/`fillTime`（最后一笔成交）；无成交时这三个字段按 OKX
            惯例给空串（引擎的 convert 把空串当 0，安全）。`avgPx` **不看本参数** ——
            它是整单累计口径，取自 `order.filled_amount / order.acc_fill_sz`。

    Returns:
        dict：`orders` 推送行 / REST 委托查询行。
    """
    fills = list(fills)
    last = fills[-1] if fills else None
    return {
        "instType": str(order.inst_type),
        "instId": order.inst_id,
        "clOrdId": order.cl_ord_id,
        "ordId": order.ord_id,
        "px": num(order.px),
        "sz": num(order.sz),
        "side": order.side,
        "tdMode": order.td_mode,
        "ordType": order.ord_type,
        "posSide": order.pos_side,
        "state": order.state,
        "accFillSz": num(order.acc_fill_sz),
        "avgPx": _avg_px(order),
        "fillPx": "" if last is None else num(last.px),
        "fillSz": "" if last is None else num(last.sz),
        "fillTime": "" if last is None else to_str_ms(last.ts),
        "cTime": to_str_ms(order.c_time),
        "uTime": to_str_ms(order.u_time),
        "code": "0",
        "msg": "",
    }


def order_update(order: Order, fills=()) -> dict:
    """`orders` 频道推送帧：`{"arg":{"channel":"orders","instType","instId"},"data":[行]}`。

    Args:
        order: 委托。
        fills: 本次推送携带的成交，透传给 `order_row`。

    Returns:
        dict：可直接 `json.dumps` 的推送帧。
    """
    return {
        "arg": {"channel": "orders", "instType": str(order.inst_type), "instId": order.inst_id},
        "data": [order_row(order, fills)],
    }


def instrument_row(inst) -> dict:
    """`/api/v5/account/instruments` 应答的 `data[]` 元素。

    引擎 `GetMarketInfo` 逐个读这 16 个键，一个都不能少、类型也不能变：
    `instIdCode` 必须是 JSON number（走 `GetInt64()`），其余 15 个键必须是字符串。
    配置侧手写成 number 的标量（`ctVal: 1`）在编码边界归一。

    Args:
        inst: `mockex.config.InstrumentConfig`（或任何同名属性的对象）。

    Returns:
        dict：键序与引擎源码里的读取顺序一致。
    """
    return {
        "instType": str(inst.inst_type),
        "instId": str(inst.inst_id),
        "baseCcy": str(inst.base_ccy),
        "quoteCcy": str(inst.quote_ccy),
        "settleCcy": str(inst.settle_ccy),
        "ctVal": str(inst.ct_val),
        "ctMult": str(inst.ct_mult),
        "ctValCcy": str(inst.ct_val_ccy),
        "listTime": str(inst.list_time),
        "expTime": str(inst.exp_time),
        "lever": str(inst.lever),
        "tickSz": str(inst.tick_sz),
        "lotSz": str(inst.lot_sz),
        "minSz": str(inst.min_sz),
        "state": str(inst.state),
        "instIdCode": int(inst.inst_id_code),
    }


def rest_ok(data, ts_ms: int | None = None) -> dict:
    """REST 成功应答外壳：`code`/`msg` 是字符串，`data` 恒为数组。

    引擎先看 `d["code"].GetString() != "0"` 判失败，且遇空数组仍索引 `data[0]`，
    故该外壳集中在 codec 里，避免每个端点各写一遍。

    Args:
        data: 应答数据项（可迭代），统一转成 list。
        ts_ms: 顶层时间（毫秒 epoch）；`/api/v5/market/books` 需要它且必须是
            字符串，其余端点不传。

    Returns:
        dict：`{"code","msg","data"[,"ts"]}`。
    """
    body = {"code": "0", "msg": "", "data": list(data)}
    if ts_ms is not None:
        body["ts"] = to_str_ms(ts_ms)
    return body
