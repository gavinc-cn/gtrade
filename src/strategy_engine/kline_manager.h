//
// Created by dell on 2025/4/19.
//

#pragma once

#include "qry_srv.h"
#include "zrtools/zrt_bmic_ordered.h"
#include "zrtools/zrt_misc.h"
#include "tuple_hash_map.h"

class KLineManager {
public:
    KLineManager(const GTradeConfig& gtrade_cfg, MyHandler& strat_engine, QueryServer& qry_srv);
    bool QryKLine(const KLineQryReq& req);
    bool AddKLine(const KLineRange& kline_range);
    void FindMissingKline(const KLineQryReq& req);
    void ResponseKLineQuery(const KLineQryReq& req);

private:
    const GTradeConfig m_gtrade_cfg {};
    MyHandler& m_strat_engine;
    QueryServer& m_qry_srv;
    zrt::BMIC<ZRT_ORDERED_BMIC(5, KLine, market, instrument, coefficient, scale, ex_time),KLine> m_kline_bmic {};
    // <market,<instrument,<coefficient,<scale,<ex_time,KLine>>>>>
    std::unordered_map<std::string, // market
        std::unordered_map<std::string, // instrument
            std::unordered_map<int, // coefficient
                std::unordered_map<char, // scale
                    std::map<int64_t,KLine>>>>> m_kline_map;
    // // <market,<instrument,<coefficient,<scale,<start_time,end_time>>>>>
    // std::unordered_map<std::string, // market
    //     std::unordered_map<std::string, // instrument
    //         std::unordered_map<int, // coefficient
    //             std::unordered_map<char, // scale
    //                 std::unordered_set<std::pair<int64_t,int64_t>>>>>> m_missing_range_map;
    // <req_id,<start_time,end_time>>
    std::unordered_map<int64_t,std::unordered_set<std::pair<int64_t,int64_t>,zrt::PairHasher>> m_missing_range_map {};
    // <req_id,<QryKLineReq>>
    std::unordered_map<int64_t,KLineQryReq> m_suspending_req {};
    zrt::TupleHashMap<KLineRange> m_kline_range_map {};
    // std::unordered_map<std::string, std::vector<KLine>> m_kline_map;
};
