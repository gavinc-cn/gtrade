//
// Event Source Base Class for Backtest and Live Trading
//

#pragma once

#include "pch.h"
#include <chrono>
#include <functional>
#include <tuple>
#include <string>
#include "type_define.h"

// 事件源基类
class EventSourceBase {
public:
    EventSourceBase(const GTradeConfig& gtrade_cfg, MyHandler& strategy_engine, const std::string& name):
    m_gtrade_cfg(gtrade_cfg),
    m_strategy_engine(strategy_engine),
    m_name(name)
    {}
    virtual ~EventSourceBase() = default;
    // 初始化事件源，设置起始时间
    virtual bool Initialize() = 0;
    // 推进到指定时间，返回是否有新事件产生
    virtual bool AdvanceToTime(int64_t target_ns) = 0;
    // 获取下一个事件的时间戳，如果没有返回LLONG_MAX
    virtual int64_t GetNextEventNs() const = 0;
    // 是否有更多数据
    virtual bool HasMoreData() const = 0;
    // 获取事件源名称
    const std::string& GetName() const { return m_name; }

protected:
    // 预加载数据到指定时间
    virtual void PreloadData() = 0;

    const GTradeConfig& m_gtrade_cfg {};
    MyHandler& m_strategy_engine;
    std::string m_name {};
    static constexpr int64_t m_least_preload_cnt = 10000;
};