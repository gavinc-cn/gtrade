//
// Created by dell on 2025/4/19.
//

#include "kline_manager.h"
#include "i_client_dump.h"
#include "zrtools/stl_dump.h"
#include "db_structures_dump.h"


KLineManager::KLineManager(const GTradeConfig& gtrade_cfg, MyHandler& strat_engine, QueryServer& qry_srv):
m_gtrade_cfg(gtrade_cfg),
m_strat_engine(strat_engine),
m_qry_srv(qry_srv)
{

}

bool KLineManager::QryKLine(const KLineQryReq& req) {
    SPDLOG_INFO("recv kline req: {}", zrt::to_str(req));
    FindMissingKline(req);
    const auto missing_range_set = m_missing_range_map[req.req_id];
    SPDLOG_INFO("missing range: {}", zrt::to_str(missing_range_set));
    // 内存中的K线满足要求
    if (missing_range_set.empty()) {
        ResponseKLineQuery(req);
    }
    // 有缺失的片段
    else {
        m_suspending_req[req.req_id] = req;
        for (const auto&[start_time, end_time]: missing_range_set) {
            KLineQryReq kline_patch_req = req;
            zrt::fill_field(kline_patch_req.start_time, start_time);
            zrt::fill_field(kline_patch_req.end_time, end_time);
            SPDLOG_INFO("req kline: {}", zrt::to_str(kline_patch_req));
            m_qry_srv.PostMsg(kQueryKLinePatchReq, std::make_shared<TBuffer>(kline_patch_req));
        }
    }

    // const auto iter = m_kline_range_map.find(std::make_tuple(req.market, req.instrument, req.coefficient, req.scale, req.start_time, req.end_time));
    // if (iter == m_kline_range_map.end()) {
    //
    // }
    // else {
    //
    // }
    // if (iter != m_kline_range_map.end()) {
    //     int64_t start_time = iter->second.start_time;
    //     int64_t end_time = iter->second.end_time;
    //     if (req.start_time < start_time) {
    //         QryKLineReq tmp = req;
    //         zrt::fill_field(tmp.end_time, start_time);
    //         m_qry_srv.PostMsg(kQueryKLinePatchReq, std::make_shared<TBuffer>(tmp));
    //     }
    //     if (req.end_time > end_time) {
    //         QryKLineReq tmp = req;
    //         zrt::fill_field(tmp.start_time, end_time);
    //         m_qry_srv.PostMsg(kQueryKLinePatchReq, std::make_shared<TBuffer>(tmp));
    //     }
    // }
    return true;
}

// 已经拼接好所有的K线, 发送完整的K线应答
void KLineManager::ResponseKLineQuery(const KLineQryReq& req) {
    const std::map<int64_t,KLine>& kline_map = m_kline_map[req.market][req.instrument][req.coefficient][req.scale];

    KLineRange kline_range {};
    zrt::fill_field(kline_range.req_id, req.req_id);
    zrt::fill_field(kline_range.strat_id, req.strat_id);
    zrt::fill_field(kline_range.market, req.market);
    zrt::fill_field(kline_range.instrument, req.instrument);
    zrt::fill_field(kline_range.coefficient, req.coefficient);
    zrt::fill_field(kline_range.scale, req.scale);
    zrt::fill_field(kline_range.start_time, req.start_time);
    zrt::fill_field(kline_range.end_time, req.end_time);

    const auto lower = kline_map.lower_bound(req.start_time);
    const auto upper = kline_map.upper_bound(req.end_time);
    zrt::fill_field(kline_range.count, std::distance(lower, upper));

    TBufferPtr buf = std::make_shared<TBuffer>();
    buf->Append(kline_range);
    for (auto it = lower; it != upper; ++it) {
        buf->Append(it->second);
    }
    SPDLOG_INFO("cnt={} co={} scale={} inst={} mk={}", kline_range.count, kline_range.coefficient, kline_range.scale, kline_range.instrument, kline_range.market);
    m_strat_engine.PostMsg(kQueryKLineRsp, buf);

    m_missing_range_map.erase(req.req_id);
    m_suspending_req.erase(kline_range.req_id);
}

bool KLineManager::AddKLine(const KLineRange& kline_range) {
    for (int i=0; i<kline_range.count; ++i) {
        const KLine& kline = kline_range.klines[i];
        // m_kline_bmic.Update<TagPrimeKey>(GetPKey(kline), kline);
        m_kline_map[kline_range.market][kline_range.instrument][kline_range.coefficient][kline_range.scale][kline.ex_time] = kline;
        SPDLOG_TRACE("add kline: {}", zrt::to_str(kline));
    }
    SPDLOG_INFO("add kline range: {}", zrt::to_str(kline_range));
    std::pair<int64_t,int64_t> range {};
    range.first = kline_range.start_time;
    range.second = kline_range.end_time;
    auto& missing_range_set = m_missing_range_map[kline_range.req_id];
    missing_range_set.erase(range);
    if (missing_range_set.empty()) {
        ResponseKLineQuery(m_suspending_req[kline_range.req_id]);
    }
    // using RecvData = KLine;
    // const auto* recv_data_ptr = reinterpret_cast<const RecvData*>(buffer->Data());
    // KLineRange kline_range {};
    // bool is_first = true;
    // for (size_t i=0; i<buffer->GetSize()/sizeof(RecvData); ++i) {
    //     const RecvData& recv_data = recv_data_ptr[i];
    //     if (is_first) {
    //         zrt::fill_field(kline_range.market, recv_data.market);
    //         zrt::fill_field(kline_range.instrument, recv_data.instrument);
    //         zrt::fill_field(kline_range.coefficient, recv_data.coefficient);
    //         zrt::fill_field(kline_range.scale, recv_data.scale);
    //         zrt::fill_field(kline_range.start_time, recv_data.ex_time);
    //         is_first = false;
    //     }
    //     zrt::fill_field(kline_range.end_time, recv_data.ex_time);
    //     kline_range.klines[recv_data.ex_time] = recv_data;
    // }
    // m_kline_range_map[GetPKey(kline_range)] = kline_range;

    return true;
}

void KLineManager::FindMissingKline(const KLineQryReq& req) {
    static const std::unordered_map<char,int> scale_map {
        {KLineScale::Sec,  1000},
        {KLineScale::Min,  1000 * 60},
        {KLineScale::Hour, 1000 * 60 * 60},
        {KLineScale::Day,  1000 * 60 * 60 * 24},
    };
    auto& missing_range_set = m_missing_range_map[req.req_id];
    auto& kline_map = m_kline_map[req.market][req.instrument][req.coefficient][req.scale];

    // 出错就认为全都缺失
    int64_t ms_per_scale {};
    if (!zrt::TryGetMapVal(scale_map, req.scale, ms_per_scale)) {
        missing_range_set.emplace(req.start_time, req.end_time);
        SPDLOG_ERROR("unexpected kline scale({})", req.scale);
        return;
    }
    const int64_t max_gap_ms = ms_per_scale * req.coefficient;

    // 空的说明全都缺失
    if (kline_map.empty()) {
        missing_range_set.emplace(req.start_time, req.end_time);
        return;
    }

    bool is_first = true;
    int64_t pre_ex_time = req.start_time;
    const auto begin = kline_map.lower_bound(req.start_time);
    const auto end = kline_map.upper_bound(req.end_time);
    for (auto iter = begin; iter != end; ++iter) {
        // 第一根K线前面都缺失
        int64_t ex_time = iter->first;
        if (is_first && ex_time > req.start_time) {
            missing_range_set.emplace(req.start_time, ex_time);
            is_first = false;
        }
        // 中间空隙大的是缺失
        if (ex_time - pre_ex_time > max_gap_ms) {
            missing_range_set.emplace(pre_ex_time, ex_time);
        }
        pre_ex_time = ex_time;
    }
    // 最后一根后面的都缺失
    if (req.end_time > pre_ex_time) {
        missing_range_set.emplace(pre_ex_time, req.end_time);
    }
}