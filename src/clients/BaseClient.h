
#pragma once

#include "pch.h"
#include "i_exchange_data_dump.h"
#include "i_client.h"



class BaseClient {
public:
    std::map<std::string, int> TIME_MAP = {
        {"1m", 6},
        {"5m", 60 * 5},
        {"15m", 60 * 15},
        {"30m", 60 * 30},
        {"1h", 3600},
        {"4h", 3600 * 4},
        {"12h", 3600 * 12},
        {"1d", 3600 * 24},
        {"1w", 3600 * 24 * 7},
    };
    BaseClient();
    virtual ~BaseClient() = default;
    virtual bool GetMarketInfo(const TBufferPtr& buf, const MarketInfoQryReq& req) {return true;}
    virtual bool GetDepth(TBufferPtr& buf, const DepthQryReq& req) {return true;}
    virtual bool GetBalance(TBufferPtr& buf, const BalanceQryReq& req) {return true;}
    virtual bool QryEntrust(TBufferPtr& buf, const EntrustQryReq& req) {return true;}
    virtual bool QryOpenEntrusts(TBufferPtr& buf, const OpenEntrustsQryReq& req) {return true;}
    virtual bool QryHisEntrusts(TBufferPtr& buf, const HisEntrustsQryReq& req) {return true;}
    virtual bool QryKLine(TBufferPtr& buf, const KLineQryReq& req) {return true;}

private:
    std::map<std::string, int> _counter;
};

