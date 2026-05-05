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

class StrategyEngine;

class QueryServer final : public MyHandler {
public:
    QueryServer(ServiceMap& pool, const GTradeConfig& gtrade_cfg);
    ~QueryServer() override = default;
    bool Init() override;
    bool Start() override;
    std::unique_ptr<QueryProcessor>& GetQryProcessor();
    void OnHandleQryReq(int msg_id, const BufPtr buffer);
    void OnHandleSyncQryReq(int msg_id, const BufPtr buffer, std::promise<BufPtr>& ret);
    void OnHandleQryRsp(int msg_id, const BufPtr buffer);
    template<int MsgId, typename T>
    void OnHandleQryError(int msg_id, const BufPtr buffer);
private:
    ServiceMap& m_pool;
    MyHandler* m_strategy_engine {};
    GTradeConfig m_gtrade_cfg {};
    size_t m_curr_processor_idx {};
    std::vector<std::unique_ptr<QueryProcessor>> m_qry_processor_pool {};
    // <<msg_id,market,account_id>,steady_timer>
    std::unordered_map<std::tuple<int,std::string,std::string>,std::unique_ptr<boost::asio::steady_timer>,zrt::TupleHasher> m_timer_map {};
};

