//
// Depth Event Source Implementation
//

#include "depth_event_source.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include "my_utc.h"
#include "mysql_client.h"
#include "i_client_dump.h"
#include "string_keys.h"

// DepthEventSource Implementation
DepthEventSource::DepthEventSource(
    const GTradeConfig& gtrade_cfg,
    MyHandler& strategy_engine,
    const KeyType& key):
EventSourceBase(gtrade_cfg, strategy_engine, zrt::to_str(key)),
m_key(key),
m_market(std::get<0>(key)),
m_instrument(std::get<1>(key))
{
}

bool DepthEventSource::Initialize() {
    SPDLOG_INFO("Initializing Depth event source: {}", zrt::to_str(m_key));
    m_qry_srv = m_pool.at(k_QueryServer).get();
    // 预加载初始数据
    PreloadData();
    return true;
}

bool DepthEventSource::AdvanceToTime(const int64_t target_ns) {
    bool has_events = false;

    // 检查是否需要预加载更多数据
    if (m_last_loaded_ns - target_ns < m_preload_ns / 2) {
        PreloadData();
    }
    
    // 处理所有在目标时间之前或等于目标时间的Depth数据
    auto it = m_cached_data.upper_bound(m_cur_ns);
    while (it != m_cached_data.end() && it->first <= target_ns) {
        const int64_t depth_timestamp_ns = it->first;
        const Depth& depth = it->second;

        // 发送Depth事件
        SPDLOG_TRACE("Depth event: {} at {}", zrt::to_str(m_key), MyUTC(depth_timestamp_ns, 19).ToFormat());
        m_strategy_engine.PostMsg(MsgId::kDepth1, std::make_shared<TBuffer>(depth));

        has_events = true;
        ++it;
    }
    
    m_cur_ns = target_ns;
    return has_events;
}

// void DepthEventSource::PreloadData(const int64_t target_ns) {
//     if (target_ns <= m_last_loaded_ns && !m_cached_data.empty()) {
//         // 数据已经加载过了, 但需要确保m_cached_data不为空
//         return;
//     }
//     int64_t real_target_ns = target_ns;
//     while (m_cached_data.empty() && real_target_ns < m_end_ns) {
//         real_target_ns += m_preload_ns;
//
//         const int64_t load_start_ns = std::max(m_last_loaded_ns, m_cur_ns);
//         SPDLOG_DEBUG("Preloading Depth data from {} to {} for {}", load_start_ns, target_ns, zrt::to_str(m_key));
//
//         if (LoadDepthData(load_start_ns, real_target_ns)) {
//             m_last_loaded_ns = real_target_ns;
//         }
//     }
// }

void DepthEventSource::PreloadData() {
    if (m_last_loaded_ns > m_end_ns) {
        return;
    }
    int64_t load_start_ns = std::max(m_last_loaded_ns, m_cur_ns);
    int64_t target_ns = load_start_ns;
    const size_t size_before_load = m_cached_data.size();
    do {
        target_ns += m_preload_ns;
        SPDLOG_DEBUG("Preloading Depth data from {} to {} for {}", MyUTC(load_start_ns).ToFormat(), MyUTC(target_ns).ToFormat(), zrt::to_str(m_key));

        if (LoadDepthData(load_start_ns, target_ns)) {
            m_last_loaded_ns = target_ns;
            load_start_ns = target_ns;
        } else {
            SPDLOG_ERROR("LoadDepthData from {} to {} failed", load_start_ns, target_ns);
            break;
        }
    } while (m_cached_data.size() - size_before_load < m_least_preload_cnt && target_ns <= m_end_ns);
}

int64_t DepthEventSource::GetNextEventNs() const {
    auto it = m_cached_data.upper_bound(m_cur_ns);
    if (it != m_cached_data.end()) {
        return it->first;
    }
    return LLONG_MAX;
}

bool DepthEventSource::HasMoreData() const {
    return m_cached_data.upper_bound(m_cur_ns) != m_cached_data.end();
}

bool DepthEventSource::LoadDepthData(const int64_t start_ns, const int64_t end_ns) {
    DepthQryReq depth_req {};
    zrt::fill_field(depth_req.market, m_market);
    zrt::fill_field(depth_req.instrument, m_instrument);
    zrt::fill_field(depth_req.level, 1); // depth1表只有一档数据
    zrt::fill_field(depth_req.start_time, start_ns);
    zrt::fill_field(depth_req.end_time, end_ns);
    
    TBufferPtr rsp = std::make_shared<TBuffer>();
    if (!m_client->QryDepth(rsp, depth_req)) {
        SPDLOG_ERROR("QryDepth failed: {}", zrt::to_str(depth_req));
        return false;
    }

    // const DepthRange& depth_range = *reinterpret_cast<const DepthRange*>(rsp->Data());
    for (size_t i = 0; i < rsp->GetSize() / sizeof(Depth); ++i) {
        const Depth& depth = reinterpret_cast<const Depth*>(rsp->Data())[i];
        m_cached_data.emplace(depth.local_time, depth);
    }
    return true;
}