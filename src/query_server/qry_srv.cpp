//
// Created by dell on 2025/2/19.
//

#include <string>
#include <cryptopp/hmac.h>
#include <cryptopp/sha.h>
#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include "i_client_dump.h"
#include "strategy_engine.h"
#include "dict_mapping.h"
#include "tbuffer.h"
#include "i_strategy_engine_dump.h"
#include "type_define_dump.h"
#include "sonic_helper.h"
#include "OkexClient.h"
#include "qry_srv.h"
#include "define.h"

QueryServer::QueryServer(ServiceMap& pool, const GTradeConfig& gtrade_cfg):
m_pool(pool),
m_gtrade_cfg(gtrade_cfg)
{
    // 回测
    if constexpr (!GlobalConst::IsRealTrading) {
        SetThread(zrt::EnginePool::GetInstance().GetNamedThread(k_BackTestThread));
    }
    // 实盘
    else {
        SetThread(zrt::EnginePool::GetInstance().GetSharedThread());
    }
}

bool QueryServer::Init() {
    SPDLOG_INFO("{}", __PRETTY_FUNCTION__ );

    m_strategy_engine = m_pool.at(k_StrategyEngine).get();

    InstallDefaultHandler(DefaultMsgHandler);
    InstallDefaultSyncHandler(DefaultSyncMsgHandler);

    ZRT_ADD_HANDLER(kQueryMarketInfoReq, QueryServer::OnHandleQryReq);
    ZRT_ADD_HANDLER(kQueryOrderReq, QueryServer::OnHandleQryReq);
    ZRT_ADD_HANDLER(kQueryTradeReq, QueryServer::OnHandleQryReq);
    ZRT_ADD_HANDLER(kQueryPositionReq, QueryServer::OnHandleQryReq);
    ZRT_ADD_HANDLER(kQueryBalanceReq, QueryServer::OnHandleQryReq);
    ZRT_ADD_HANDLER(kQueryKLinePatchReq, QueryServer::OnHandleQryReq);
    ZRT_ADD_HANDLER(kQueryHisOrdersReq, QueryServer::OnHandleQryReq);

    ZRT_ADD_HANDLER(kQueryMarketInfoRsp, QueryServer::OnHandleQryRsp);
    ZRT_ADD_HANDLER(kQueryOrderRsp, QueryServer::OnHandleQryRsp);
    ZRT_ADD_HANDLER(kQueryTradeRsp, QueryServer::OnHandleQryRsp);
    ZRT_ADD_HANDLER(kQueryPositionRsp, QueryServer::OnHandleQryRsp);
    ZRT_ADD_HANDLER(kQueryBalanceRsp, QueryServer::OnHandleQryRsp);
    ZRT_ADD_HANDLER(kQueryKLinePatchRsp, QueryServer::OnHandleQryRsp);
    ZRT_ADD_HANDLER(kQueryHisOrdersRsp, QueryServer::OnHandleQryRsp);

    ZRT_ADD_HANDLER(kQueryMarketInfoErr, QueryServer::OnHandleQryError<kQueryMarketInfoReq,MarketInfoQryReq>);
    ZRT_ADD_HANDLER(kQueryOrderErr, QueryServer::OnHandleQryError<kQueryOrderReq,EntrustQryReq>);
    ZRT_ADD_HANDLER(kQueryTradeErr, QueryServer::OnHandleQryError<kQueryTradeReq,DoneQryReq>);
    ZRT_ADD_HANDLER(kQueryPositionErr, QueryServer::OnHandleQryError<kQueryPositionReq,HoldQryReq>);
    ZRT_ADD_HANDLER(kQueryBalanceErr, QueryServer::OnHandleQryError<kQueryBalanceReq,BalanceQryReq>);
    ZRT_ADD_HANDLER(kQueryKLinePatchErr, QueryServer::OnHandleQryError<kQueryKLinePatchReq,KLineQryReq>);
    ZRT_ADD_HANDLER(kQueryHisOrdersErr, QueryServer::OnHandleQryError<kQueryHisOrdersReq,HisEntrustsQryReq>);

    ZRT_ADD_SYNC_HANDLER(kQueryMarketInfoSync, QueryServer::OnHandleSyncQryReq);

    m_qry_processor_pool.reserve(m_gtrade_cfg.query_processor_num);
    for (int i=0; i<m_gtrade_cfg.query_processor_num; ++i) {
        m_qry_processor_pool.emplace_back(std::make_unique<QueryProcessor>(m_gtrade_cfg, *this));
    }
    for (const auto& v: m_qry_processor_pool) {
        v->Init();
    }
    return true;
}

bool QueryServer::Start() {
    SPDLOG_INFO("{}", __PRETTY_FUNCTION__ );
    // MyService::Start();
    for (const auto& v: m_qry_processor_pool) {
        v->ThreadStart();
    }
    return true;
}

std::unique_ptr<QueryProcessor>& QueryServer::GetQryProcessor() {
    return m_qry_processor_pool[m_curr_processor_idx++ % m_qry_processor_pool.size()];
}

void QueryServer::OnHandleQryReq(const int msg_id, const BufPtr buffer) {
    GetQryProcessor()->PostMsg(msg_id, buffer);
}

BufPtr QueryServer::OnHandleSyncQryReq(const int msg_id, const BufPtr buffer) {
    BufPtr rsp = std::make_shared<TBuffer>();
    GetQryProcessor()->PostSyncMsg(msg_id, buffer, rsp);
    return rsp;
}

void QueryServer::OnHandleQryRsp(const int msg_id, const BufPtr buffer) {
    m_strategy_engine->PostMsg(msg_id, buffer);
}

template<int MsgId, typename T>
void QueryServer::OnHandleQryError(int msg_id, const BufPtr buffer) {
    auto recv_data = *reinterpret_cast<const T*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    auto key = std::make_tuple(msg_id, recv_data.market, recv_data.account_id);
    m_timer_map[key] = std::make_unique<boost::asio::steady_timer>(*RefIoService(),std::chrono::seconds(++recv_data.retry_time));
    SPDLOG_INFO("retry after {}s", recv_data.retry_time);
    m_timer_map[key]->async_wait([this,recv_data](const boost::system::error_code &ec) {
        if (ec) {
            SPDLOG_ERROR("{}", ec.message());
            return;
        }
        GetQryProcessor()->PostMsg(MsgId, std::make_shared<TBuffer>(recv_data));
    });
}

