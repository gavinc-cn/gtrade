//
// Created by dell on 2026/3/12.
//


#include "strategy_engine.h"

void StrategyEngine::OnQueryTradeRsp(int msg_id, const BufPtr buffer) {
    // using RecvData = MarketInfo;
    // const auto* recv_data_ptr = reinterpret_cast<const RecvData*>(buffer->Data());
    // for (size_t i=0; i<buffer->GetSize()/sizeof(RecvData); ++i) {
    //     const RecvData& recv_data = recv_data_ptr[i];
    //     SPDLOG_INFO("{}", zrt::to_str(recv_data));
    //     m_market_info_map[recv_data.market][recv_data.instrument] = recv_data;
    // }
}