//
// Created by dell on 2026/3/12.
//


#include "strategy_engine.h"



void StrategyEngine::OnQueryBalanceRsp(int msg_id, const BufPtr buffer) {
    using RecvData = Balance;
    const auto* recv_data_ptr = reinterpret_cast<const RecvData*>(buffer->Data());
    for (size_t i=0; i<buffer->GetSize()/sizeof(RecvData); ++i) {
        const RecvData& recv_data = recv_data_ptr[i];
        SPDLOG_INFO("{}", zrt::to_str(recv_data));
        if (!m_order_manager.IsUp2date(recv_data)) {
            m_order_manager.UpdateBalance(recv_data);
        }
        m_balance_status[recv_data.account_id] = DataStatus::kReady;
    }
}
