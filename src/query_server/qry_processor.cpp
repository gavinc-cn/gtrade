//
// Created by dell on 2025/2/19.
//

#include <string>
#include <cryptopp/hmac.h>
#include <cryptopp/sha.h>
#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include "qry_processor.h"
#include "i_client_dump.h"
#include "i_exchange_data.h"   // MarketInfoQryRspHeader
#include "strategy_engine.h"
#include "dict_mapping.h"
#include "tbuffer.h"
#include "i_strategy_engine_dump.h"
#include "type_define_dump.h"
#include "sonic_helper.h"
#include "OkexClient.h"
#include "mysql_client.h"

QueryProcessor::QueryProcessor(const GTradeConfig& gtrade_cfg, QueryServer& qry_srv):
m_qry_srv(qry_srv),
m_gtrade_cfg(gtrade_cfg)
{
}

bool QueryProcessor::Init() {
    SPDLOG_INFO("{}", __PRETTY_FUNCTION__ );
    InstallDefaultHandler([](int msg_id, const BufPtr buffer) {
        SPDLOG_ERROR("msg_id={} buf_sz={}", GetEmName_MsgId(msg_id), buffer->GetSize());
    });
    ZRT_SRV_ADD_HANDLER(kQueryMarketInfoReq, QueryProcessor::OnQueryMarketInfoReq);
    ZRT_SRV_ADD_HANDLER(kQueryPositionReq, QueryProcessor::OnQueryHoldReq);
    ZRT_SRV_ADD_HANDLER(kQueryBalanceReq, QueryProcessor::OnQueryBalanceReq);
    ZRT_SRV_ADD_HANDLER(kQueryKLinePatchReq, QueryProcessor::OnQueryKLineReq);
    ZRT_SRV_ADD_HANDLER(kQueryOrderReq, QueryProcessor::OnHandleEntrustQryReq);
    ZRT_SRV_ADD_HANDLER(kQueryHisOrdersReq, QueryProcessor::OnHandleHisEntrustQryReq);
    // ZRT_ADD_HANDLER(kQueryEntrust, QueryProcessor::OnSendEntrust);
    // ZRT_ADD_HANDLER(kQueryDone, QueryProcessor::OnSendEntrust);
    // ZRT_ADD_HANDLER(kQueryHold, QueryProcessor::OnSendEntrust);
    // ZRT_ADD_HANDLER(kQueryBalance, QueryProcessor::OnSendEntrust);

    ZRT_SRV_ADD_SYNC_HANDLER(kQueryMarketInfoSync, QueryProcessor::OnQueryMarketInfoSync);

    // run();
    // while (!IsOpen()) {
    //     SPDLOG_INFO("wait for QueryProcessor ready");
    //     sleep(1);
    // }
    return true;
}

std::unique_ptr<BaseClient>& QueryProcessor::GetClient(const std::string& market, const std::string& account_id) {
    if (account_id.empty() && !zrt::equal(market, k_mysql)) {
        SPDLOG_ERROR("account_id for market({}) is empty", market);
    }
    auto& client = m_client_map[market][account_id];
    if (!client) {
        if (zrt::equal_any_of(market, k_okx, k_okx_dummy)) {
            client = std::make_unique<OkexClient>(m_gtrade_cfg, m_gtrade_cfg.account_map[account_id]);
        }
        else {
            SPDLOG_ERROR("unexpected market={}", market);
        }
    }
    return client;
}

std::unique_ptr<BaseClient>& QueryProcessor::GetMysqlClient() {
    if (!m_mysql_client) {
        m_mysql_client = std::make_unique<MysqlClient>(m_gtrade_cfg.db_config);
    }
    return m_mysql_client;
}

bool QueryProcessor::OnQueryMarketInfoReqImpl(const BufPtr& buffer, BufPtr& rsp_buffer) {
    const auto& recv_data = buffer->RefData<MarketInfoQryReq>();
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    rsp_buffer = std::make_shared<TBuffer>();
    if (const std::unique_ptr<BaseClient>& client = GetClient(recv_data.market, recv_data.account_id);
        client && client->GetMarketInfo(rsp_buffer, recv_data)) {
        return true;
    } else {
        return false;
    }
}

void QueryProcessor::OnQueryMarketInfoReq(int msg_id, const BufPtr buffer) {
    if (BufPtr send_buf {};
        OnQueryMarketInfoReqImpl(buffer, send_buf)) {
        m_qry_srv.PostMsg(kQueryMarketInfoRsp, send_buf);
    } else {
        m_qry_srv.PostMsg(kQueryMarketInfoErr, buffer);
    }
}

BufPtr QueryProcessor::OnQueryMarketInfoSync(int msg_id, const BufPtr buffer) {
    BufPtr send_buf {};
    const bool ok = OnQueryMarketInfoReqImpl(buffer, send_buf);
    // 同步链路的 Buffer 表达不了失败（框架会把 nullptr 兜底成非空空响应，调用方还会无条件解引用），
    // 因此把成败放进响应首部：ok=1 表示查询成功（后面可能跟 0 条 MarketInfo，属合法结果），
    // ok=0 表示查询失败，由调用方决定重试/跳过 —— 不再用"响应长度是否为 0"表达失败。
    MarketInfoQryRspHeader header {};
    header.ok = ok ? 1 : 0;
    BufPtr rsp_buf = std::make_shared<TBuffer>();
    rsp_buf->Append(header);
    if (ok && send_buf && send_buf->GetSize() > 0) {
        rsp_buf->CopyBuffer(send_buf->Data(), send_buf->GetSize());
    }
    return rsp_buf;
}

void QueryProcessor::OnQueryHoldReq(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const HoldQryReq*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    std::unique_ptr<BaseClient>& client = GetClient(recv_data.market, recv_data.account_id);
    TBufferPtr rsp = std::make_shared<TBuffer>();
    // todo 添加持仓查询接口
    // if (client && client->GetHold(rsp, recv_data)) {
    //     m_qry_srv.PostMsg(kQueryMarketInfoRsp, rsp);
    // } else {
    //     m_qry_srv.PostMsg(kQueryMarketInfoErr, buffer);
    // }
}

void QueryProcessor::OnQueryBalanceReq(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const BalanceQryReq*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    std::unique_ptr<BaseClient>& client = GetClient(recv_data.market, recv_data.account_id);
    TBufferPtr rsp = std::make_shared<TBuffer>();
    // todo 资金查询接口rsp改为tbuffer
    // std::unordered_map<std::string,Balance>& balance_map {};
    // if (client && client->GetBalance(rsp, recv_data)) {
        // m_qry_srv.PostMsg(kQueryMarketInfoRsp, rsp);
    // } else {
        // m_qry_srv.PostMsg(kQueryMarketInfoErr, buffer);
    // }
}

void QueryProcessor::OnQueryKLineReq(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const KLineQryReq*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    // std::unique_ptr<BaseClient>& client = GetClient(k_mysql, recv_data.account_id);
    // 实盘
    if constexpr (GlobalConst::IsRealTrading) {
        std::unique_ptr<BaseClient>& client = GetClient(recv_data.market, recv_data.account_id);
        TBufferPtr rsp = std::make_shared<TBuffer>();
        if (client && client->QryKLine(rsp, recv_data)) {
            m_qry_srv.PostMsg(kQueryKLinePatchRsp, rsp);
        } else {
            m_qry_srv.PostMsg(kQueryKLinePatchErr, buffer);
        }
    }
    // 回测
    else {
        std::unique_ptr<BaseClient>& client = GetMysqlClient();
        TBufferPtr rsp = std::make_shared<TBuffer>();
        if (client && client->QryKLine(rsp, recv_data)) {
            m_qry_srv.PostMsg(kQueryKLinePatchRsp, rsp);
        } else {
            m_qry_srv.PostMsg(kQueryKLinePatchErr, buffer);
        }
    }
}

void QueryProcessor::OnHandleEntrustQryReq(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const EntrustQryReq*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    std::unique_ptr<BaseClient>& client = GetClient(recv_data.market, recv_data.account_id);
    TBufferPtr rsp = std::make_shared<TBuffer>();
    if (client && client->QryEntrust(rsp, recv_data)) {
        m_qry_srv.PostMsg(kQueryOrderRsp, rsp);
    } else {
        m_qry_srv.PostMsg(kQueryOrderErr, buffer);
    }
}

void QueryProcessor::OnHandleHisEntrustQryReq(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const HisEntrustsQryReq*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    std::unique_ptr<BaseClient>& client = GetClient(recv_data.market, recv_data.account_id);
    TBufferPtr rsp = std::make_shared<TBuffer>();
    if (client && client->QryHisEntrusts(rsp, recv_data)) {
        m_qry_srv.PostMsg(kQueryHisOrdersRsp, rsp);
    } else {
        m_qry_srv.PostMsg(kQueryHisOrdersErr, buffer);
    }
}