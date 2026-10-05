//
// Created by dell on 2024/12/12.
//

#include "timer_manager.h"
#include "i_timer_manager_dump.h"
#include "logger_config.h"

bool TimerManager::Init() {
    SPDLOG_INFO("{}", __PRETTY_FUNCTION__);

    m_strategy_engine = m_pool.at(k_StrategyEngine).get();

    ZRT_ADD_HANDLER(kStartEpochGenerator, TimerManager::OnStartEpochGenerator);
    ZRT_ADD_HANDLER(kSetTimer, TimerManager::OnSetTimer);
    ZRT_ADD_HANDLER(kKillTimer, TimerManager::OnKillTimer);
    ZRT_ADD_HANDLER(kClearAllTimer, TimerManager::OnClearAllTimer);
    ZRT_ADD_HANDLER(kPauseAllTimer, TimerManager::OnPauseAllTimer);
    ZRT_ADD_HANDLER(kResumeAllTimer, TimerManager::OnResumeAllTimer);

    ZRT_ADD_SYNC_HANDLER(kListAllTimer, TimerManager::OnListAllTimer);

    return true;
}

bool TimerManager::Start() {
    return true;
}

void TimerManager::OnStartEpochGenerator(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("{}", msg_id);
    m_epoch_generator.timer = std::make_shared<boost::asio::steady_timer>(*RefIoService());
    zrt::fill_field(m_epoch_generator.delay_ms, 1000);
    zrt::fill_field(m_epoch_generator.repeat, true);
    ResetTimer(m_epoch_generator);
}

void TimerManager::OnTimerEventPush(const TimerInfo& timer_info) {
    SPDLOG_INFO("{}", zrt::to_str(timer_info));
    // <service_name,<setter_id,<timer_id,timer>>>

    TimerEventPush push_body {};
    zrt::fill_field(push_body.service_name, timer_info.service_name);
    zrt::fill_field(push_body.setter_id, timer_info.setter_id);
    zrt::fill_field(push_body.timer_id, timer_info.timer_id);
    // timespec ts {};
    // clock_gettime(CLOCK_REALTIME, &ts);
    // zrt::fill_field(push_body.second, ts.tv_sec);
    // zrt::fill_field(push_body.nano, ts.tv_nsec);
    if (zrt::equal(push_body.service_name, k_StrategyEngine)) {
        m_strategy_engine->PostMsg(kTimerEvent, std::make_shared<TBuffer>(push_body));
    }
}

void TimerManager::ResetTimer(const TimerInfo& timer_info) {
    timer_info.timer->expires_after(std::chrono::milliseconds(timer_info.delay_ms));
    timer_info.timer->async_wait([this,&timer_info](const boost::system::error_code& e) {
        if (e) {
            LOG_WARN("[ResetTimer] {}", e.message());
            return;
        }
        OnTimerEventPush(timer_info);
        if (timer_info.repeat) {
            ResetTimer(timer_info);
        }
    });
}

void TimerManager::OnSetTimer(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("msg_id={}", msg_id);
    const auto& req = *reinterpret_cast<const SetTimerReq*>(buffer->Data());

    auto& setter_timer_map = m_timer_map[req.service_name][req.setter_id];
    auto iter = setter_timer_map.find(req.timer_id);
    if (iter != setter_timer_map.end()) {
        zrt::fill_field(iter->second.delay_ms, req.delay_ms);
        zrt::fill_field(iter->second.repeat, req.repeat);
        ResetTimer(iter->second);
    } else {
        TimerInfo timer_info {};
        zrt::fill_field(timer_info.service_name, req.service_name);
        zrt::fill_field(timer_info.setter_id, req.setter_id);
        zrt::fill_field(timer_info.timer_id, req.timer_id);
        timer_info.timer = std::make_shared<boost::asio::steady_timer>(*RefIoService());
        zrt::fill_field(timer_info.delay_ms, req.delay_ms);
        zrt::fill_field(timer_info.repeat, req.repeat);
        setter_timer_map.emplace(req.timer_id, timer_info);
        ResetTimer(setter_timer_map.at(req.timer_id));
    }
}

void TimerManager::OnKillTimer(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("{}", msg_id);
    const auto& req = *reinterpret_cast<const TimerKey*>(buffer->Data());
    m_timer_map[req.service_name][req.setter_id].erase(req.timer_id);
}

void TimerManager::OnClearAllTimer(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("{}", msg_id);
    const auto& req = *reinterpret_cast<const TimerKey*>(buffer->Data());
    m_timer_map[req.service_name].erase(req.setter_id);
}

void TimerManager::OnPauseAllTimer(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("{}", msg_id);
    const auto& req = *reinterpret_cast<const TimerKey*>(buffer->Data());
    for (auto& p: m_timer_map[req.service_name][req.setter_id]) {
        p.second.timer->cancel();
    }
}

void TimerManager::OnResumeAllTimer(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("{}", msg_id);
    const auto& req = *reinterpret_cast<const TimerKey*>(buffer->Data());
    for (auto& p: m_timer_map[req.service_name][req.setter_id]) {
        ResetTimer(p.second);
    }
}

BufPtr TimerManager::OnListAllTimer(int msg_id, const BufPtr buffer)
{
    SPDLOG_INFO("{}", msg_id);
    TBufferPtr buf = std::make_shared<TBuffer>();
    const auto& req = *reinterpret_cast<const TimerKey*>(buffer->Data());
    for (auto& p: m_timer_map[req.service_name][req.setter_id]) {
        TimerKey key {};
        zrt::fill_field(key.service_name, p.second.service_name);
        zrt::fill_field(key.setter_id, p.second.setter_id);
        zrt::fill_field(key.timer_id, p.second.timer_id);
        buf->Append(key);
    }
    return buf;
}

