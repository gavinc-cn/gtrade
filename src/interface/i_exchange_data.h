
#pragma once

#include "pch.h"
#include "type_define.h"
#include "db_structures.h"

typedef std::map<std::string, std::string> Params;
typedef std::map<std::string, std::string> Body;
typedef std::unordered_map<std::string, std::string> Headers;
typedef const std::string Endpoint;
typedef const std::string Method;
typedef std::string Symbol;
typedef std::string OrderId;
typedef std::vector<std::vector<double>> Asks;
typedef std::vector<std::vector<double>> Bids;
typedef double Price;
typedef double Amount;

// enum Side {BUY, SELL};
// enum OrderType {LIMIT, MARKET, LIMIT_MAKER, STOP_LIMIT};

struct Base
{
    int64_t ex_time;
    int64_t local_time;
    DateTimeCs datetime;
};

struct Ohlcv: public Base
{

};

struct MarketInfo: public Base
{
    MarketCs market;
    CharCs inst_type;
    InstrumentCs instrument;
    int64_t inst_id_code;
    CurrencyCs base_ccy;
    CurrencyCs quote_ccy;
    CurrencyCs settle_ccy;
    double contract_val;
    double contract_multi;
    CurrencyCs contract_val_ccy;
    int64_t list_time;
    int64_t exp_time;
    int lever;
    double price_unit;
    double amt_unit;
    double min_amt;
    char inst_state;
};

// 同步查询响应头：同步链路（PostSyncMsg）的 Buffer 表达不了失败 —— 框架会把 handler
// 返回的 nullptr 兜底成非空空响应（见 utilities/zrtools/io_pool_v2/i_handler.h），
// 调用方只剩 GetSize() 可用，于是"该 inst_type 0 条标的"会被误判成查询失败。
// 故把成败显式写进响应首部：8 字节，保证紧随其后的 MarketInfo 数组保持 8 字节对齐。
struct MarketInfoQryRspHeader {
    int64_t ok {0};   // 1=查询成功（后面可以跟 0 条 MarketInfo，属合法结果）；0=查询失败
};

struct Ticker: public Base
{

};

struct KLineRange {
    int64_t req_id;
    StrategyIdCs strat_id;
    MarketCs market;
    InstrumentCs instrument;
    int coefficient;
    char scale;
    int64_t start_time;
    int64_t end_time;
    int count;
    KLine klines[0];
};

struct Depth: public Base
{
    char market[kMarketSz];
    char symbol[kInstrumentSz];
    int ask_cnt;
    double ask_price[10];
    double ask_amount[10];
    int bid_cnt;
    double bid_price[10];
    double bid_amount[10];
    int64_t seq_id;
    int64_t monotonic;
};

// struct DepthRange {
//     int64_t req_id;
//     StrategyIdCs strat_id;
//     MarketCs market;
//     InstrumentCs instrument;
//     int level;
//     int64_t start_time;
//     int64_t end_time;
//     int count;
//     Depth depths[0];
// };

