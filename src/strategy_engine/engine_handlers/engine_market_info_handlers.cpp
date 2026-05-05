//
// Created by dell on 2026/3/8.
//


#include "strategy_engine.h"


void StrategyEngine::RefreshAllMarketInfo(const bool is_sync) {
    // 刷新所有已知市场的信息
    // 收集所有需要刷新的市场：从账户配置和已有缓存中获取
    SPDLOG_INFO("Refreshing market info for all known markets");

    std::unordered_set<std::string> markets_to_refresh {};
    for (const auto& [acc_id, acc] : m_gtrade_cfg.account_map) {
        markets_to_refresh.insert(acc.market);
    }
    for (const auto& [market, _] : m_market_info_map) {
        markets_to_refresh.insert(market);
    }
    LOG_INFO("markets_to_refresh={}", zrt::to_str(markets_to_refresh));

    // 为每个市场发送所有品种类型的查询请求
    for (const auto& market : markets_to_refresh) {
        // 找到该市场对应的账户ID
        std::string account_id {};
        for (const auto& [acc_id, acc] : m_gtrade_cfg.account_map) {
            if (acc.market == market) {
                account_id = acc_id;
                break;
            }
        }
        if (account_id.empty()) {
            LOG_ERROR("No account found for market={}, skip refresh", market);
            continue;
        }

        // 查询所有品种类型
        for (const auto& inst_type : {InstType::Spot, InstType::Margin,
                                       InstType::Swap, InstType::Futures, InstType::Option}) {
            MarketInfoQryReq req {};
            zrt::fill_field(req.market, market);
            zrt::fill_field(req.account_id, account_id);
            zrt::fill_field(req.inst_type, inst_type);
            zrt::fill_field(req.retry_time, 5);

            if (is_sync) {
                // 同步请求自己调用回调完成
                BufPtr rsp_buf = std::make_shared<TBuffer>();
                zrt::func_with_retry([this,&rsp_buf,&req] {
                    m_qry_srv->PostSyncMsg(kQueryMarketInfoSync, std::make_shared<TBuffer>(req), rsp_buf);
                    if (!rsp_buf || !rsp_buf->GetSize()) {
                        throw std::runtime_error("query market info failed");
                    }
                }, req.retry_time, "query market info");
                OnQueryMarketInfoRsp(kQueryMarketInfoRsp, rsp_buf);
            } else {
                // 异步请求
                m_qry_srv->PostMsg(kQueryMarketInfoReq, std::make_shared<TBuffer>(req));
            }
        }
        SPDLOG_DEBUG("Sent market info refresh requests for market={}", market);
    }
    SPDLOG_INFO("Refreshed market info for {} markets", markets_to_refresh.size());
}

void StrategyEngine::OnQueryMarketInfoRsp(int msg_id, const BufPtr buffer) {
    using RecvData = MarketInfo;
    const auto* recv_data_ptr = reinterpret_cast<const RecvData*>(buffer->Data());
    for (size_t i=0; i<buffer->GetSize()/sizeof(RecvData); ++i) {
        const RecvData& recv_data = recv_data_ptr[i];
        SPDLOG_INFO("{}", zrt::to_str(recv_data));
        m_market_info_map[recv_data.market][recv_data.instrument] = recv_data;
        m_market_info_status[recv_data.market] = DataStatus::kReady;
    }
}

// 处理策略查询市场信息实现
std::string StrategyEngine::BuildQueryMarketInfoBuf(const BufPtr& buffer, BufPtr& rsp_buffer) {
    // 策略同步查询市场信息
    const auto& recv = buffer->RefData<StratQryMarketInfoReq>();
    LOG_INFO("{}", zrt::to_str(recv));

    rsp_buffer = std::make_shared<TBuffer>();
    for (const auto& [market, market_info]: m_market_info_map) {
        if (!zrt::equal(market, recv.market)) {
            continue;
        }
        for (const auto& [inst, inst_info]: market_info) {
            if (!zrt::equal(inst, recv.instrument)) {
                continue;
            }
            rsp_buffer->Append(inst_info);
        }
    }
    return recv.strat_id;
}

// 异步处理策略查询市场信息
void StrategyEngine::OnStratQueryMarketInfoReq(int msg_id, const BufPtr buffer) {
    BufPtr rsp_buf {};
    const std::string strat_id = BuildQueryMarketInfoBuf(buffer, rsp_buf);
    SendToStrategy(kQueryMarketInfoRsp, rsp_buf, strat_id);
}

// 同步处理策略查询市场信息
void StrategyEngine::OnStratQueryMarketInfoSync(int msg_id, const BufPtr buffer, std::promise<BufPtr>& ret) {
    BufPtr rsp_buf {};
    BuildQueryMarketInfoBuf(buffer, rsp_buf);
    ret.set_value(rsp_buf);
}

// void StrategyEngine::OnQueryMarketInfo(int msg_id, const BufPtr buffer) {
//     // 策略同步查询市场信息
//     const auto& recv = buffer->RefData<StratQryMarketInfoReq>();
//     LOG_INFO("{}", zrt::to_str(recv));
//
//     const BufPtr buf = std::make_shared<TBuffer>();
//     // 在本地缓存中查找
//     if (const auto market_it = m_market_info_map.find(recv.market);
//         market_it != m_market_info_map.end()) {
//         if (const auto inst_it = market_it->second.find(recv.instrument);
//             inst_it != market_it->second.end()) {
//             buf->Append(inst_it->second);
//         }
//     }
// }

// void StrategyEngine::OnQueryMarketInfoSync(int msg_id, const BufPtr buffer, std::promise<BufPtr>& ret) {
//     OnQueryMarketInfo(msg_id, buffer);
//
//
//     // 策略同步查询市场信息
//     const auto& recv = buffer->RefData<StratQryMarketInfoReq>();
//     LOG_INFO("{}", zrt::to_str(recv));
//
//     const BufPtr buf = std::make_shared<TBuffer>();
//     // 在本地缓存中查找
//     if (const auto market_it = m_market_info_map.find(recv.market);
//         market_it != m_market_info_map.end()) {
//         if (const auto inst_it = market_it->second.find(recv.instrument);
//             inst_it != market_it->second.end()) {
//             buf->Append(inst_it->second);
//             }
//         }
//     ret.set_value(buf);
// }