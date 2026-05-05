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
#include "BaseClient.h"
#include "define.h"

class QueryServer;

class QueryProcessor: public std::conditional_t<GlobalConst::IsRealTrading, BoostAsioSrv, SyncIoSrv> {
public:
    QueryProcessor(const GTradeConfig& gtrade_cfg, QueryServer& qry_srv);
    ~QueryProcessor() override = default;
    bool Init() override;
    // bool Start() override {return true;}
    std::unique_ptr<BaseClient>& GetClient(const std::string& market, const std::string& account_id);
    std::unique_ptr<BaseClient>& GetMysqlClient();
    bool OnQueryMarketInfoReqImpl(const BufPtr& buffer, BufPtr& rsp_buffer);
    void OnQueryMarketInfoReq(int msg_id, const BufPtr buffer);
    void OnQueryHoldReq(int msg_id, const BufPtr buffer);
    void OnQueryBalanceReq(int msg_id, const BufPtr buffer);
    void OnQueryKLineReq(int msg_id, const BufPtr buffer);
    void OnHandleEntrustQryReq(int msg_id, const BufPtr buffer);
    void OnHandleHisEntrustQryReq(int msg_id, const BufPtr buffer);

    void OnQueryMarketInfoSync(int msg_id, BufPtr buffer, std::promise<BufPtr>& ret);
private:
    QueryServer& m_qry_srv;
    GTradeConfig m_gtrade_cfg {};
    std::string m_account_id {};
    std::unordered_map<std::string,std::unordered_map<std::string,std::unique_ptr<BaseClient>>> m_client_map {};
    std::unique_ptr<BaseClient> m_mysql_client {};
};

