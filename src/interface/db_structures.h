#pragma once

#include "pch.h"
#include "str_types.h"

// 自动生成的数据库结构体定义
// 来源: db_tables.xml

// 资金表
struct Balance {
    int64_t ex_time;  // 交易所时间(纳秒)
    int64_t local_time;  // 本地时间(纳秒)
    DateTimeCs datetime;  // 格式化时间
    MarketCs market;  // 市场
    AccountIdCs account_id;  // 账户ID
    CurrencyCs currency;  // 币种
    double available;  // 可用余额
    double frozen;  // 冻结余额
    double total;  // 总余额
};

// K线数据表
struct KLine {
    int64_t ex_time;  // 交易所时间戳(纳秒)
    int64_t local_time;  // 本地接收时间戳(纳秒)
    DateTimeCs datetime;  // 格式化时间字符串
    MarketCs market;  // 市场
    InstrumentCs instrument;  // 标的
    int coefficient;  // 时间周期系数
    CharCs scale;  // 时间周期单位(d/H/M/S)
    double open;  // 开盘价
    double high;  // 最高价
    double low;  // 最低价
    double close;  // 收盘价
    double volume;  // 成交量
};

// 委托表
struct Order {
    int64_t entno;  // 委托号
    CharCs status;  // 委托状态
    int64_t fmt_time;  // 格式化时间
    MarketCs market;  // 市场
    AccountIdCs account_id;  // 账户ID
    PortfolioCs portfolio;  // 组合
    CharCs inst_type;  // 标的类型
    InstrumentCs inst_id;  // 标的名称
    int64_t inst_id_code;  // 标的唯一标识代码
    PolicyNoCs policy_no;  // 策略编号
    PrivateNoCs private_no;  // 私有号
    CharCs bs_side;  // 买卖方向(B/S)
    CharCs pos_side;  // 持仓方向
    CharCs oc_side;  // 开平方向
    CharCs trade_mode;  // 交易模式
    CharCs price_type;  // 价格类型
    double price;  // 价格
    double amount;  // 数量
    int64_t ent_time;  // 委托时间(纳秒)
    int64_t expire_time;  // 委托超时时间(纳秒)
    int64_t ex_entno;  // 交易所委托号
    double filled_px;  // 成交均价
    double filled;  // 成交数量
    double remain;  // 剩余数量
    int64_t confirm_time;  // 委托确认时间(纳秒)
    int64_t filled_time;  // 成交时间(纳秒)
    int64_t update_time;  // 更新时间(纳秒)
    CharCs source;  // 委托来源
    int err_code;  // 错误码
    ErrMsgCs err_msg;  // 错误消息
    int64_t drawno;  // 撤单号
    double draw_amt;  // 撤单数量
    int64_t withdraw_time;  // 撤单时间(纳秒)
    int status_id;  // 订单状态id
    int64_t status_gid;  // 订单状态全局id
    double trd_px;  // 最近一笔成交价
    double trd_qty;  // 最近一笔成交量
    int64_t quote_monotonic;  // 延时测量 T0 贯穿（rdtsc 域 ns，源自 Depth.monotonic → 策略 OrderReq → 此处），非 DB 持久化字段
};

// 持仓
struct Position {
    MarketCs market;  // 市场
    AccountIdCs account_id;  // 账户ID
    CharCs inst_type;  // 标的类型
    InstrumentCs instrument;  // 标的名称
    CharCs pos_side;  // 持仓方向[PosSide]
    PortfolioCs portfolio;  // 组合
    CharCs margin_mode;  // 保证金模式
    double avg_px;  // 成交均价
    double available;  // 可用数量
    int64_t ex_time;  // 交易所时间
    int64_t local_time;  // 本地时间
    DateTimeCs datetime;  // 格式化时间
    double upl;  // 未实现盈亏：交易所推送(账户持仓) / 本地计算(策略持仓)
    double upl_ratio;  // 未实现盈亏比率 - 来自交易所
    double notional_usd;  // 名义价值(USD) - 来自交易所
    double total_cost;  // 总成本（累计开仓价值 = Σ(数量 × 开仓价格)）
    double realized_pnl;  // 已实现盈亏（平仓产生的盈亏）
    double fee_paid;  // 已支付手续费
    CharCs pos_source;  // 持仓来源[PosSource]
};

// 策略运行日志表
struct StrategyLog {
    PolicyNoCs strat_id;  // 策略编号
    CharCs log_level;  // 日志等级: I/W/E
    int64_t log_time;  // 日志时间(纳秒)
    uint16_t seq;  // 同纳秒内序号，防止主键碰撞
    LogContentCs content;  // 日志内容
};

// 成交表
struct Trade {
    int64_t tdno;  // 成交号
    MarketCs market;  // 市场
    AccountIdCs account_id;  // 账户ID
    PortfolioCs portfolio;  // 组合
    InstrumentCs instrument;  // 标的名称
    PolicyNoCs strat_id;  // 策略编号
    PrivateNoCs private_no;  // 私有号
    int64_t ordno;  // 委托号
    CharCs td_side;  // 买卖方向(B/S)
    CharCs pos_side;  // 持仓方向
    CharCs px_type;  // 价格类型
    double td_px;  // 成交价格
    double td_qty;  // 成交数量
    double td_val;  // 成交金额
    int64_t filled_time;  // 成交时间(纳秒)
    int64_t ord_status_id;  // 委托状态id
    CharCs margin_mode;  // 保证金模式
};
