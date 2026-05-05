//
// Created by Claude Code
//

#pragma once

#include "pch.h"
#include "3rd/httplib.h"
#include "type_define.h"
#include "service_map.h"
#include <thread>
#include <atomic>

class StrategyEngine;

class HttpGateway final : public MyHandler {
public:
    HttpGateway(ServiceMap& pool, const GTradeConfig& gtrade_cfg);
    ~HttpGateway() override;

    bool Init() override;
    bool Start() override;
    void Stop() override;

private:
    // 处理策略管理请求（仅操作类接口，查询类接口由 web_server 提供）
    void HandleAddStrategy(const httplib::Request& req, httplib::Response& res);
    void HandleDeleteStrategy(const httplib::Request& req, httplib::Response& res);
    void HandleRestartStrategy(const httplib::Request& req, httplib::Response& res);
    void HandleStartStrategy(const httplib::Request& req, httplib::Response& res);
    void HandleStopStrategy(const httplib::Request& req, httplib::Response& res);
    void HandleBatchOperation(const httplib::Request& req, httplib::Response& res);
    // 系统管理
    void HandleSnapshot(const httplib::Request& req, httplib::Response& res);
    void HandleWalStats(const httplib::Request& req, httplib::Response& res);

    // HTTP服务器线程函数
    void ServerThread();

    ServiceMap& m_pool;
    GTradeConfig m_gtrade_cfg {};
    MyHandler* m_strategy_engine {};

    std::unique_ptr<httplib::Server> m_server {};
    std::thread m_server_thread {};
    std::atomic<bool> m_running {false};
};
