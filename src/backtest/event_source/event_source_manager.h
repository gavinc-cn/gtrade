//
// Event Source Manager for Backtest and Live Trading
//

#pragma once

#include "pch.h"
#include "event_source_base.h"
#include "kline_event_source.h"
#include "depth_event_source.h"
#include "type_define.h"
#include <memory>
#include <unordered_map>
#include <queue>
#include <chrono>

// 事件时间点
struct EventTimePoint {
    int64_t epoch_ns;
    std::shared_ptr<EventSourceBase> source;
    
    bool operator>(const EventTimePoint& other) const {
        return epoch_ns > other.epoch_ns;
    }
};

// 事件源管理器
class EventSourceManager {
public:
    EventSourceManager(MyHandler& strategy_engine, const GTradeConfig& config);
    ~EventSourceManager() = default;

    bool Initialize();
    bool Start();
    void Stop();

    bool SubscribeKLine(const KLineEventSource::KeyType& key);
    bool UnsubscribeKLine(const KLineEventSource::KeyType& key);
    
    bool SubscribeDepth(const DepthEventSource::KeyType& key);
    bool UnsubscribeDepth(const DepthEventSource::KeyType& key);

    // 设置时间控制模式
    // void SetTimeControlMode(TimeControlMode mode, double speed_multiplier = 1.0);
    
    // 推进到指定时间（回测模式）
    bool AdvanceToTime(int64_t target_time);
    // 运行回测循环（回测模式专用）
    void RunBacktest();
    // 获取当前时间
    int64_t GetCurrentTime() const { return m_cur_ns; }
    // 是否还有更多事件
    bool HasMoreEvents() const;

private:
    const GTradeConfig& m_gtrade_cfg;
    MyHandler& m_strategy_engine;
    // K线事件源映射：K线事件源ID -> K线事件源实例
    std::unordered_map<KLineEventSource::KeyType,std::shared_ptr<KLineEventSource>,zrt::TupleHasher> m_kline_event_sources {};
    // Depth事件源映射：Depth事件源ID -> Depth事件源实例
    std::unordered_map<DepthEventSource::KeyType,std::shared_ptr<DepthEventSource>,zrt::TupleHasher> m_depth_event_sources {};
    // 事件优先队列（最小堆，按时间戳排序）
    std::priority_queue<EventTimePoint, std::vector<EventTimePoint>, std::greater<EventTimePoint>> m_event_queue;
    
    double m_speed_multiplier {1.0};

    int64_t m_cur_ns {};
    int64_t m_start_ns {};
    int64_t m_end_ns {LLONG_MAX};
    
    bool m_is_running {};
};

