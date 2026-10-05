//
// Created by dell on 2024/12/12.
//

#pragma once

#include "zrtools/io_pool_v2/io_service.h"
#include "zrtools/io_pool_v2/boost_asio_thread.h"
#include "zrtools/zrt_bmic.h"
#include "zrtools/zrt_fill.h"
#include "tbuffer.h"
#include "i_timer_manager.h"
#include "type_define.h"
#include "service_map.h"

class StrategyEngine;

class ITimerManager: public MyHandler {
public:
    // ITimerManager() = default;
    // ~ITimerManager() override = default;
    virtual void UpdateTime(int64_t new_time) {}
protected:
    virtual void OnDefaultMsg(int msg_id, const BufPtr buffer) {}
    virtual BufPtr OnDefaultSyncMsg(int msg_id, const BufPtr buffer) { return nullptr; }
    virtual void OnStartEpochGenerator(int msg_id, const BufPtr buffer) {}
    // virtual void OnTimerEventPush(const TimerInfo& timer_info) {}
    // virtual void ResetTimer(const TimerInfo& timer_info) {}
    virtual void OnSetTimer(int msg_id, const BufPtr buffer) = 0;
    virtual void OnKillTimer(int msg_id, const BufPtr buffer) = 0;
    virtual void OnClearAllTimer(int msg_id, const BufPtr buffer) = 0;
    virtual void OnPauseAllTimer(int msg_id, const BufPtr buffer) = 0;
    virtual void OnResumeAllTimer(int msg_id, const BufPtr buffer) = 0;
    virtual BufPtr OnListAllTimer(int msg_id, const BufPtr buffer) = 0;
};


