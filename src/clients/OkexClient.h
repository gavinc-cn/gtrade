
#pragma once

#include "pch.h"
#include "BaseClient.h"
#include "httplib.h"
#include "type_define.h"
#include "i_client.h"
#include "sonic_helper.h"

class OkexClient final : public BaseClient {
public:
    explicit OkexClient(const GTradeConfig& gtrade_cfg, const Account& account);
    bool GetMarketInfo(const TBufferPtr& buf, const MarketInfoQryReq& req) override;
    bool GetDepth(TBufferPtr& buf, const DepthQryReq& req) override;
    bool QryKLine(TBufferPtr& buf, const KLineQryReq& req) override;
    bool GetBalance(TBufferPtr& buf, const BalanceQryReq& req) override;
    bool QryEntrust(TBufferPtr& buf, const EntrustQryReq& req) override;
    bool QryOpenEntrusts(TBufferPtr& buf, const OpenEntrustsQryReq& req) override;
    bool QryHisEntrusts(TBufferPtr& buf, const HisEntrustsQryReq& req) override;

    void FillEntrust(Order& entrust, const sonic_json::Node& data);
    bool HttpGet(std::string endpoint, const Params& params, sonic_json::Document& d);
    bool HttpSignedGet(std::string endpoint, const Params& params, sonic_json::Document& d);
    std::string GetSign(const std::string& endpoint, const std::string& secret, const std::string& timestamp, const std::string& method, const std::string& body);
private:
    // 根据账户的market字段获取REST URL
    static std::string GetRestUrl(const GTradeConfig& gtrade_cfg, const Account& account) {
        auto it = gtrade_cfg.url_map.find(account.market);
        if (it != gtrade_cfg.url_map.end()) {
            return it->second.rest;
        }
        SPDLOG_ERROR("URL config not found for market={}", account.market);
        return "";
    }

    const GTradeConfig m_gtrade_cfg {};
    const Account m_account {};
    const std::string m_base_url {GetRestUrl(m_gtrade_cfg, m_account)};
    httplib::Client m_client {m_base_url};
};


