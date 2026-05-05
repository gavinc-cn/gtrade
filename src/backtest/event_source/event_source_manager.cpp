//
// Event Source Manager Implementation
//

#include "event_source_manager.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include "my_utc.h"
#include "time_machine.h"

EventSourceManager::EventSourceManager(MyHandler& strategy_engine, const GTradeConfig& config):
m_gtrade_cfg(config),
m_strategy_engine(strategy_engine)
{
    m_start_ns = MyUTC(config.start_date, BACKTEST_TIME_FORMAT).Epoch19();
    m_end_ns = MyUTC(config.end_date, BACKTEST_TIME_FORMAT).Epoch19();
}

bool EventSourceManager::Initialize() {
    m_cur_ns = m_start_ns;
    m_is_running = false;
    return true;
}

bool EventSourceManager::Start() {
    if (m_is_running) {
        SPDLOG_WARN("EventSourceManager is already running");
        return false;
    }
    m_is_running = true;
    SPDLOG_INFO("EventSourceManager started");
    return true;
}

void EventSourceManager::Stop() {
    if (!m_is_running) {
        return;
    }
    m_is_running = false;
    
    // 清理资源
    m_kline_event_sources.clear();
    m_depth_event_sources.clear();
    while (!m_event_queue.empty()) {
        m_event_queue.pop();
    }
    SPDLOG_INFO("EventSourceManager stopped");
}

bool EventSourceManager::SubscribeKLine(const KLineEventSource::KeyType& key) {
    SPDLOG_INFO("subscribing to KLine {}", zrt::to_str(key));
    const auto iter = m_kline_event_sources.find(key);
    if (iter != m_kline_event_sources.end()) {
        return true;
    }
    m_kline_event_sources[key] = std::make_shared<KLineEventSource>(m_gtrade_cfg, m_strategy_engine, key);
    const auto kline_source = m_kline_event_sources[key];
    if (!kline_source) {
        SPDLOG_ERROR("Failed to create KLine event source for {}", zrt::to_str(key));
        return false;
    }
    if (!kline_source->Initialize()) {
        SPDLOG_ERROR("Failed to initialize KLine event source for {}", zrt::to_str(key));
        return false;
    }
    const int64_t next_ns = kline_source->GetNextEventNs();
    if (next_ns != LLONG_MAX) {
        m_event_queue.push({next_ns, kline_source});
    }
    return true;
}

bool EventSourceManager::UnsubscribeKLine(const KLineEventSource::KeyType& id) {
    return true;
}

bool EventSourceManager::SubscribeDepth(const DepthEventSource::KeyType& key) {
    SPDLOG_INFO("subscribing to Depth {}", zrt::to_str(key));
    const auto iter = m_depth_event_sources.find(key);
    if (iter != m_depth_event_sources.end()) {
        return true;
    }
    m_depth_event_sources[key] = std::make_shared<DepthEventSource>(m_gtrade_cfg, m_strategy_engine, key);
    const auto depth_source = m_depth_event_sources[key];
    if (!depth_source) {
        SPDLOG_ERROR("Failed to create Depth event source for {}", zrt::to_str(key));
        return false;
    }
    if (!depth_source->Initialize()) {
        SPDLOG_ERROR("Failed to initialize Depth event source for {}", zrt::to_str(key));
        return false;
    }
    const int64_t next_ns = depth_source->GetNextEventNs();
    if (next_ns != LLONG_MAX) {
        m_event_queue.push({next_ns, depth_source});
    }
    return true;
}

bool EventSourceManager::UnsubscribeDepth(const DepthEventSource::KeyType& id) {
    return true;
}

bool EventSourceManager::AdvanceToTime(const int64_t target_time) {
    if (!m_is_running) {
        return false;
    }
    bool has_events = false;
    
    // 处理所有在目标时间之前的事件
    while (!m_event_queue.empty() && m_event_queue.top().epoch_ns <= target_time) {
        const auto event = m_event_queue.top();
        m_event_queue.pop();
        
        // 推进事件源到该事件的时间
        if (event.source->AdvanceToTime(event.epoch_ns)) {
            has_events = true;
        }
        
        // 获取该事件源的下一个事件时间
        const int64_t next_time = event.source->GetNextEventNs();
        if (next_time != LLONG_MAX) {
            m_event_queue.push({next_time, event.source});
        }
        SPDLOG_INFO("advance to {} event={} sz={} next={}",
            MyUTC(event.epoch_ns).ToFormat(),
            event.source->GetName(),
            m_event_queue.size(),
            MyUTC(next_time).ToFormat());
    }
    
    m_cur_ns = target_time;
    return has_events;
}

bool EventSourceManager::HasMoreEvents() const {
    return !m_event_queue.empty() && m_cur_ns < m_end_ns;
}

void EventSourceManager::RunBacktest() {
    SPDLOG_INFO("Starting backtest from {} to {}", MyUTC(m_start_ns, 19).ToFormat(), MyUTC(m_end_ns, 19).ToFormat());
    
    int64_t cur_ns = m_start_ns;
    const int64_t time_interval_ns = m_gtrade_cfg.backtest_interval * zrt::kMega; // 毫秒转纳秒
    
    while (cur_ns <= m_end_ns || HasMoreEvents()) {
        // 设置时间机器的当前时间
        TimeMachine::GetInstance().SetEpoch(cur_ns);
        AdvanceToTime(cur_ns);
        cur_ns += time_interval_ns;
        // 1分钟打印一次日志
        if ((cur_ns - m_start_ns) % (time_interval_ns * 1000 * 60) == 0) {
            SPDLOG_INFO("Backtest progress: {}", MyUTC(cur_ns, 19).ToFormat());
        }
    }
    SPDLOG_INFO("Backtest completed at time {}", MyUTC(cur_ns, 19).ToFormat());
}