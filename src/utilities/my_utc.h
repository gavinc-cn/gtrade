#pragma once

#include "zrtools/zrt_time-inl.h"
#include "time_machine.h"

class MyUTC : public zrt::DateTimeUTC {
public:
    MyUTC() {
        UpdateFromTimeMachine();
    }
    
    MyUTC(const std::string& time_str, const std::string& format)
        : zrt::DateTimeUTC(time_str, format) {
    }
    
    explicit MyUTC(const int64_t epoch19)
        : zrt::DateTimeUTC(epoch19, 19) {
    }
    
    MyUTC(const int64_t epoch, const int digits)
        : zrt::DateTimeUTC(epoch, digits) {
    }
    
    // static MyUTC Now() {
        // return MyUTC();
    // }
    
    void UpdateFromTimeMachine() {
        const auto& tm = TimeMachine::GetInstance();
        const int64_t ts_ns = tm.GetCurEpoch();
        m_ts.tv_sec = ts_ns / zrt::kGiga;
        m_ts.tv_nsec = ts_ns % zrt::kGiga;
    }
};
