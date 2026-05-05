//
// Created by dell on 2025/11/14.
//

#include "gtrade_cli.h"
#include <iostream>
#include <yaml-cpp/yaml.h>
#include "3rd/httplib.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

// 构造函数
GTradeClient::GTradeClient(const std::string& host, int port)
    : m_host(host), m_port(port) {
    // 如果端口为0，从配置文件加载
    if (m_port == 0) {
        m_port = LoadHttpPortFromConfig();
    }
}

// 从配置文件加载HTTP端口
int GTradeClient::LoadHttpPortFromConfig() {
    try {
        YAML::Node main_yml = YAML::LoadFile("config/config.yml");
        return main_yml["http_server_port"].as<int>();
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << std::endl;
        return 8080; // 默认端口
    }
}

// 添加新策略
int GTradeClient::AddStrategy(const std::string& cfg_path) {
    httplib::Client cli(m_host, m_port);

    rapidjson::Document req_doc;
    req_doc.SetObject();
    rapidjson::Value config_path;
    config_path.SetString(cfg_path.c_str(), req_doc.GetAllocator());
    req_doc.AddMember("config_path", config_path, req_doc.GetAllocator());

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    req_doc.Accept(writer);

    auto res = cli.Post("/api/strategy/add", buffer.GetString(), "application/json");

    if (!res) {
        std::cerr << "Failed to connect to HTTP server. Is gtrade running?" << std::endl;
        return 1;
    }

    std::cout << "Response: " << res->body << std::endl;
    return res->status == 200 ? 0 : 1;
}

// 删除已有策略
int GTradeClient::DeleteStrategy(const std::string& strat_id) {
    httplib::Client cli(m_host, m_port);

    std::string path = "/api/strategy/delete/" + strat_id;
    auto res = cli.Delete(path);

    if (!res) {
        std::cerr << "Failed to connect to HTTP server. Is gtrade running?" << std::endl;
        return 1;
    }

    std::cout << "Response: " << res->body << std::endl;
    return (res->status == 200 || res->status == 404) ? 0 : 1;
}

// 重启已有策略
int GTradeClient::RestartStrategy(const std::string& strat_id) {
    httplib::Client cli(m_host, m_port);

    std::string path = "/api/strategy/restart/" + strat_id;
    auto res = cli.Post(path, "", "application/json");

    if (!res) {
        std::cerr << "Failed to connect to HTTP server. Is gtrade running?" << std::endl;
        return 1;
    }

    std::cout << "Response: " << res->body << std::endl;
    return (res->status == 200 || res->status == 404) ? 0 : 1;
}

// 列出所有策略
int GTradeClient::ListStrategies() {
    httplib::Client cli(m_host, m_port);

    auto res = cli.Get("/api/strategy/list");

    if (!res) {
        std::cerr << "Failed to connect to HTTP server. Is gtrade running?" << std::endl;
        return 1;
    }

    std::cout << "Response: " << res->body << std::endl;
    return res->status == 200 ? 0 : 1;
}

// 手动触发保存快照
int GTradeClient::SaveSnapshot() {
    httplib::Client cli(m_host, m_port);

    auto res = cli.Post("/api/system/snapshot", "", "application/json");

    if (!res) {
        std::cerr << "Failed to connect to HTTP server. Is gtrade running?" << std::endl;
        return 1;
    }

    // 解析响应
    rapidjson::Document doc;
    doc.Parse(res->body.c_str());

    if (doc.HasParseError() || !doc.HasMember("success")) {
        std::cerr << "Invalid response: " << res->body << std::endl;
        return 1;
    }

    if (doc["success"].GetBool()) {
        std::cout << "Snapshot saved successfully!" << std::endl;
        if (doc.HasMember("snapshot_path")) {
            std::cout << "Path: " << doc["snapshot_path"].GetString() << std::endl;
        }
        return 0;
    } else {
        std::cerr << "Failed to save snapshot";
        if (doc.HasMember("error")) {
            std::cerr << ": " << doc["error"].GetString();
        }
        std::cerr << std::endl;
        return 1;
    }
}

// 获取WAL统计信息
int GTradeClient::GetWalStats() {
    httplib::Client cli(m_host, m_port);

    auto res = cli.Get("/api/system/wal-stats");

    if (!res) {
        std::cerr << "Failed to connect to HTTP server. Is gtrade running?" << std::endl;
        return 1;
    }

    // 解析响应并格式化输出
    rapidjson::Document doc;
    doc.Parse(res->body.c_str());

    if (doc.HasParseError() || !doc.HasMember("success")) {
        std::cerr << "Invalid response: " << res->body << std::endl;
        return 1;
    }

    if (!doc["success"].GetBool()) {
        std::cerr << "WAL is not enabled" << std::endl;
        return 1;
    }

    std::cout << "=== WAL Statistics ===" << std::endl;

    // 共享内存 WAL
    if (doc.HasMember("shm_wal")) {
        const auto& shm = doc["shm_wal"];
        std::cout << "\n[Shared Memory WAL]" << std::endl;
        std::cout << "  Write Position:      " << shm["write_pos"].GetUint64() << std::endl;
        std::cout << "  Confirmed Position:  " << shm["confirmed_pos"].GetUint64() << std::endl;
        std::cout << "  Unconfirmed Bytes:   " << shm["unconfirmed_bytes"].GetUint64() << std::endl;
    }

    // 文件 WAL
    if (doc.HasMember("file_wal")) {
        const auto& file = doc["file_wal"];
        std::cout << "\n[File WAL]" << std::endl;
        std::cout << "  Current Sequence:    " << file["current_seq"].GetUint64() << std::endl;
        std::cout << "  Total Size:          " << file["total_size_bytes"].GetUint64() << " bytes" << std::endl;
        std::cout << "  Size (MB):           " << file["size_mb"].GetUint64() << " MB" << std::endl;
    }

    // 快照信息
    if (doc.HasMember("snapshot")) {
        const auto& snapshot = doc["snapshot"];
        std::cout << "\n[Snapshot]" << std::endl;
        std::cout << "  Last Snapshot Seq:   " << snapshot["last_seq"].GetUint64() << std::endl;
        std::cout << "  Need Snapshot:       " << (snapshot["need_snapshot"].GetBool() ? "Yes" : "No") << std::endl;
    }

    std::cout << std::endl;
    return 0;
}
