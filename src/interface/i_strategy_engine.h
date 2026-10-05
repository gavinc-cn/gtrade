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
// 主键返回内容键：char[] 若直接放进 make_tuple 会衰减成 const char*，键就变成按地址比较
// （同内容不同实例互相命中不到），且指针指向调用方的临时对象、易悬垂；故此处显式构造 std::string。
inline auto GetPKey(const QuoteSub& st) {
    return std::make_tuple(std::string(st.channel), std::string(st.market), std::string(st.inst_id));
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

// ── 补查接口（web_server → 引擎，读内存权威态）───────────────────────────────
// 游标语义见方案 rev4 §5：委托按 entno、成交按 tdno，两者都是引擎本地生成的单调号。
// 响应统一为"若干条 Order / Trade 记录"（TBuffer 内连续存放，用 ForEach<T> 展开）：
//   - 空响应 = 没有更多数据（也用于内部异常，异常另记 SPDLOG_ERROR）
//   - "是否还有更多"由调用方按 返回条数 == limit 判断（返回满 limit 即当可能还有）
constexpr int kQueryMaxRows = 500;        // 单次补查返回条数上限
constexpr int kQueryDefaultRows = 200;    // 未指定 limit 时的默认条数
constexpr int kQueryMaxEntnos = 500;      // by_entnos 模式一次最多带多少个委托号

struct HttpQueryOrdersReq {
    int64_t cursor_entno;                 // 游标模式：只取 entno > cursor_entno（0 = 从头）
    int     entno_cnt;                    // >0 时进入 by_entnos 模式（此时忽略 cursor_entno）
    int     limit;                        // 返回条数上限（0 = kQueryDefaultRows）
    int64_t entnos[kQueryMaxEntnos];      // by_entnos 模式：客户端已知在途委托号
};

struct HttpQueryTradesReq {
    int64_t cursor_tdno;                  // 只取 tdno > cursor_tdno（0 = 从头）
    int     limit;                        // 返回条数上限（0 = kQueryDefaultRows）
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

// ── MCP 交易接口结构体 ───────────────────────────────────────────────────────────

struct HttpPlaceOrderReq {
    AccountIdCs  account_id;   // char[32]
    MarketCs     market;       // char[16]  e.g. "okx"
    InstrumentCs inst_id;      // char[32]  e.g. "BTC-USDT"
    PortfolioCs  portfolio;    // char[32]  组合，可选；空则 FillNewEntByReq 回落为 policy_no
    char td_mode;              // TradeMode::Em  ('h'=cash, 'c'=cross, 'i'=isolated)
    char side;                 // TradeSide::Em  ('b'=buy, 's'=sell)
    char ord_type;             // PriceType::Em  ('l'=limit, 'm'=market, 'y'=post_only, 'o'=fok, 'a'=ioc)
    double px;                 // 价格（market单填0）
    double sz;                 // 数量
    int64_t ent_time;          // 报单时间 epoch19，由 HttpGateway 填写
};

struct HttpPlaceOrderRsp {
    bool    success;
    int64_t order_id;          // 本地 entno，不等 OKX 确认
    char    error_msg[128];
};

struct HttpCancelOrderReq {
    AccountIdCs account_id;    // char[32]
    int64_t     order_id;      // 本地 entno
};

struct HttpCancelOrderRsp {
    bool success;
    char error_msg[128];
};

struct HttpGetDepthReq {
    InstrumentCs inst_id;      // char[32]
    MarketCs     market;       // char[16]
};

struct HttpGetDepthRsp {
    bool         success;
    InstrumentCs inst_id;      // char[32]
    MarketCs     market;       // char[16]
    int          ask_cnt;
    double       ask_price[10];
    double       ask_amount[10];
    int          bid_cnt;
    double       bid_price[10];
    double       bid_amount[10];
    int64_t      timestamp;    // ex_time (nanoseconds)
    char         error_msg[128];
};

// ── 标的范围订阅接口结构体（web 设置页）────────────────────────────────────
// 范围是 (market, inst_id, inst_type) 集合；owner 标识 k_scope_owner 表示 web 设置页订阅，
// 与策略 id 共同构成引用计数：策略仍在用时不会真正退订。
// inst_type 是标的唯一性的一部分（OKX 的币币杠杆 MARGIN 复用现货的 inst_id 与 instIdCode），
// 取值用 OKX 风格字符串（"SPOT"/"SWAP"/...），与 InstrumentInfoItem 同型。
inline constexpr const char* k_scope_owner = "__scope__";
inline constexpr int kMaxScopeItems = 128;   // 单次保存标的上限
inline constexpr int kInstTypeSz = 16;       // inst_type 字段长度（OKX 风格字符串）

struct InstrumentScopeItem {   // 范围条目 / DB 行
    MarketCs market;           // char[16]
    InstrumentCs inst_id;      // char[32]
    char inst_type[kInstTypeSz];   // OKX 风格：SPOT/SWAP/...（见 DictInstType2Okx）
};

struct InstrumentInfoItem {    // 全量标的列表条目
    MarketCs market;
    InstrumentCs inst_id;
    char inst_type[kInstTypeSz];   // OKX 风格：SPOT/SWAP/...（经 DictInstType2Okx 转出）
};

struct ScopeOwnerItem {        // 订阅现状行：owner 为策略 id 或 k_scope_owner
    MarketCs market;
    InstrumentCs inst_id;
    char inst_type[kInstTypeSz];   // 订阅键不含类型，此处由引擎从范围集合反查填入；不在范围内为空
    StrategyIdCs owner;        // char[64]
};

struct HttpSetInstrumentScopeReq {
    int count {};
    InstrumentScopeItem items[kMaxScopeItems] {};
};

struct HttpSetInstrumentScopeRsp {
    bool success {};
    int applied_cnt {};
    int removed_cnt {};
    int kept_cnt {};
    int unknown_cnt {};
    ErrMsgCs error_msg {};     // char[128]
};
