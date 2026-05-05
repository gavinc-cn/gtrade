//
// Created by dell on 2025/2/22.
//

#pragma once

#include "type_define.h"
#include "str_types.h"


struct QuoteSub {
    char channel[kChannelSz];
    char market[kMarketSz];
    char inst_id[kInstrumentSz];
    char strat_id[kStrategyIdSz];
};
inline auto GetPKey(const QuoteSub& st) {
    return std::make_tuple(st.channel, st.market, st.inst_id);
}

struct TradeSub {
    // char channel[kChannelSz];
    MarketCs market;
    char account_id[kAccountIdSz];
    char inst_id[kInstrumentSz];
    char strat_id[kStrategyIdSz];
};
inline auto GetPKey(const TradeSub& st) {
    return std::make_tuple(st.account_id, st.inst_id);
}

struct KLineSub {
    MarketCs market;
    InstrumentCs instrument;
    int coefficient;
    char scale;
    StrategyIdCs strat_id;
};
inline auto GetPKey(const KLineSub& st) {
    return std::make_tuple(st.market, st.instrument, st.coefficient, st.scale);
}

struct OrderReq {
    MarketCs market;
    AccountIdCs account_id;
    PortfolioCs portfolio;
    InstrumentCs inst_id;
    int64_t inst_id_code;
    PolicyNoCs policy_no;
    PrivateNoCs private_no;
    int64_t entno;
    char bs_side;
    char pos_side;
    char oc_side;
    char price_type;
    char trade_mode;
    double price;
    double amount;
    int64_t expire_time;
    int64_t ent_time;
    int64_t quote_monotonic;
};

struct WithdrawReq {
    MarketCs market;
    AccountIdCs account_id;
    InstrumentCs instrument;
    int64_t entno;
};

struct WithdrawRsp {
    int64_t entno;
    int64_t ex_time;
    int err_code;
    char err_msg[128];
};

struct WebSocketOpenNotify {
    AccountIdCs account_id;
};

enum class StrategyEnvStatus {
    Stopped,
    Running,
    Paused,
};

// 策略状态数据结构
struct StrategyInfo {
    StrategyIdCs id; // 策略ID
    StrategyNameCs strat_name; // 策略名称
    TemplateNameCs strat_template; // 策略模板
    StrategyEnvStatus status; // 策略状态: 0-停止, 1-运行, 2-暂停
    StrategyParamCs param; // 策略配置
    StrategyIndicatorCs indicator; // 策略指标
    int64_t create_time; // 创建时间
    int64_t update_time; // 更新时间
};

struct HttpAddStrategyReq {
    ConfigPathCs config_path;
};

struct HttpDeleteStrategyReq {
    StrategyIdCs strat_id;
};

struct HttpRestartStrategyReq {
    StrategyIdCs strat_id;
};

struct HttpStartStrategyReq {
    StrategyIdCs strat_id;
};

struct HttpStopStrategyReq {
    StrategyIdCs strat_id;
};

struct HttpQueryStrategiesByTemplateReq {
    TemplateNameCs template_name;
};

struct HttpGetTemplateConfigReq {
    TemplateNameCs template_name;
};

// HTTP响应结构
struct HttpStrategyOperationRsp {
    bool success;
};

struct HttpQueryRsp {
    HttpResponseCs response;
};

// HTTP系统管理请求/响应
struct HttpSaveSnapshotRsp {
    bool success;
    char snapshot_path[256];  // 快照文件路径
    char error_msg[128];      // 错误信息
};

struct HttpWalStatsRsp {
    bool success;
    // 共享内存 WAL
    uint64_t shm_write_pos;
    uint64_t shm_confirmed_pos;
    uint64_t shm_unconfirmed_bytes;
    // 文件 WAL
    uint64_t file_current_seq;
    uint64_t file_total_size_bytes;
    // 快照
    uint64_t last_snapshot_seq;
    // WAL 文件大小 (MB)
    uint64_t wal_file_size_mb;
    // 是否需要快照
    bool need_snapshot;
};

// 策略同步查询市场信息
struct StratQryMarketInfoReq {
    StrategyIdCs strat_id;
    MarketCs market;
    InstrumentCs instrument;
};
