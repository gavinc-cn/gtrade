"""mock_exchange 中立领域对象：委托与成交。

交易所无关 —— 这里不出现任何 OKX 专属拼写（`instIdCode` / `tdMode` 这类只活在
`mockex/exchanges/okx/codec.py`）。价格与数量用 float 参与计算，编码成报文时才转
字符串；**时间统一毫秒 epoch**（OKX 协议的原生单位：引擎读到后自己 `*= 1e6` 转
纳秒，mock 侧绝不预乘）。
"""

from dataclasses import dataclass

# 委托状态：取值与 OKX `state` 字段一一对应
# （引擎 `DictStatusFromOkx` 只认这四个，写错一个字母就退化成未知状态）
STATE_LIVE = "live"
STATE_PARTIALLY_FILLED = "partially_filled"
STATE_FILLED = "filled"
STATE_CANCELED = "canceled"

# 买卖方向：取值为 OKX `side` 字段原样（引擎 `DictBsSideFromOkx` 只认这两个）
SIDE_BUY = "buy"
SIDE_SELL = "sell"


@dataclass
class Order:
    """一张委托（订单簿与撮合的唯一凭据）。

    Attributes:
        cl_ord_id: 客户端委托号（引擎的 `entno`），撤单、查单按它索引。
        ord_id: 交易所委托号，mock 自产（"1"、"2"…），引擎原样回存。
        inst_id: 合约，如 "BTC-USDT"。
        side: 买卖方向，取值 `SIDE_BUY` / `SIDE_SELL`。
        px: 委托价。
        sz: 委托量。
        acc_fill_sz: 累计成交量（由撮合推进）。
        state: 委托状态，取值 `STATE_*`。
        owner: 归属标识 —— 引擎账户（api_key）或注入方（"mock_mm"）；
            订单簿按 owner 整批撤单（`OrderBook.remove_owner`）靠它区分自营与外部。
        c_time: 建立时间（毫秒 epoch）。
        u_time: 最后变更时间（毫秒 epoch）。
        inst_type / td_mode / ord_type / pos_side: 随委托原样带回 `orders` 推送帧的
            协议字段（引擎每次推送都会读 `posSide`；SPOT cash 限价单即默认值）。
        filled_amount: **累计**成交额（Σ 成交价 × 成交量），与 `acc_fill_sz` 一起由
            撮合推进；`avgPx = filled_amount / acc_fill_sz` 是整单累计口径，
            分次推送部分成交时也不能退化成"本批均价"。
    """

    cl_ord_id: str
    ord_id: str
    inst_id: str
    side: str
    px: float
    sz: float
    acc_fill_sz: float = 0.0
    state: str = STATE_LIVE
    owner: str = ""
    c_time: int = 0
    u_time: int = 0
    inst_type: str = "SPOT"
    td_mode: str = "cash"
    ord_type: str = "limit"
    pos_side: str = "net"
    # 追加在末尾（而不是紧跟 acc_fill_sz）：brief 的 11 个位置参数顺序必须原样保留
    filled_amount: float = 0.0


@dataclass
class Fill:
    """一笔成交（已发生的撮合事实）。

    Attributes:
        ord_id: 交易所委托号，指回 `Order.ord_id`。
        inst_id: 合约。
        px: 成交价。
        sz: 成交量。
        ts: 成交时间（毫秒 epoch）。
        taker_owner: 主动方归属（吃单方）。
        maker_owner: 被动方归属（挂单方）；immediate 恒空串（隐含对手不是真实账户），
            M2 orderbook 起为被吃挂单的 owner。
        side: 主动方方向 —— 账户结算（`AccountManager.apply_fill`）靠它判断
            资金增减方向；旧调用方不传时为空串。
    """

    ord_id: str
    inst_id: str
    px: float
    sz: float
    ts: int
    taker_owner: str
    maker_owner: str
    side: str = ""


__all__ = [
    "Fill",
    "Order",
    "SIDE_BUY",
    "SIDE_SELL",
    "STATE_CANCELED",
    "STATE_FILLED",
    "STATE_LIVE",
    "STATE_PARTIALLY_FILLED",
]
