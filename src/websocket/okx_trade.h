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

class StrategyEngine;
class OkexClient;

class OkxTrade: public WebSocketBase, public MyHandler {
public:
    OkxTrade(const GTradeConfig& gtrade_cfg, const std::string& account_id, StrategyEngine& strat_engine);

    // 根据账户获取WebSocket私有URL
    static std::string GetWsPrivateUrl(const GTradeConfig& gtrade_cfg, const std::string& account_id) {
        auto acc_it = gtrade_cfg.account_map.find(account_id);
        if (acc_it == gtrade_cfg.account_map.end()) {
            SPDLOG_ERROR("Account not found: {}", account_id);
            return "";
        }
        auto url_it = gtrade_cfg.url_map.find(acc_it->second.market);
        if (url_it != gtrade_cfg.url_map.end()) {
            return url_it->second.ws_private;
        }
        SPDLOG_ERROR("URL config not found for market={}", acc_it->second.market);
        return "";
    }
    ~OkxTrade() override = default;
    // 初始化
    bool Init() override;
    bool Start() override;

    /// MyHandler线程 ///
    // 订阅
    void SubscribeOrder(const std::string& channel, const std::string& inst_type);
    void SubscribeBalanceAndHold();
    void SubscribePositions();
    void SubscribeTrade(const std::string& channel, const std::string& inst_id);
    void SubscribeAll();
    // 下单
    // static void FillStruct(Entrust& dst, const EntrustReq& src);
    void OnPlaceOrder(int msg_id, const BufPtr buffer);
    // 撤单
    void OnCancelOrder(int msg_id, const BufPtr buffer);
    ///

    /// websocket线程 ///
    void on_open_impl() override;
    void on_close_impl() override;
    void on_reconnected_impl() override;
    void Login();
    // 处理接收的数据
    void on_message(websocketpp::connection_hdl, client::message_ptr msg) override;
    void OnLogin();
    void OnPlaceOrderRsp(const sonic_json::Node& node);
    void OnCancelOrderRsp(const sonic_json::Node& node) const;
    void OnPlaceOrderConfirm(const sonic_json::Node& data);
    void OnBalAndPos(const sonic_json::Node& data) const;
    void OnPositions(const sonic_json::Node& data) const;
    // 工具函数
    std::string GetSign(const std::string& secret, const std::string& timestamp);
    void QueryAllOpenEntrusts();
    ///

private:
    /// 线程安全 ///
    void SendNotifyMsg(const std::string& subject, const std::string& content);
    ///

    // 获取账户的交易所market字段 (缓存的成员变量, 避免每次map查找)
    [[nodiscard]] const std::string& GetMarket() const {
        return m_market;
    }

    StrategyEngine& m_strategy_engine;
    GTradeConfig m_gtrade_cfg {};
    std::string m_account_id {};
    std::string m_market {};  // 缓存账户的market字段 (如 "okx" 或 "okx_dummy")
    std::unique_ptr<OkexClient> m_okex_client {};

    /// websocket线程 ///
    std::unordered_set<int> m_received_from_ws {};
    ///

    static const std::unordered_map<char,std::string> m_price_type_map;
    static const std::unordered_map<std::string, char> m_status_map;
    static const std::unordered_map<char,std::string> m_bs_side_map;
    static const std::unordered_map<char,std::string> m_pos_side_map;
    static const std::unordered_map<char,std::string> m_trade_mode_map;
};

