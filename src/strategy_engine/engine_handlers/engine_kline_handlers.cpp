//
// Created by dell on 2026/3/8.
//


#include "strategy_engine.h"


// 策略发来的请求完整K线
void StrategyEngine::OnQueryKLineReq(int msg_id, const BufPtr buffer) {
    using RecvData = KLineQryReq;
    auto recv_data = *reinterpret_cast<const RecvData*>(buffer->Data());
    const int64_t req_id = zrt::get_random<int64_t>();
    zrt::fill_field(recv_data.req_id, req_id);
    m_req_id_map[req_id] = recv_data.strat_id;
    SPDLOG_INFO("req kline, req_id={}, strat_id={}", req_id, recv_data.strat_id);
    // 由kline_manager找出本地缺失的部分
    m_kline_manager->QryKLine(recv_data);
}

// 收到K线片段, 要先去拼接
void StrategyEngine::OnQueryKLinePatchRsp(int msg_id, const BufPtr buffer) {
    using RecvData = KLineRange;
    const auto& recv_data = *reinterpret_cast<const RecvData*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    m_kline_manager->AddKLine(recv_data);
}

// 这是完整的K线
void StrategyEngine::OnQueryKLineRsp(int msg_id, const BufPtr buffer) {
    using RecvData = KLineRange;
    const auto& recv_data = *reinterpret_cast<const RecvData*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    std::string strat_id = m_req_id_map[recv_data.req_id];
    const auto iter = m_strategy_proxy_map.find(strat_id);
    if (iter != m_strategy_proxy_map.end()) {
        SPDLOG_DEBUG("post kline to strat_id={}", strat_id);
        iter->second->PostData(msg_id, buffer);
    }
    else {
        SPDLOG_ERROR("strat_id not found, strat_id={}", strat_id);
    }
}