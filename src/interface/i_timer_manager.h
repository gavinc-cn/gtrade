//
// Created by dell on 2024/12/12.
//

#pragma once

#include "str_types.h"

#include "pch.h"
#include <boost/asio.hpp>

struct TimerKey {
    ServiceIdCs service_name;
    SetterIdCs setter_id;
    int timer_id;
};
inline auto GetPKey(const TimerKey& st) {
    return std::make_tuple(st.service_name, st.setter_id, st.timer_id);
}

struct SetTimerReq: public TimerKey {
    int delay_ms;
    bool repeat;
};
inline auto GetPKey(const SetTimerReq& st) {
    return std::make_tuple(st.service_name, st.setter_id, st.timer_id);
}

struct TimerInfo: public TimerKey {
    std::shared_ptr<boost::asio::steady_timer> timer;
    int delay_ms;
    bool repeat;
};

struct DummyTimerInfo {
    ServiceIdCs service_name;
    SetterIdCs setter_id;
    int timer_id;
    int delay_ms;
    int64_t target_ms;
    bool repeat;
    bool paused;
};
inline auto GetPKey(const DummyTimerInfo& st) {
    return std::make_tuple(st.service_name, st.setter_id, st.timer_id);
}

//struct KillTimerReq: public TimerKey {
//
//};
//
//struct ClearAllTimerReq {
//
//};
//
//struct PauseAllTimerReq {
//
//};
//
//struct ResumeAllTimerReq {
//
//};

struct TimerEventPush: public TimerKey {
    int64_t target_ms;
    // int64_t second;
    // int64_t nano;
};

struct EpochPush {
    int second;
    int nano;
};
