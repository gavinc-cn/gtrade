//
// Created by dell on 2025/3/8.
//

#pragma once

#include "type_define.h"
#include "str_types.h"

struct QryReqBase {
    MarketCs market;
    AccountIdCs account_id;
    int retry_time; // 重试间隔, 系统内部字段
};

struct MarketInfoQryReq: public QryReqBase {
    CharCs inst_type;
    InstrumentCs instrument;
};

struct DepthQryReq: public QryReqBase {
    int64_t req_id;
    StrategyIdCs strat_id;
    InstrumentCs instrument;
    int level;
    int64_t start_time;
    int64_t end_time;
};

struct KLineQryReq: public QryReqBase {
    int64_t req_id;
    StrategyIdCs strat_id;
    InstrumentCs instrument;
    int coefficient;
    char scale; // 时间尺度, YmdHMS
    int64_t start_time;
    int64_t end_time;
};

struct BalanceQryReq: public QryReqBase {
    CurrencyCs currency;
};

struct HoldQryReq: public QryReqBase {
    CurrencyCs currency;
};

struct DoneQryReq: public QryReqBase {
    InstrumentCs instrument;
    int64_t entno;
    int64_t ex_entno;
};

struct EntrustQryReq: public QryReqBase {
    CharCs inst_type;
    InstrumentCs instrument;
    int64_t entno;
    int64_t ex_entno;
    StrategyIdCs strat_id;
    PrivateNoCs private_no;
};

struct EntrustQryByPrivateNoReq: public QryReqBase {
    PrivateNoCs private_no;
};

struct OpenEntrustsQryReq: public QryReqBase {
    CharCs inst_type;
    InstrumentCs instrument;
    char price_type;
    char status;
};

struct HisEntrustsQryReq: public QryReqBase {
    CharCs inst_type;
    InstrumentCs instrument;
    char price_type;
    char status;
    int64_t start_time;
    int64_t end_time;
};

// 启动高水位查询响应：order / trade 表当前最大号（无记录时为 0）
// 用途：设置号段基数，保证跨重启（含同一秒内重启）ID 单调不重叠
struct MaxIdsQryRsp {
    int64_t max_entno;
    int64_t max_tdno;
};

// 外发通知消息
struct NotifyMessageReq {
    NoticeChannelCs channel;      // 频道名称
    NoticeSubjectCs subject;      // 消息主题
    NoticeContentCs content;      // 消息内容
    bool is_async;            // 是否异步发送
};