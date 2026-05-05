//
// Created by dell on 2025/3/14.
//


#include "pch.h"
#include "str_types.h"
#include "strategy_engine.h"
#include "define.h"
#include "i_client_dump.h"

void StrategyEngine::OnQueryOrderRsp(int msg_id, const BufPtr buffer) {
    using RecvData = Order;
    const auto* recv_data_ptr = reinterpret_cast<const RecvData*>(buffer->Data());
    for (size_t i=0; i<buffer->GetSize()/sizeof(RecvData); ++i) {
        const RecvData& recv_data = recv_data_ptr[i];
        SPDLOG_INFO("{}", zrt::to_str(recv_data));
        Order* entrust = m_order_manager.FindLocalOrder(recv_data);
        if (entrust && !m_order_manager.IsUp2date(entrust->account_id, entrust->entno)) {
            FillOrderByOrder(*entrust, recv_data);
        }
        m_order_status[recv_data.account_id] = DataStatus::kReady;
    }
}

// todo 合并到QueryEntrustReq
void StrategyEngine::OnQueryOrderByPrivateNoReq(int msg_id, const BufPtr buffer) {
    using RecvData = EntrustQryByPrivateNoReq;
    const auto& recv_data = *reinterpret_cast<const RecvData*>(buffer->Data());

    SPDLOG_INFO("Query entrust by private_no={} from strategy", recv_data.private_no);

    // 从本地OrderManager查找
    Order* local_entrust = m_order_manager.FindLocalOrderByPrivateNo(recv_data.private_no);

    if (local_entrust) {
        // 找到本地委托，返回给策略
        SPDLOG_INFO("Found entrust locally: private_no={}, entno={}, status={}",
                   local_entrust->private_no, local_entrust->entno, local_entrust->status);

        TBufferPtr rsp_buf = std::make_shared<TBuffer>(*local_entrust);
        PostMsg(kQueryOrderByPrivateNoRsp, rsp_buf);
    } else {
        // 本地未找到，返回错误
        // 交易所会在连接时自动推送最近的委托状态，无需主动查询
        SPDLOG_WARN("Entrust not found locally: private_no={}", recv_data.private_no);
        TBufferPtr err_buf = std::make_shared<TBuffer>();
        PostMsg(kQueryOrderByPrivateNoErr, err_buf);
    }
}





void StrategyEngine::OnHandleQueryServerReq(int msg_id, const BufPtr buffer) {
    // using RecvData = QryEntrustReq;
    // auto recv_data = *reinterpret_cast<const RecvData*>(buffer->Data());

    // const int64_t req_id = zrt::get_random<int64_t>();
    // zrt::fill_field(recv_data.req_id, req_id);
    // m_req_id_map[req_id] = recv_data.strat_id;
    // SPDLOG_INFO("req kline, req_id={}, strat_id={}", req_id, recv_data.strat_id);

    // QryKLineReq qry_req {};
    // zrt::fill_field(qry_req.market, recv_data.market);
    // zrt::fill_field(qry_req.instrument, recv_data.instrument);
    // zrt::fill_field(qry_req.coefficient, recv_data.coefficient);
    // zrt::fill_field(qry_req.scale, recv_data.scale);
    // zrt::fill_field(qry_req.start_time, recv_data.start_time);
    // zrt::fill_field(qry_req.end_time, recv_data.end_time);
    // m_kline_manager->QryKLine(recv_data);
    m_qry_srv->PostMsg(msg_id, buffer);
}

void StrategyEngine::OnHandleQueryOrderReq(int msg_id, const BufPtr buffer) {
    using RecvData = EntrustQryReq;
    RecvData recv_data = buffer->RefData<RecvData>();
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    if (!recv_data.entno) {
        zrt::fill_field(recv_data.entno, zrt::GetUmapVal(m_private_no_map[recv_data.strat_id], recv_data.private_no));
    }
    const Order* entrust = m_order_manager.FindLocalOrder(recv_data.entno);
    if (entrust) {
        SendToStrategy(kQueryOrderRsp, std::make_shared<TBuffer>(entrust), entrust->policy_no);
    } else {
        SPDLOG_ERROR("entrust({}) not found", recv_data.entno);
    }
}

void StrategyEngine::OnQueryHisOrdersRsp(int msg_id, const BufPtr buffer) {
    buffer->ForEach<Order>([this](const Order& entrust) {
        SPDLOG_DEBUG("Received entrust from DB: entno={}, private_no={}, policy_no={}, status={}", entrust.entno, entrust.private_no, entrust.policy_no, entrust.status);

        // 将数据库中的委托发送给对应的策略
        if (ZRT_LIKELY(!zrt::is_empty(entrust.policy_no))) {
            if (const auto it = m_strategy_proxy_map.find(entrust.policy_no);
                it != m_strategy_proxy_map.end()) {
                SPDLOG_TRACE("Sending entrust to strategy={}: entno={}, private_no={}, status={}", entrust.policy_no, entrust.entno, entrust.private_no, entrust.status);
                it->second->PostData(kPlaceOrderConfirm, std::make_shared<TBuffer>(entrust));
            }
        }
    });
    SPDLOG_INFO("Query historical entrusts completed: {} entrusts loaded and sent to strategies", buffer->GetSize() / sizeof(Order));
}