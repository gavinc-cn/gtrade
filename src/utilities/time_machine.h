#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "zrtools/zrt_define.h"


class TimeMachine {
    ZRT_DECLARE_SINGLETON(TimeMachine);
public:
    int64_t GetCurEpoch() const;
    void SetEpoch(int64_t epoch_ns);
    void SetEpoch();

    void Start(int64_t interval_microseconds);
    void Stop();

private:
    void UpdateEpoch();
    
    std::atomic<int64_t> timestamp_ {};
    std::atomic<bool> running_ {};
    std::unique_ptr<std::thread> update_thread_ {};
    int64_t interval_microseconds_ {200};
};

