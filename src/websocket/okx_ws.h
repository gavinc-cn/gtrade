//
// Created by dell on 2025/2/15.
//

#pragma once

#include "pch.h"
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
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
#include "service_map.h"
#include "tuple_hash_map.h"

class StrategyEngine;

class OkxWs: public WebSocketBase, public MyHandler {
public:
    // 根据交易所名称获取WebSocket公有URL
    static std::string GetWsPublicUrl(const GTradeConfig& gtrade_cfg, const std::string& exchange) {
        auto it = gtrade_cfg.url_map.find(exchange);
        if (it != gtrade_cfg.url_map.end()) {
            return it->second.ws_public;
        }
        SPDLOG_ERROR("URL config not found for exchange={}", exchange);
        return "";
    }

    OkxWs(const ServiceMap& pool, const GTradeConfig& gtrade_cfg, const std::string& exchange):
    WebSocketBase(GetWsPublicUrl(gtrade_cfg, exchange), gtrade_cfg.proxy_config.http),
    m_gtrade_cfg(gtrade_cfg),
    m_strategy_engine(pool.at(k_StrategyEngine).get()),
    m_exchange(exchange)
    {
        SetThread(zrt::EnginePool::GetInstance().GetSharedThread());
    }
    ~OkxWs() override = default;
    bool Init() override;
    bool Start() override;
    void on_open_impl() override;
    void on_close_impl() override;
    void on_reconnected_impl() override;
    // void subscribe() override;
    void Subscribe(const std::string& channel, const std::string& inst_id);
    void OnSubscribeQuote(int msg_id, const BufPtr buffer);
    void on_message(websocketpp::connection_hdl, client::message_ptr msg) override;
    void OnBboTbt(int64_t entry_time, int64_t monotonic, const std::string& symbol, const rapidjson::Value& data);
private:
    void SendNotifyMsg(const std::string& subject, const std::string& content);
    const GTradeConfig m_gtrade_cfg {};
    MyHandler* m_strategy_engine;
    std::string m_exchange {};  // 交易所名称 (如 "okx", "okx_dummy")
    zrt::TupleHashMap<QuoteSub> m_sub_map {};
};
