//
// Created by dell on 2024/12/12.
//

#pragma once

#include "zrtools/io_pool_v2/io_service.h"
#include "zrtools/io_pool_v2/boost_asio_thread.h"
#include "zrtools/zrt_bmic.h"
#include "zrtools/zrt_fill.h"
#include "tbuffer.h"
#include "timer_manager_base.h"
#include "type_define.h"
#include "service_map.h"
#include "string_keys.h"

class TimerManager: public ITimerManager {
public:
    TimerManager(ServiceMap& pool, const GTradeConfig& gtrade_cfg):
    m_pool(pool),
    m_gtrade_cfg(gtrade_cfg)
    {
        SetThread(zrt::EnginePool::GetInstance().GetNamedThread(k_TimerManagerThread));
    }
    ~TimerManager() override = default;
    bool Init() override;
    bool Start() override;
private:
    void OnDefaultMsg(int msg_id, const BufPtr buffer) override {}
    BufPtr OnDefaultSyncMsg(int msg_id, const BufPtr buffer) override { return nullptr; }
    void OnStartEpochGenerator(int msg_id, const BufPtr buffer) override;
    void OnTimerEventPush(const TimerInfo& timer_info);
    void ResetTimer(const TimerInfo& timer_info);
    void OnSetTimer(int msg_id, const BufPtr buffer) override;
    void OnKillTimer(int msg_id, const BufPtr buffer) override;
    void OnClearAllTimer(int msg_id, const BufPtr buffer) override;
    void OnPauseAllTimer(int msg_id, const BufPtr buffer) override;
    void OnResumeAllTimer(int msg_id, const BufPtr buffer) override;
    BufPtr OnListAllTimer(int msg_id, const BufPtr buffer) override;

    ServiceMap& m_pool;
    const GTradeConfig m_gtrade_cfg {};
    MyHandler* m_strategy_engine {};
    // <service_name,<setter_id,<timer_id,timer>>>
    std::unordered_map<std::string,std::unordered_map<std::string,std::unordered_map<int,TimerInfo>>> m_timer_map {};
    TimerInfo m_epoch_generator {};
};


