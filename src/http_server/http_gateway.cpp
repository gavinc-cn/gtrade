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
#include "db_struct_json.h"
#include "my_utc.h"
#include <map>
#include <tuple>
#include <utility>

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

#ifdef GTRADE_ENABLE_HTTP_TRADE
    // POST /api/trade/place_order - 下单（仅在 GTRADE_ENABLE_HTTP_TRADE 编译时开放）
    m_server->Post("/api/trade/place_order", [this](const httplib::Request& req, httplib::Response& res) {
        HandlePlaceOrder(req, res);
    });

    // POST /api/trade/cancel_order - 撤单（仅在 GTRADE_ENABLE_HTTP_TRADE 编译时开放）
    m_server->Post("/api/trade/cancel_order", [this](const httplib::Request& req, httplib::Response& res) {
        HandleCancelOrder(req, res);
    });
#endif  // GTRADE_ENABLE_HTTP_TRADE

    // GET /api/trade/depth - 最新行情快照（始终开放，只读无风险）
    m_server->Get("/api/trade/depth", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetDepth(req, res);
    });

    // 标的范围订阅（web 设置页）
    m_server->Get("/api/instrument/list", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetInstrumentList(req, res);
    });
    m_server->Get("/api/instrument/scope", [this](const httplib::Request& req, httplib::Response& res) {
        HandleGetInstrumentScope(req, res);
    });
    m_server->Post("/api/instrument/scope", [this](const httplib::Request& req, httplib::Response& res) {
        HandleSetInstrumentScope(req, res);
    });

    // 补查（web_server 断线重连后按游标补齐；始终开放，只读无风险）
    m_server->Get("/api/query/orders", [this](const httplib::Request& req, httplib::Response& res) {
        HandleQueryOrders(req, res);
    });
    m_server->Get("/api/query/trades", [this](const httplib::Request& req, httplib::Response& res) {
        HandleQueryTrades(req, res);
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
    // 仅监听本机 loopback：本服务无鉴权且 CORS 全开，不暴露到局域网/宿主；外部访问走 SSH/IDE 端口转发
    m_server->listen("127.0.0.1", m_gtrade_cfg.http_server_port);
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

#ifdef GTRADE_ENABLE_HTTP_TRADE

void HttpGateway::HandlePlaceOrder(const httplib::Request& req, httplib::Response& res) {
    // 解析 JSON 请求，将字符串参数映射为内部枚举，构造 HttpPlaceOrderReq 发往引擎线程
    SPDLOG_INFO("Received place order request");
    SPDLOG_TRACE("Request body: {}", req.body);

    try {
        rapidjson::Document req_doc;
        req_doc.Parse(req.body.c_str());

        // 必填参数检查
        for (const char* field : {"account_id", "inst_id", "td_mode", "side", "ord_type", "sz"}) {
            if (!req_doc.HasMember(field) || !req_doc[field].IsString()) {
                JsonObj json;
                json.AddMember("success", false);
                json.AddMember("error", std::string("Missing or invalid field: ") + field);
                res.set_content(static_cast<std::string>(json), "application/json");
                res.status = 400;
                return;
            }
        }

        // 字符串到内部 char 枚举的映射
        const auto parse_side = [](const std::string& s) -> char {
            if (s == "buy")  return TradeSide::Buy;
            if (s == "sell") return TradeSide::Sell;
            return '\0';
        };
        const auto parse_ord_type = [](const std::string& s) -> char {
            if (s == "limit")     return PriceType::Limit;
            if (s == "market")    return PriceType::Market;
            if (s == "post_only") return PriceType::MakerOnly;
            if (s == "fok")       return PriceType::Fok;
            if (s == "ioc")       return PriceType::Fak;
            return '\0';
        };
        const auto parse_td_mode = [](const std::string& s) -> char {
            if (s == "cash")     return TradeMode::Cash;
            if (s == "cross")    return TradeMode::Cross;
            if (s == "isolated") return TradeMode::Isolated;
            return '\0';
        };

        const std::string side_str     = req_doc["side"].GetString();
        const std::string ord_type_str = req_doc["ord_type"].GetString();
        const std::string td_mode_str  = req_doc["td_mode"].GetString();

        const char side     = parse_side(side_str);
        const char ord_type = parse_ord_type(ord_type_str);
        const char td_mode  = parse_td_mode(td_mode_str);

        if (!side || !ord_type || !td_mode) {
            JsonObj json;
            json.AddMember("success", false);
            json.AddMember("error", std::string("invalid side/ord_type/td_mode value"));
            res.set_content(static_cast<std::string>(json), "application/json");
            res.status = 400;
            return;
        }

        const std::string account_id = req_doc["account_id"].GetString();
        const std::string inst_id    = req_doc["inst_id"].GetString();
        const std::string sz_str     = req_doc["sz"].GetString();
        const std::string market_str = req_doc.HasMember("market") && req_doc["market"].IsString()
                                        ? req_doc["market"].GetString() : std::string("okx");
        // 组合为可选字段：不传/非字符串时置空，引擎侧回落为 policy_no
        const std::string portfolio_str = req_doc.HasMember("portfolio") && req_doc["portfolio"].IsString()
                                        ? req_doc["portfolio"].GetString() : std::string();
        const double sz = std::stod(sz_str);
        const double px = [&]() -> double {
            if (req_doc.HasMember("px") && req_doc["px"].IsString()) {
                const std::string s = req_doc["px"].GetString();
                if (!s.empty()) return std::stod(s);
            }
            return 0.0;
        }();

        HttpPlaceOrderReq http_req{};
        zrt::fill_field(http_req.account_id, account_id);
        zrt::fill_field(http_req.market,     market_str);
        zrt::fill_field(http_req.inst_id,    inst_id);
        zrt::fill_field(http_req.portfolio,  portfolio_str);
        http_req.td_mode  = td_mode;
        http_req.side     = side;
        http_req.ord_type = ord_type;
        http_req.px       = px;
        http_req.sz       = sz;
        http_req.ent_time = MyUTC().Epoch19();

        auto req_buf = std::make_shared<TBuffer>(http_req);
        BufPtr rsp_buf{};
        m_strategy_engine->PostSyncMsg(kHttpPlaceOrder, req_buf, rsp_buf);
        const auto& rsp = *reinterpret_cast<const HttpPlaceOrderRsp*>(rsp_buf->Data());

        JsonObj json;
        json.AddMember("success", rsp.success);
        if (rsp.success) {
            json.AddMember("order_id", rsp.order_id);
        } else {
            json.AddMember("error", std::string(rsp.error_msg));
        }
        res.set_content(static_cast<std::string>(json), "application/json");
        res.status = rsp.success ? 200 : 500;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in HandlePlaceOrder: {}", e.what());
        JsonObj json;
        json.AddMember("success", false);
        json.AddMember("error", std::string(e.what()));
        res.set_content(static_cast<std::string>(json), "application/json");
        res.status = 500;
    }
}

void HttpGateway::HandleCancelOrder(const httplib::Request& req, httplib::Response& res) {
    // 解析 JSON 请求，向引擎线程发送撤单消息
    SPDLOG_INFO("Received cancel order request");
    SPDLOG_TRACE("Request body: {}", req.body);

    try {
        rapidjson::Document req_doc;
        req_doc.Parse(req.body.c_str());

        if (!req_doc.HasMember("account_id") || !req_doc["account_id"].IsString() ||
            !req_doc.HasMember("order_id") || !req_doc["order_id"].IsInt64()) {
            JsonObj json;
            json.AddMember("success", false);
            json.AddMember("error", std::string("Missing or invalid account_id/order_id"));
            res.set_content(static_cast<std::string>(json), "application/json");
            res.status = 400;
            return;
        }

        const std::string account_id = req_doc["account_id"].GetString();
        const int64_t order_id       = req_doc["order_id"].GetInt64();

        HttpCancelOrderReq http_req{};
        zrt::fill_field(http_req.account_id, account_id);
        http_req.order_id = order_id;

        auto req_buf = std::make_shared<TBuffer>(http_req);
        BufPtr rsp_buf{};
        m_strategy_engine->PostSyncMsg(kHttpCancelOrder, req_buf, rsp_buf);
        const auto& rsp = *reinterpret_cast<const HttpCancelOrderRsp*>(rsp_buf->Data());

        JsonObj json;
        json.AddMember("success", rsp.success);
        if (!rsp.success) {
            json.AddMember("error", std::string(rsp.error_msg));
        }
        res.set_content(static_cast<std::string>(json), "application/json");
        res.status = rsp.success ? 200 : 500;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in HandleCancelOrder: {}", e.what());
        JsonObj json;
        json.AddMember("success", false);
        json.AddMember("error", std::string(e.what()));
        res.set_content(static_cast<std::string>(json), "application/json");
        res.status = 500;
    }
}

#endif  // GTRADE_ENABLE_HTTP_TRADE

void HttpGateway::HandleGetDepth(const httplib::Request& req, httplib::Response& res) {
    // 读取 query 参数 inst_id/market，从引擎缓存取最新 Depth，构造买卖盘 JSON
    SPDLOG_INFO("Received get depth request");

    try {
        if (!req.has_param("inst_id")) {
            JsonObj json;
            json.AddMember("success", false);
            json.AddMember("error", std::string("Missing inst_id param"));
            res.set_content(static_cast<std::string>(json), "application/json");
            res.status = 400;
            return;
        }

        const std::string inst_id = req.get_param_value("inst_id");
        const std::string market  = req.has_param("market")
                                     ? req.get_param_value("market") : std::string("okx");

        HttpGetDepthReq http_req{};
        zrt::fill_field(http_req.inst_id, inst_id);
        zrt::fill_field(http_req.market,  market);

        auto req_buf = std::make_shared<TBuffer>(http_req);
        BufPtr rsp_buf{};
        m_strategy_engine->PostSyncMsg(kHttpGetDepth, req_buf, rsp_buf);
        const auto& rsp = *reinterpret_cast<const HttpGetDepthRsp*>(rsp_buf->Data());

        JsonObj json;
        json.AddMember("success", rsp.success);

        if (rsp.success) {
            json.AddMember("inst_id",   std::string(rsp.inst_id));
            json.AddMember("market",    std::string(rsp.market));
            json.AddMember("timestamp", rsp.timestamp);

            // asks: [[price, amount], ...]
            JsonArray asks = json.AddArray("asks");
            for (int i = 0; i < rsp.ask_cnt; ++i) {
                JsonArray pair = asks.PushBackArray();
                pair.PushBack(rsp.ask_price[i]);
                pair.PushBack(rsp.ask_amount[i]);
            }

            // bids: [[price, amount], ...]
            JsonArray bids = json.AddArray("bids");
            for (int i = 0; i < rsp.bid_cnt; ++i) {
                JsonArray pair = bids.PushBackArray();
                pair.PushBack(rsp.bid_price[i]);
                pair.PushBack(rsp.bid_amount[i]);
            }
        } else {
            json.AddMember("error", std::string(rsp.error_msg));
        }

        res.set_content(static_cast<std::string>(json), "application/json");
        res.status = rsp.success ? 200 : 404;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in HandleGetDepth: {}", e.what());
        JsonObj json;
        json.AddMember("success", false);
        json.AddMember("error", std::string(e.what()));
        res.set_content(static_cast<std::string>(json), "application/json");
        res.status = 500;
    }
}

namespace {

// 解析逗号分隔的委托号列表（浏览器按字符串传，避免 JS 精度丢失）
size_t ParseEntnos(const std::string& raw, int64_t* out, size_t max_count) {
    size_t count = 0;
    size_t begin = 0;
    while (begin <= raw.size() && count < max_count) {
        const size_t end = raw.find(',', begin);
        const std::string token = raw.substr(begin, (end == std::string::npos) ? std::string::npos : end - begin);
        if (!token.empty()) {
            out[count++] = std::atoll(token.c_str());
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return count;
}

}  // namespace

void HttpGateway::HandleGetInstrumentList(const httplib::Request&, httplib::Response& res) {
    // 请求引擎线程取全量标的，按 InstrumentInfoItem 序列展开为 data 数组
    try {
        BufPtr rsp_buf {};
        m_strategy_engine->PostSyncMsg(kHttpQueryInstruments, std::make_shared<TBuffer>(), rsp_buf);
        JsonObj json;
        json.AddMember("success", true);
        JsonArray arr = json.AddArray("data");
        int count = 0;
        if (rsp_buf) {
            count = rsp_buf->ForEach<InstrumentInfoItem>([&arr](const InstrumentInfoItem& item) {
                JsonObj obj = arr.PushBackObject();
                obj.AddMember("market", std::string(item.market));
                obj.AddMember("inst_id", std::string(item.inst_id));
                obj.AddMember("inst_type", std::string(item.inst_type));
            });
        }
        json.AddMember("count", count);
        res.set_content(static_cast<std::string>(json), "application/json");

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in HandleGetInstrumentList: {}", e.what());
        JsonObj json;
        json.AddMember("success", false);
        json.AddMember("error", std::string(e.what()));
        res.set_content(static_cast<std::string>(json), "application/json");
        res.status = 500;
    }
}

void HttpGateway::HandleGetInstrumentScope(const httplib::Request&, httplib::Response& res) {
    // 请求引擎线程取订阅现状，按 (market, inst_id, inst_type) 聚合 owner 后拆分 scope/owned 两组返回
    try {
        BufPtr rsp_buf {};
        m_strategy_engine->PostSyncMsg(kHttpGetInstrumentScope, std::make_shared<TBuffer>(), rsp_buf);
        // (market|inst_id|inst_type) → owners 聚合；用 tuple 做 std::map 键（字典序稳定）
        std::map<std::tuple<std::string, std::string, std::string>, std::vector<std::string>> owners_map;
        if (rsp_buf) {
            rsp_buf->ForEach<ScopeOwnerItem>([&owners_map](const ScopeOwnerItem& item) {
                owners_map[{std::string(item.market), std::string(item.inst_id),
                            std::string(item.inst_type)}].emplace_back(item.owner);
            });
        }
        JsonObj json;
        json.AddMember("success", true);
        JsonArray scope_arr = json.AddArray("scope");
        JsonArray owned_arr = json.AddArray("owned");
        for (const auto& [key, owners] : owners_map) {
            const auto& [market, inst_id, inst_type] = key;
            bool in_scope = false;
            std::vector<std::string> strategies {};
            for (const auto& owner : owners) {
                if (owner == k_scope_owner) { in_scope = true; } else { strategies.push_back(owner); }
            }
            if (in_scope) {
                JsonObj obj = scope_arr.PushBackObject();
                obj.AddMember("market", market);
                obj.AddMember("inst_id", inst_id);
                obj.AddMember("inst_type", inst_type);
            }
            JsonObj obj = owned_arr.PushBackObject();
            obj.AddMember("market", market);
            obj.AddMember("inst_id", inst_id);
            obj.AddMember("inst_type", inst_type);
            obj.AddMember("scope", in_scope);
            JsonArray strategies_arr = obj.AddArray("strategies");
            for (const auto& sid : strategies) { strategies_arr.PushBack(sid); }
        }
        res.set_content(static_cast<std::string>(json), "application/json");

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in HandleGetInstrumentScope: {}", e.what());
        JsonObj json;
        json.AddMember("success", false);
        json.AddMember("error", std::string(e.what()));
        res.set_content(static_cast<std::string>(json), "application/json");
        res.status = 500;
    }
}

void HttpGateway::HandleSetInstrumentScope(const httplib::Request& req, httplib::Response& res) {
    // 解析 {"scope":[{market,inst_id,inst_type},...]}，转发引擎线程应用订阅范围
    try {
        rapidjson::Document doc {};
        doc.Parse(req.body.c_str());
        if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("scope") || !doc["scope"].IsArray()) {
            res.status = 400;
            JsonObj json;
            json.AddMember("success", false);
            json.AddMember("error", std::string("body must be {\"scope\":[{market,inst_id,inst_type},...]}"));
            res.set_content(static_cast<std::string>(json), "application/json");
            return;
        }
        HttpSetInstrumentScopeReq engine_req {};
        for (const auto& item : doc["scope"].GetArray()) {
            if (engine_req.count >= kMaxScopeItems) {
                res.status = 400;
                JsonObj json;
                json.AddMember("success", false);
                json.AddMember("error", std::string("标的数量超过上限 128"));
                res.set_content(static_cast<std::string>(json), "application/json");
                return;
            }
            if (!item.IsObject() || !item.HasMember("market") || !item["market"].IsString()
                || !item.HasMember("inst_id") || !item["inst_id"].IsString()) {
                res.status = 400;
                JsonObj json;
                json.AddMember("success", false);
                json.AddMember("error", std::string("market/inst_id 必须为字符串"));
                res.set_content(static_cast<std::string>(json), "application/json");
                return;
            }
            // inst_type 可选：缺失/非字符串时留空串，由引擎按行情缓存归一化推断；
            // 推断不出（未知 instId 或同 instId 多类型）时计入 unknown_cnt 并跳过。
            const std::string inst_type = (item.HasMember("inst_type") && item["inst_type"].IsString())
                                          ? std::string(item["inst_type"].GetString()) : std::string {};
            zrt::fill_field(engine_req.items[engine_req.count].market, std::string(item["market"].GetString()));
            zrt::fill_field(engine_req.items[engine_req.count].inst_id, std::string(item["inst_id"].GetString()));
            zrt::fill_field(engine_req.items[engine_req.count].inst_type, inst_type);
            ++engine_req.count;
        }
        BufPtr rsp_buf {};
        m_strategy_engine->PostSyncMsg(kHttpSetInstrumentScope, std::make_shared<TBuffer>(engine_req), rsp_buf);
        if (!rsp_buf) {
            // 框架层保证响应非空，此处仅作防御
            res.status = 500;
            JsonObj json;
            json.AddMember("success", false);
            json.AddMember("error", std::string("engine unavailable"));
            res.set_content(static_cast<std::string>(json), "application/json");
            return;
        }
        const auto& rsp = rsp_buf->RefData<HttpSetInstrumentScopeRsp>();
        JsonObj json;
        json.AddMember("success", rsp.success);
        json.AddMember("applied_cnt", rsp.applied_cnt);
        json.AddMember("removed_cnt", rsp.removed_cnt);
        json.AddMember("kept_cnt", rsp.kept_cnt);
        json.AddMember("unknown_cnt", rsp.unknown_cnt);
        if (!rsp.success) { json.AddMember("error", std::string(rsp.error_msg)); }
        res.status = rsp.success ? 200 : 500;
        res.set_content(static_cast<std::string>(json), "application/json");

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in HandleSetInstrumentScope: {}", e.what());
        JsonObj json;
        json.AddMember("success", false);
        json.AddMember("error", std::string(e.what()));
        res.set_content(static_cast<std::string>(json), "application/json");
        res.status = 500;
    }
}

void HttpGateway::HandleQueryOrders(const httplib::Request& req, httplib::Response& res) {
    // 补查委托（只读，读引擎内存权威态）：`?entnos=1,2,3`（已知在途，含终态）或
    // `?entno_gt=<游标>`（发现离线期间新增），`?limit=N`（默认 200，上限 500）。
    try {
        HttpQueryOrdersReq query_req {};
        if (req.has_param("limit")) {
            query_req.limit = std::atoi(req.get_param_value("limit").c_str());
        }
        if (req.has_param("entnos")) {
            query_req.entno_cnt = static_cast<int>(ParseEntnos(req.get_param_value("entnos"),
                                                                query_req.entnos, kQueryMaxEntnos));
        } else if (req.has_param("entno_gt")) {
            query_req.cursor_entno = std::atoll(req.get_param_value("entno_gt").c_str());
        }

        BufPtr rsp_buf {};
        m_strategy_engine->PostSyncMsg(kHttpQueryOrdersReq, std::make_shared<TBuffer>(query_req), rsp_buf);

        JsonObj json;
        json.AddMember("success", true);
        JsonArray arr = json.AddArray("orders");
        int count = 0;
        if (rsp_buf) {
            count = rsp_buf->ForEach<Order>([&arr](const Order& order) {
                JsonObj obj = arr.PushBackObject();
                zrt::ToJson(obj, order);
            });
        }
        const int eff_limit = (query_req.limit > 0) ? std::min(query_req.limit, kQueryMaxRows)
                                                    : kQueryDefaultRows;
        json.AddMember("count", count);
        // 满 limit 即当"可能还有"：游标模式客户端用最后一条 entno 继续查（下次返回 0 即到底）
        json.AddMember("has_more", query_req.entno_cnt == 0 && count >= eff_limit);
        res.set_content(static_cast<std::string>(json), "application/json");

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in HandleQueryOrders: {}", e.what());
        JsonObj json;
        json.AddMember("success", false);
        json.AddMember("error", std::string(e.what()));
        res.set_content(static_cast<std::string>(json), "application/json");
        res.status = 500;
    }
}

void HttpGateway::HandleQueryTrades(const httplib::Request& req, httplib::Response& res) {
    // 补查成交（只读）：`?tdno_gt=<游标>`，`?limit=N`（默认 200，上限 500）
    try {
        HttpQueryTradesReq query_req {};
        if (req.has_param("limit")) {
            query_req.limit = std::atoi(req.get_param_value("limit").c_str());
        }
        if (req.has_param("tdno_gt")) {
            query_req.cursor_tdno = std::atoll(req.get_param_value("tdno_gt").c_str());
        }

        BufPtr rsp_buf {};
        m_strategy_engine->PostSyncMsg(kHttpQueryTradesReq, std::make_shared<TBuffer>(query_req), rsp_buf);

        JsonObj json;
        json.AddMember("success", true);
        JsonArray arr = json.AddArray("trades");
        int count = 0;
        if (rsp_buf) {
            count = rsp_buf->ForEach<Trade>([&arr](const Trade& trade) {
                JsonObj obj = arr.PushBackObject();
                zrt::ToJson(obj, trade);
            });
        }
        const int eff_limit = (query_req.limit > 0) ? std::min(query_req.limit, kQueryMaxRows)
                                                    : kQueryDefaultRows;
        json.AddMember("count", count);
        json.AddMember("has_more", count >= eff_limit);
        res.set_content(static_cast<std::string>(json), "application/json");

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in HandleQueryTrades: {}", e.what());
        JsonObj json;
        json.AddMember("success", false);
        json.AddMember("error", std::string(e.what()));
        res.set_content(static_cast<std::string>(json), "application/json");
        res.status = 500;
    }
}
