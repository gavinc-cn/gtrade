//
// Created by dell on 2025/2/15.
//

#pragma once

#include "pch.h"
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include "sonic_helper.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "websocket_base.h"
#include "i_exchange_data_dump.h"
#include "i_strategy_engine_dump.h"
#include "type_define.h"
#include "misc.h"
#include "str_utils.h"
#include "json_helper.h"
#include "zrtools/zrt_time.h"
#include "dict.h"
#include "qry_processor.h"
#include "service_map.h"
#include "persistence_buffer.h"
#include "zrtools/zrt_shm_direct.h"
#include <chrono>

class StrategyEngine;
class MysqlClient;

class MySqlGateway final : public MyHandler {
public:
    MySqlGateway(ServiceMap& pool, const GTradeConfig& gtrade_cfg);
    ~MySqlGateway() override = default;
    bool Init() override;
    bool Start() override;
    void OnDbSetStrategyInfo(int msg_id, const BufPtr buffer);
    void OnDbDelStrategyInfo(int msg_id, const BufPtr buffer);
    void OnDbDelAllStrategyInfo(int msg_id, const BufPtr buffer);
    void OnDbSetKLine(int msg_id, const BufPtr buffer);
    void OnDbDelKLine(int msg_id, const BufPtr buffer);
    void OnDbDelOrder(int msg_id, const BufPtr buffer);
    void OnDbSetOrder(int msg_id, const BufPtr buffer);
    void OnDbDelTrade(int msg_id, const BufPtr buffer);
    void OnDbSetTrade(int msg_id, const BufPtr buffer);
    void OnDbSetPosition(int msg_id, const BufPtr buffer);
    void OnDbSetPortfolioPosition(int msg_id, const BufPtr buffer);
    void OnDbSetStrategyLog(int msg_id, const BufPtr buffer);
    BufPtr OnDbQueryHisOrdersReq(int msg_id, const BufPtr buffer);
    void OnDbSetInstrumentScope(int msg_id, const BufPtr buffer);
    BufPtr OnDbQueryInstrumentScopeReq(int msg_id, const BufPtr buffer);
    // 启动高水位：查 order / trade 的最大号（同步），供客户端设置号段基数
    BufPtr OnDbQueryMaxIdsReq(int msg_id, const BufPtr buffer);

private:
    // SQL字符串转义函数
    std::string EscapeString(const std::string& str) const;

    // ============ 共享内存轮询持久化 ============
    // 轮询共享内存并批量写入数据库
    void PollSharedMemory();
    // 重试写入委托（带指数退避）
    bool WriteOrderWithRetry(const Order& order, int retry_count = 0);
    // 重试写入成交（带指数退避）
    bool WriteTradeWithRetry(const Trade& trade, int retry_count = 0);
    // 重试写入持仓（带指数退避）
    bool WritePositionWithRetry(const Position& position, const std::string& table_name, int retry_count = 0);
    // 重试写入组合持仓（带指数退避）
    bool WritePortfolioPositionWithRetry(const Position& position, int retry_count = 0);
    // 重试写入资金（带指数退避）
    bool WriteBalanceWithRetry(const Balance& balance, int retry_count = 0);

    MyHandler* m_strategy_engine {};
    GTradeConfig m_gtrade_cfg {};
    size_t m_curr_processor_idx {};
    std::vector<std::unique_ptr<QueryProcessor>> m_qry_processor_pool {};
    // <<msg_id,market,account_id>,steady_timer>
    std::unordered_map<std::tuple<int,std::string,std::string>,std::unique_ptr<boost::asio::steady_timer>,zrt::TupleHasher> m_timer_map {};

    // 数据库客户端
    std::unique_ptr<MysqlClient> m_mysql_client {};

    // ============ 共享内存持久化相关成员 ============

    // 共享内存访问
    zrt::DirectShm<OrderPersistenceBuffer> m_order_shm {kOrderShmName};
    zrt::DirectShm<TradePersistenceBuffer> m_trade_shm {kTradeShmName};
    zrt::DirectShm<PositionPersistenceBuffer> m_position_shm {kPositionShmName};
    zrt::DirectShm<PortfolioPositionPersistenceBuffer> m_portfolio_position_shm {kPortfolioPositionShmName};
    zrt::DirectShm<BalancePersistenceBuffer> m_balance_shm {kBalanceShmName};

    // MessageServer引用（用于发送Slack通知）
    MyHandler* m_message_server {};

    // 轮询定时器
    std::unique_ptr<boost::asio::steady_timer> m_poll_timer;

    // 轮询间隔（毫秒）
    static constexpr int64_t POLL_INTERVAL_MS = 100;  // 100ms轮询一次

    // 批量处理大小
    static constexpr size_t BATCH_SIZE = 100;  // 每次最多处理100条

    // 重试配置
    static constexpr int MAX_RETRY_COUNT = 5;  // 最多重试5次
    static constexpr int64_t INITIAL_RETRY_DELAY_MS = 1000;  // 初始重试延迟1秒
    static constexpr int64_t MAX_RETRY_DELAY_MS = 60000;     // 最大重试延迟60秒

    // 告警节流
    struct AlertThrottle {
        std::chrono::steady_clock::time_point last_alert_time;
        size_t alert_count {0};
        static constexpr int64_t ALERT_INTERVAL_MS = 60000;  // 1分钟内最多告警一次
    };
    AlertThrottle m_db_failure_alert;
};

