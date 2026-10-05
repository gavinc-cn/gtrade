//
// Created by dell on 2024/12/12.
//

#pragma once

#include "zrtools/io_pool_v2/io_service.h"
#include "zrtools/io_pool_v2/boost_asio_thread.h"
#include "zrtools/zrt_bmic_hashed.h"
#include "zrtools/zrt_bmic_ordered.h"
#include "zrtools/zrt_bmic.h"
#include "zrtools/zrt_fill.h"
#include "tbuffer.h"
#include "timer_manager_base.h"
#include "type_define.h"

class TimerManagerDummy: public ITimerManager {
public:
    TimerManagerDummy(ServiceMap& pool, const GTradeConfig& gtrade_cfg):
    m_pool(pool),
    m_gtrade_cfg(gtrade_cfg)
    {
        SetThread(zrt::EnginePool::GetInstance().GetSharedThread());
    }
    ~TimerManagerDummy() override = default;
    bool Init() override;
    bool Start() override;
    void UpdateTime(int64_t new_time) override;
protected:  // 处理器开放为 protected 供单测子类同步驱动（成员变量仍保持 private）
    void OnDefaultMsg(int msg_id, const BufPtr buffer) override {}
    BufPtr OnDefaultSyncMsg(int msg_id, const BufPtr buffer) override { return nullptr; }
    // void OnBusRetransBegin(int msg_id, const BufPtr buffer) override {}
    // void OnBusRetransEnd(int msg_id, const BufPtr buffer) override {}
    // void OnStartEpochGenerator(int msg_id, const BufPtr buffer) override;
    void OnTimerEventPush(const DummyTimerInfo& timer_info);
    void ResetTimer(const DummyTimerInfo& timer_info);
    void OnSetTimer(int msg_id, const BufPtr buffer) override;
    void OnKillTimer(int msg_id, const BufPtr buffer) override;
    void OnClearAllTimer(int msg_id, const BufPtr buffer) override;
    void OnPauseAllTimer(int msg_id, const BufPtr buffer) override;
    void OnResumeAllTimer(int msg_id, const BufPtr buffer) override;
    BufPtr OnListAllTimer(int msg_id, const BufPtr buffer) override;
    void OnBacktestTimerEvent(int msg_id, const BufPtr buffer);

private:
    ServiceMap& m_pool;
    const GTradeConfig m_gtrade_cfg {};
    MyHandler* m_strategy_engine {};
    // <service_name,<setter_id,<timer_id,timer>>>
    // std::unordered_map<std::string,std::unordered_map<std::string,std::unordered_map<int,TimerInfo>>> m_timer_map {};
    // TimerInfo m_epoch_generator {};
    std::atomic<int64_t> m_now_ms {};
    // std::multimap<int64_t,DummyTimerInfo> m_time_wheel {};
    zrt::BMIC<ZRT_BMIC(DummyTimerInfo,
        ZRT_BMI_ORDERED(3, unique, TagPrimeKey, DummyTimerInfo, service_name, setter_id, timer_id),
        ZRT_BMI_ORDERED(1, non_unique, TagTargetMs, DummyTimerInfo, target_ms)),
    DummyTimerInfo> m_time_machine {};
};


