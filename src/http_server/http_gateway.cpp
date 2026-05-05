//
// Created by Claude Code
//

#include "http_gateway.h"
#include "strategy_engine.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "i_strategy_engine.h"
#include "zrtools/zrt_misc.h"
#include "sonic_helper.h"

HttpGateway::HttpGateway(ServiceMap& pool, const GTradeConfig& gtrade_cfg)
    : m_pool(pool), m_gtrade_cfg(gtrade_cfg) {
    SetThread(zrt::EnginePool::GetInstance().GetNamedThread(k_HttpGatewayThread));
}

HttpGateway::~HttpGateway() {
    Stop();
}

bool HttpGateway::Init() {
    SPDLOG_INFO("{}", __PRETTY_FUNCTION__);

    m_strategy_engine = m_pool.at(k_StrategyEngine).get();
    m_server = std::make_unique<httplib::Server>();

    // 启用 CORS
    m_server->set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type"}
    });

    m_server->Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    // 设置HTTP路由
    // POST /api/strategy/add - 添加策略
    m_server->Post("/api/strategy/add", [this](const httplib::Request& req, httplib::Response& res) {
        HandleAddStrategy(req, res);
    });

    // DELETE /api/strategy/delete/:id - 删除策略
    m_server->Delete(R"(/api/strategy/delete/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        HandleDeleteStrategy(req, res);
    });

    // POST /api/strategy/restart/:id - 重启策略
    m_server->Post(R"(/api/strategy/restart/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        HandleRestartStrategy(req, res);
    });

    // POST /api/strategy/start/:id - 启动策略
    m_server->Post(R"(/api/strategy/start/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        HandleStartStrategy(req, res);
    });

    // POST /api/strategy/stop/:id - 停止策略
    m_server->Post(R"(/api/strategy/stop/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        HandleStopStrategy(req, res);
    });

    // POST /api/strategy/batch - 批量操作策略
    m_server->Post("/api/strategy/batch", [this](const httplib::Request& req, httplib::Response& res) {
        HandleBatchOperation(req, res);
    });

    // POST /api/system/snapshot - 手动触发快照
    m_server->Post("/api/system/snapshot", [this](const httplib::Request& req, httplib::Response& res) {
        HandleSnapshot(req, res);
    });

    // GET /api/system/wal-stats - 获取 WAL 统计信息
    m_server->Get("/api/system/wal-stats", [this](const httplib::Request& req, httplib::Response& res) {
        HandleWalStats(req, res);
    });

    SPDLOG_INFO("HTTP Gateway initialized on port {}", m_gtrade_cfg.http_server_port);
    return true;
}

bool HttpGateway::Start() {
    SPDLOG_INFO("Starting HTTP Gateway on port {}", m_gtrade_cfg.http_server_port);

    m_running = true;
    m_server_thread = std::thread(&HttpGateway::ServerThread, this);

    return true;
}

void HttpGateway::Stop() {
    if (m_running) {
        SPDLOG_INFO("Stopping HTTP Gateway");
        m_running = false;
        if (m_server) {
            m_server->stop();
        }
        if (m_server_thread.joinable()) {
            m_server_thread.join();
        }
    }
}

void HttpGateway::ServerThread() {
    SPDLOG_INFO("HTTP server thread started");
    m_server->listen("0.0.0.0", m_gtrade_cfg.http_server_port);
    SPDLOG_INFO("HTTP server thread stopped");
}

void HttpGateway::HandleAddStrategy(const httplib::Request& req, httplib::Response& res) {
    SPDLOG_INFO("Received add strategy request");
    SPDLOG_TRACE("Request body: {}", req.body);

    rapidjson::Document doc;
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    try {
        // 解析请求JSON
        rapidjson::Document req_doc;
        req_doc.Parse(req.body.c_str());

        if (!req_doc.HasMember("config_path") || !req_doc["config_path"].IsString()) {
            doc.SetObject();
            doc.AddMember("success", false, doc.GetAllocator());
            doc.AddMember("error", "Missing or invalid config_path parameter", doc.GetAllocator());
            doc.Accept(writer);
            res.set_content(buffer.GetString(), "application/json");
            res.status = 400;
            return;
        }

        std::string cfg_path = req_doc["config_path"].GetString();
        SPDLOG_TRACE("Parsed config_path: {}", cfg_path);

        // 通过PostSyncMsg调用StrategyEngine
        HttpAddStrategyReq http_req{};
        zrt::fill_field(http_req.config_path, cfg_path);
        auto req_buf = std::make_shared<TBuffer>(http_req);
        BufPtr rsp_buf {};
        m_strategy_engine->PostSyncMsg(kHttpAddStrategy, req_buf, rsp_buf);
        const auto& rsp = *reinterpret_cast<const HttpStrategyOperationRsp*>(rsp_buf->Data());
        bool success = rsp.success;
        SPDLOG_TRACE("Received response from StrategyEngine: success={}", success);

        doc.SetObject();
        doc.AddMember("success", success, doc.GetAllocator());
        if (success) {
            rapidjson::Value msg;
            msg.SetString("Strategy added successfully", doc.GetAllocator());
            doc.AddMember("message", msg, doc.GetAllocator());
        } else {
            rapidjson::Value msg;
            msg.SetString("Failed to add strategy. Check logs for details.", doc.GetAllocator());
            doc.AddMember("error", msg, doc.GetAllocator());
        }

        doc.Accept(writer);
        std::string response_body = buffer.GetString();
        SPDLOG_TRACE("Response body: {}", response_body);
        res.set_content(response_body, "application/json");
        res.status = success ? 200 : 500;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in HandleAddStrategy: {}", e.what());
        doc.SetObject();
        doc.AddMember("success", false, doc.GetAllocator());
        rapidjson::Value error_msg;
        error_msg.SetString(e.what(), doc.GetAllocator());
        doc.AddMember("error", error_msg, doc.GetAllocator());
        doc.Accept(writer);
        res.set_content(buffer.GetString(), "application/json");
        res.status = 500;
    }
}

void HttpGateway::HandleDeleteStrategy(const httplib::Request& req, httplib::Response& res) {
    SPDLOG_INFO("Received delete strategy request");

    rapidjson::Document doc;
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    try {
        // 从URL路径参数获取策略ID
        std::string strat_id = req.matches[1];
        SPDLOG_TRACE("Request strat_id: {}", strat_id);

        // 通过PostSyncMsg调用StrategyEngine
        HttpDeleteStrategyReq http_req{};
        zrt::fill_field(http_req.strat_id, strat_id);
        auto req_buf = std::make_shared<TBuffer>(http_req);
        BufPtr rsp_buf {};
        m_strategy_engine->PostSyncMsg(kHttpDeleteStrategy, req_buf, rsp_buf);
        const auto& rsp = *reinterpret_cast<const HttpStrategyOperationRsp*>(rsp_buf->Data());
        bool success = rsp.success;
        SPDLOG_TRACE("Received response from StrategyEngine: success={}", success);

        doc.SetObject();
        doc.AddMember("success", success, doc.GetAllocator());
        if (success) {
            rapidjson::Value msg;
            msg.SetString("Strategy deleted successfully", doc.GetAllocator());
            doc.AddMember("message", msg, doc.GetAllocator());
        } else {
            rapidjson::Value msg;
            msg.SetString("Failed to delete strategy. Strategy may not exist.", doc.GetAllocator());
            doc.AddMember("error", msg, doc.GetAllocator());
        }

        doc.Accept(writer);
        std::string response_body = buffer.GetString();
        SPDLOG_TRACE("Response body: {}", response_body);
        res.set_content(response_body, "application/json");
        res.status = success ? 200 : 404;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in HandleDeleteStrategy: {}", e.what());
        doc.SetObject();
        doc.AddMember("success", false, doc.GetAllocator());
        rapidjson::Value error_msg;
        error_msg.SetString(e.what(), doc.GetAllocator());
        doc.AddMember("error", error_msg, doc.GetAllocator());
        doc.Accept(writer);
        res.set_content(buffer.GetString(), "application/json");
        res.status = 500;
    }
}

void HttpGateway::HandleRestartStrategy(const httplib::Request& req, httplib::Response& res) {
    SPDLOG_INFO("Received restart strategy request");

    rapidjson::Document doc;
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    try {
        // 从URL路径参数获取策略ID
        std::string strat_id = req.matches[1];
        SPDLOG_TRACE("Request strat_id: {}", strat_id);

        // 通过PostSyncMsg调用StrategyEngine
        HttpRestartStrategyReq http_req{};
        zrt::fill_field(http_req.strat_id, strat_id);
        auto req_buf = std::make_shared<TBuffer>(http_req);
        BufPtr rsp_buf {};
        m_strategy_engine->PostSyncMsg(kHttpRestartStrategy, req_buf, rsp_buf);
        const auto& rsp = *reinterpret_cast<const HttpStrategyOperationRsp*>(rsp_buf->Data());
        bool success = rsp.success;
        SPDLOG_TRACE("Received response from StrategyEngine: success={}", success);

        doc.SetObject();
        doc.AddMember("success", success, doc.GetAllocator());
        if (success) {
            rapidjson::Value msg;
            msg.SetString("Strategy restarted successfully", doc.GetAllocator());
            doc.AddMember("message", msg, doc.GetAllocator());
        } else {
            rapidjson::Value msg;
            msg.SetString("Failed to restart strategy. Strategy may not exist.", doc.GetAllocator());
            doc.AddMember("error", msg, doc.GetAllocator());
        }

        doc.Accept(writer);
        std::string response_body = buffer.GetString();
        SPDLOG_TRACE("Response body: {}", response_body);
        res.set_content(response_body, "application/json");
        res.status = success ? 200 : 404;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in HandleRestartStrategy: {}", e.what());
        doc.SetObject();
        doc.AddMember("success", false, doc.GetAllocator());
        rapidjson::Value error_msg;
        error_msg.SetString(e.what(), doc.GetAllocator());
        doc.AddMember("error", error_msg, doc.GetAllocator());
        doc.Accept(writer);
        res.set_content(buffer.GetString(), "application/json");
        res.status = 500;
    }
}

void HttpGateway::HandleStartStrategy(const httplib::Request& req, httplib::Response& res) {
    SPDLOG_INFO("Received start strategy request");

    rapidjson::Document doc;
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    try {
        std::string strat_id = req.matches[1];
        SPDLOG_TRACE("Request strat_id: {}", strat_id);

        // 通过PostSyncMsg调用StrategyEngine
        HttpStartStrategyReq http_req{};
        zrt::fill_field(http_req.strat_id, strat_id);
        auto req_buf = std::make_shared<TBuffer>(http_req);
        BufPtr rsp_buf {};
        m_strategy_engine->PostSyncMsg(kHttpStartStrategy, req_buf, rsp_buf);
        const auto& rsp = *reinterpret_cast<const HttpStrategyOperationRsp*>(rsp_buf->Data());
        bool success = rsp.success;
        SPDLOG_TRACE("Received response from StrategyEngine: success={}", success);

        doc.SetObject();
        doc.AddMember("success", success, doc.GetAllocator());
        if (success) {
            rapidjson::Value msg;
            msg.SetString("Strategy started successfully", doc.GetAllocator());
            doc.AddMember("message", msg, doc.GetAllocator());
        } else {
            rapidjson::Value msg;
            msg.SetString("Failed to start strategy. Strategy may not exist.", doc.GetAllocator());
            doc.AddMember("error", msg, doc.GetAllocator());
        }

        doc.Accept(writer);
        std::string response_body = buffer.GetString();
        SPDLOG_TRACE("Response body: {}", response_body);
        res.set_content(response_body, "application/json");
        res.status = success ? 200 : 404;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in HandleStartStrategy: {}", e.what());
        doc.SetObject();
        doc.AddMember("success", false, doc.GetAllocator());
        rapidjson::Value error_msg;
        error_msg.SetString(e.what(), doc.GetAllocator());
        doc.AddMember("error", error_msg, doc.GetAllocator());
        doc.Accept(writer);
        res.set_content(buffer.GetString(), "application/json");
        res.status = 500;
    }
}

void HttpGateway::HandleStopStrategy(const httplib::Request& req, httplib::Response& res) {
    SPDLOG_INFO("Received stop strategy request");

    rapidjson::Document doc;
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    try {
        std::string strat_id = req.matches[1];
        SPDLOG_TRACE("Request strat_id: {}", strat_id);

        // 通过PostSyncMsg调用StrategyEngine
        HttpStopStrategyReq http_req{};
        zrt::fill_field(http_req.strat_id, strat_id);
        auto req_buf = std::make_shared<TBuffer>(http_req);
        BufPtr rsp_buf {};
        m_strategy_engine->PostSyncMsg(kHttpStopStrategy, req_buf, rsp_buf);
        const auto& rsp = *reinterpret_cast<const HttpStrategyOperationRsp*>(rsp_buf->Data());
        bool success = rsp.success;
        SPDLOG_TRACE("Received response from StrategyEngine: success={}", success);

        doc.SetObject();
        doc.AddMember("success", success, doc.GetAllocator());
        if (success) {
            rapidjson::Value msg;
            msg.SetString("Strategy stopped successfully", doc.GetAllocator());
            doc.AddMember("message", msg, doc.GetAllocator());
        } else {
            rapidjson::Value msg;
            msg.SetString("Failed to stop strategy. Strategy may not exist.", doc.GetAllocator());
            doc.AddMember("error", msg, doc.GetAllocator());
        }

        doc.Accept(writer);
        std::string response_body = buffer.GetString();
        SPDLOG_TRACE("Response body: {}", response_body);
        res.set_content(response_body, "application/json");
        res.status = success ? 200 : 404;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in HandleStopStrategy: {}", e.what());
        doc.SetObject();
        doc.AddMember("success", false, doc.GetAllocator());
        rapidjson::Value error_msg;
        error_msg.SetString(e.what(), doc.GetAllocator());
        doc.AddMember("error", error_msg, doc.GetAllocator());
        doc.Accept(writer);
        res.set_content(buffer.GetString(), "application/json");
        res.status = 500;
    }
}

void HttpGateway::HandleBatchOperation(const httplib::Request& req, httplib::Response& res) {
    SPDLOG_INFO("Received batch operation request");
    SPDLOG_TRACE("Request body: {}", req.body);

    rapidjson::Document doc;
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    try {
        // 解析请求JSON
        rapidjson::Document req_doc;
        req_doc.Parse(req.body.c_str());

        if (!req_doc.HasMember("operation") || !req_doc["operation"].IsString()) {
            doc.SetObject();
            doc.AddMember("success", false, doc.GetAllocator());
            doc.AddMember("error", "Missing or invalid operation parameter", doc.GetAllocator());
            doc.Accept(writer);
            res.set_content(buffer.GetString(), "application/json");
            res.status = 400;
            return;
        }

        if (!req_doc.HasMember("strategy_ids") || !req_doc["strategy_ids"].IsArray()) {
            doc.SetObject();
            doc.AddMember("success", false, doc.GetAllocator());
            doc.AddMember("error", "Missing or invalid strategy_ids parameter", doc.GetAllocator());
            doc.Accept(writer);
            res.set_content(buffer.GetString(), "application/json");
            res.status = 400;
            return;
        }

        std::string operation = req_doc["operation"].GetString();
        const auto& strategy_ids = req_doc["strategy_ids"];
        SPDLOG_TRACE("Batch operation: {}, strategy count: {}", operation, strategy_ids.Size());

        int success_count = 0;
        int fail_count = 0;
        rapidjson::Value results(rapidjson::kArrayType);

        for (rapidjson::SizeType i = 0; i < strategy_ids.Size(); i++) {
            if (!strategy_ids[i].IsString()) {
                continue;
            }

            std::string strat_id = strategy_ids[i].GetString();
            SPDLOG_TRACE("Processing strategy [{}]: {}", i, strat_id);
            bool success = false;

            // 通过PostSyncMsg调用StrategyEngine
            if (operation == "start") {
                HttpStartStrategyReq http_req{};
                zrt::fill_field(http_req.strat_id, strat_id);
                auto req_buf = std::make_shared<TBuffer>(http_req);
                BufPtr rsp_buf;
                m_strategy_engine->PostSyncMsg(kHttpStartStrategy, req_buf, rsp_buf);
                const auto& rsp = *reinterpret_cast<const HttpStrategyOperationRsp*>(rsp_buf->Data());
                success = rsp.success;
            } else if (operation == "stop") {
                HttpStopStrategyReq http_req{};
                zrt::fill_field(http_req.strat_id, strat_id);
                auto req_buf = std::make_shared<TBuffer>(http_req);
                BufPtr rsp_buf;
                m_strategy_engine->PostSyncMsg(kHttpStopStrategy, req_buf, rsp_buf);
                const auto& rsp = *reinterpret_cast<const HttpStrategyOperationRsp*>(rsp_buf->Data());
                success = rsp.success;
            } else if (operation == "restart") {
                HttpRestartStrategyReq http_req{};
                zrt::fill_field(http_req.strat_id, strat_id);
                auto req_buf = std::make_shared<TBuffer>(http_req);
                BufPtr rsp_buf;
                m_strategy_engine->PostSyncMsg(kHttpRestartStrategy, req_buf, rsp_buf);
                const auto& rsp = *reinterpret_cast<const HttpStrategyOperationRsp*>(rsp_buf->Data());
                success = rsp.success;
            } else {
                doc.SetObject();
                doc.AddMember("success", false, doc.GetAllocator());
                doc.AddMember("error", "Invalid operation. Must be 'start', 'stop', or 'restart'", doc.GetAllocator());
                doc.Accept(writer);
                res.set_content(buffer.GetString(), "application/json");
                res.status = 400;
                return;
            }

            SPDLOG_TRACE("Strategy [{}] {} result: {}", i, strat_id, success ? "success" : "failed");

            if (success) {
                success_count++;
            } else {
                fail_count++;
            }

            rapidjson::Value result_obj(rapidjson::kObjectType);
            rapidjson::Value id_val;
            id_val.SetString(strat_id.c_str(), doc.GetAllocator());
            result_obj.AddMember("strategy_id", id_val, doc.GetAllocator());
            result_obj.AddMember("success", success, doc.GetAllocator());
            results.PushBack(result_obj, doc.GetAllocator());
        }

        doc.SetObject();
        doc.AddMember("success", true, doc.GetAllocator());
        doc.AddMember("total", static_cast<int>(strategy_ids.Size()), doc.GetAllocator());
        doc.AddMember("success_count", success_count, doc.GetAllocator());
        doc.AddMember("fail_count", fail_count, doc.GetAllocator());
        doc.AddMember("results", results, doc.GetAllocator());

        doc.Accept(writer);
        std::string response_body = buffer.GetString();
        SPDLOG_TRACE("Batch operation completed: success={}, fail={}", success_count, fail_count);
        SPDLOG_TRACE("Response body: {}", response_body);
        res.set_content(response_body, "application/json");
        res.status = 200;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in HandleBatchOperation: {}", e.what());
        doc.SetObject();
        doc.AddMember("success", false, doc.GetAllocator());
        rapidjson::Value error_msg;
        error_msg.SetString(e.what(), doc.GetAllocator());
        doc.AddMember("error", error_msg, doc.GetAllocator());
        doc.Accept(writer);
        res.set_content(buffer.GetString(), "application/json");
        res.status = 500;
    }
}

void HttpGateway::HandleSnapshot(const httplib::Request& req, httplib::Response& res) {
    SPDLOG_INFO("Received save snapshot request");

    try {
        // 发送空请求到 StrategyEngine
        auto req_buf = std::make_shared<TBuffer>();
        BufPtr rsp_buf {};
        m_strategy_engine->PostSyncMsg(kHttpSaveSnapshot, req_buf, rsp_buf);

        const auto& rsp = *reinterpret_cast<const HttpSaveSnapshotRsp*>(rsp_buf->Data());

        JsonObj json;
        json.AddMember("success", rsp.success);

        if (rsp.success) {
            json.AddMember("snapshot_path", std::string(rsp.snapshot_path));
            json.AddMember("message", std::string("Snapshot saved successfully"));
        } else {
            json.AddMember("error", std::string(rsp.error_msg));
        }

        res.set_content(static_cast<std::string>(json), "application/json");
        res.status = rsp.success ? 200 : 500;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in HandleSnapshot: {}", e.what());
        JsonObj json;
        json.AddMember("success", false);
        json.AddMember("error", std::string(e.what()));
        res.set_content(static_cast<std::string>(json), "application/json");
        res.status = 500;
    }
}

void HttpGateway::HandleWalStats(const httplib::Request& req, httplib::Response& res) {
    SPDLOG_INFO("Received get WAL stats request");

    try {
        // 发送空请求到 StrategyEngine
        auto req_buf = std::make_shared<TBuffer>();
        BufPtr rsp_buf {};
        m_strategy_engine->PostSyncMsg(kHttpGetWalStats, req_buf, rsp_buf);

        const auto& rsp = *reinterpret_cast<const HttpWalStatsRsp*>(rsp_buf->Data());

        JsonObj json;
        json.AddMember("success", rsp.success);

        if (rsp.success) {
            // 共享内存 WAL 统计
            JsonObj shm_obj = json.AddObject("shm_wal");
            shm_obj.AddMember("write_pos", rsp.shm_write_pos);
            shm_obj.AddMember("confirmed_pos", rsp.shm_confirmed_pos);
            shm_obj.AddMember("unconfirmed_bytes", rsp.shm_unconfirmed_bytes);

            // 文件 WAL 统计
            JsonObj file_obj = json.AddObject("file_wal");
            file_obj.AddMember("current_seq", rsp.file_current_seq);
            file_obj.AddMember("total_size_bytes", rsp.file_total_size_bytes);
            file_obj.AddMember("size_mb", rsp.wal_file_size_mb);

            // 快照信息
            JsonObj snapshot_obj = json.AddObject("snapshot");
            snapshot_obj.AddMember("last_seq", rsp.last_snapshot_seq);
            snapshot_obj.AddMember("need_snapshot", rsp.need_snapshot);
        } else {
            json.AddMember("error", std::string("WAL is not enabled"));
        }

        res.set_content(static_cast<std::string>(json), "application/json");
        res.status = rsp.success ? 200 : 500;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in HandleWalStats: {}", e.what());
        JsonObj json;
        json.AddMember("success", false);
        json.AddMember("error", std::string(e.what()));
        res.set_content(static_cast<std::string>(json), "application/json");
        res.status = 500;
    }
}
