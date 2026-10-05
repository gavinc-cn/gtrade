//
// K-Line Event Source Implementation
//

#include "kline_event_source.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include "my_utc.h"
#include "mysql_client.h"
#include "i_client_dump.h"
#include "string_keys.h"

// KLineEventSource Implementation
KLineEventSource::KLineEventSource(
    const GTradeConfig& gtrade_cfg,
    MyHandler& strategy_engine,
    const KeyType& key):
EventSourceBase(gtrade_cfg, strategy_engine, zrt::to_str(key)),
m_key(key),
m_market(std::get<0>(key)),
m_instrument(std::get<1>(key)),
m_coefficient(std::get<2>(key)),
m_scale(std::get<3>(key))
{
}

bool KLineEventSource::Initialize() {
    SPDLOG_INFO("Initializing KLine event source: {}", zrt::to_str(m_key));
    m_qry_srv = m_pool.at(k_QueryServer).get();
    // 预加载初始数据
    PreloadData();
    return !m_cached_data.empty();
}

bool KLineEventSource::AdvanceToTime(const int64_t target_ns) {
    bool has_events = false;

    // 检查是否需要预加载更多数据
    if (m_last_loaded_ns - target_ns < m_preload_ns / 2) {
        PreloadData();
    }
    
    // 处理所有在目标时间之前或等于目标时间的K线数据
    auto it = m_cached_data.upper_bound(m_last_close_ns);
    while (it != m_cached_data.end() && it->first <= target_ns) {
        int64_t kline_ex_time_ns = it->first;
        const KLine& kline = it->second;

        if (kline_ex_time_ns > m_last_open_ns) {
            KLine kline_open = kline;
            zrt::fill_field(kline_open.open, kline.open);
            zrt::fill_field(kline_open.high, kline.open);
            zrt::fill_field(kline_open.low, kline.open);
            zrt::fill_field(kline_open.close, kline.open);
            zrt::fill_field(kline_open.volume, 0);
            // 发送K线开启事件 (K线开始时间)
            SPDLOG_TRACE("KLine open event: {} at {}", zrt::to_str(m_key), MyUTC(kline_ex_time_ns, 19).ToFormat());
            m_strategy_engine.PostMsg(MsgId::kIndicatorKLineOpenPush, std::make_shared<TBuffer>(kline_open));
            m_last_open_ns = kline_ex_time_ns;
        }
        const int64_t close_ns = kline_ex_time_ns + GetScaleNs(m_scale) * m_coefficient;
        if (kline_ex_time_ns > m_last_close_ns && close_ns <= target_ns) {
            SPDLOG_TRACE("KLine close event: {} at {}", zrt::to_str(m_key), MyUTC(close_ns, 19).ToFormat());
            m_strategy_engine.PostMsg(MsgId::kIndicatorKLineClosePush, std::make_shared<TBuffer>(kline));
            m_last_close_ns = kline_ex_time_ns;
        }

        has_events = true;
        ++it;
    }
    
    m_cur_ns = target_ns;
    return has_events;
}

void KLineEventSource::PreloadData() {
    if (m_last_loaded_ns > m_end_ns) {
        return;
    }
    int64_t load_start_ns = std::max(m_last_loaded_ns, m_cur_ns);
    int64_t target_ns = load_start_ns;
    const size_t size_before_load = m_cached_data.size();
    do {
        target_ns += m_preload_ns;
        SPDLOG_DEBUG("Preloading KLine data from {} to {} for {}", MyUTC(load_start_ns).ToFormat(), MyUTC(target_ns).ToFormat(), zrt::to_str(m_key));

        if (LoadKLineData(load_start_ns, target_ns)) {
            m_last_loaded_ns = target_ns;
            load_start_ns = target_ns;
        } else {
            SPDLOG_ERROR("LoadDepthData from {} to {} failed", load_start_ns, target_ns);
            break;
        }
    } while (m_cached_data.size() - size_before_load < m_least_preload_cnt && target_ns <= m_end_ns);
}

int64_t KLineEventSource::GetNextEventNs() const {
    auto it = m_cached_data.upper_bound(m_cur_ns);
    if (it != m_cached_data.end()) {
        return it->first;
    }
    return LLONG_MAX;
}

bool KLineEventSource::HasMoreData() const {
    return m_cached_data.upper_bound(m_cur_ns) != m_cached_data.end();
}

bool KLineEventSource::LoadKLineData(const int64_t start_ns, const int64_t end_ns) {
    KLineQryReq kline_patch_req {};
    zrt::fill_field(kline_patch_req.market, m_market);
    zrt::fill_field(kline_patch_req.instrument, m_instrument);
    zrt::fill_field(kline_patch_req.coefficient, m_coefficient);
    zrt::fill_field(kline_patch_req.scale, m_scale);
    zrt::fill_field(kline_patch_req.start_time, start_ns);
    zrt::fill_field(kline_patch_req.end_time, end_ns);
    TBufferPtr rsp = std::make_shared<TBuffer>();
    if (!m_client->QryKLine(rsp, kline_patch_req)) {
        SPDLOG_ERROR("QryKLine failed: {}", zrt::to_str(kline_patch_req));
        return false;
    }

    const KLineRange& kline_range = *reinterpret_cast<const KLineRange*>(rsp->Data());
    for (int i=0; i<kline_range.count; ++i) {
        const KLine& kline = kline_range.klines[i];
        m_cached_data.emplace(kline.ex_time, kline);
    }
    return true;
}