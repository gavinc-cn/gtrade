//
// Created by dell on 2026/3/8.
//


#include "strategy_engine.h"
#include "dict_mapping.h"   // DictInstType2Okx：缓存第三维键用 OKX 风格字符串


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

        // 查询所有品种类型（不含 MARGIN：币币杠杆复用现货的 instId，本系统不做杠杆交易）
        for (const auto& inst_type : {InstType::Spot,
                                       InstType::Swap, InstType::Futures, InstType::Option}) {
            MarketInfoQryReq req {};
            zrt::fill_field(req.market, market);
            zrt::fill_field(req.account_id, account_id);
            zrt::fill_field(req.inst_type, inst_type);
            zrt::fill_field(req.retry_time, 5);

            if (is_sync) {
                // 同步请求自己调用回调完成
                BufPtr rsp_buf {};
                bool query_ok = false;
                try {
                    zrt::func_with_retry([this,&rsp_buf,&req] {
                        m_qry_srv->PostSyncMsg(kQueryMarketInfoSync, std::make_shared<TBuffer>(req), rsp_buf);
                        // 成败只认响应头：ok=1 时即使 0 条也是合法结果（该 inst_type 本就没有标的）。
                        // 旧的 GetSize()==0 判据会把"该类型无标的"误判为查询失败 —— 那是本类崩溃的根因。
                        if (!rsp_buf || rsp_buf->GetSize() < sizeof(MarketInfoQryRspHeader)
                            || rsp_buf->RefData<MarketInfoQryRspHeader>().ok == 0) {
                            throw std::runtime_error("query market info failed");
                        }
                    }, req.retry_time, "query market info");
                    query_ok = true;
                } catch (const std::exception& e) {
                    // 单个类别查询失败不再让引擎启动失败（曾一路 throw 到 safe_terminate 致整个进程 abort）：
                    // 打印错误日志后跳过本类别，继续刷新其余市场/品种类型
                    LOG_ERROR("refresh market info failed, market={} inst_type={}, skip it, error: {}",
                              market, std::string(DictInstType2Okx(static_cast<char>(inst_type))), e.what());
                }
                if (!query_ok) {
                    continue;
                }
                // 剥离响应头：既有解析路径只认 MarketInfo 数组（0 条时循环体不执行，无副作用）
                const unsigned int header_size {static_cast<unsigned int>(sizeof(MarketInfoQryRspHeader))};
                const unsigned int body_size {rsp_buf->GetSize() - header_size};
                if (body_size < sizeof(MarketInfo)) {
                    // 查询成功但该类型没有标的：只记错误日志，不算失败、不重试、不退出
                    LOG_ERROR("no market info returned, market={} inst_type={}, skip it",
                              market, std::string(DictInstType2Okx(static_cast<char>(inst_type))));
                    continue;
                }
                OnQueryMarketInfoRsp(kQueryMarketInfoRsp,
                    std::make_shared<TBuffer>(rsp_buf->Data() + header_size, body_size));
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
        // 第三维用 OKX 风格字符串：范围订阅链（HTTP/DB/前端）传的就是它，键同型可一路透传。
        // dict 字符转不出来说明交易所回了未知类型（引擎新老版本不一致），跳过并告警 ——
        // 不能塞个空键进去，否则空键条目会污染列表接口与范围校验。
        const std::string inst_type {DictInstType2Okx(recv_data.inst_type)};
        if (inst_type.empty()) {
            SPDLOG_WARN("skip market info with unknown inst_type={} market={} instrument={}",
                        static_cast<int>(recv_data.inst_type),
                        std::string(recv_data.market), std::string(recv_data.instrument));
            continue;
        }
        m_market_info_map[recv_data.market][recv_data.instrument][inst_type] = recv_data;
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
        const auto inst_it = market_info.find(std::string(recv.instrument));
        if (inst_it == market_info.end()) {
            continue;
        }
        // 策略只按 (market, instrument) 查（StratQryMarketInfoReq 无 inst_type），返回该 instId 下
        // 的全部类型各一条 —— 返回值本就是 vector<MarketInfo>，调用方按 inst_type 自取。
        // 出厂清单每个 instId 只有一种类型，故实际上总是一条。
        for (const auto& entry: inst_it->second) {
            rsp_buffer->Append(entry.second);
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
BufPtr StrategyEngine::OnStratQueryMarketInfoSync(int msg_id, const BufPtr buffer) {
    BufPtr rsp_buf {};
    BuildQueryMarketInfoBuf(buffer, rsp_buf);
    return rsp_buf;
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