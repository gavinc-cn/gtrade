#include "time_machine.h"
#include <mutex>
#include "spdlog/spdlog.h"
#include "zrtools/zrt_time.h"

TimeMachine::TimeMachine()
{
    SetEpoch();
}

TimeMachine::~TimeMachine() {
    Stop();
}

int64_t TimeMachine::GetCurEpoch() const {
    return timestamp_.load();
}

void TimeMachine::SetEpoch(const int64_t epoch_ns) {
    timestamp_.store(epoch_ns);
}

void TimeMachine::SetEpoch() {
    timestamp_.store(zrt::DateTimeUTC().Epoch19());
}

void TimeMachine::Start(int64_t interval_microseconds) {
    if (!running_.exchange(true)) {
        interval_microseconds_ = interval_microseconds;
        update_thread_ = std::make_unique<std::thread>(&TimeMachine::UpdateEpoch, this);
    }
}

void TimeMachine::Stop() {
    if (running_.exchange(false)) {
        if (update_thread_ && update_thread_->joinable()) {
            update_thread_->join();
        }
        update_thread_.reset();
    }
}

void TimeMachine::UpdateEpoch() {
    while (running_.load()) {
        SetEpoch();
        std::this_thread::sleep_for(std::chrono::microseconds(interval_microseconds_));
    }
}

