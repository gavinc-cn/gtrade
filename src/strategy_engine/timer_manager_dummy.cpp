//
// Created by dell on 2024/12/12.
//

#include "timer_manager_dummy.h"
#include "tuple_hash_map.h"
#include "i_timer_manager_dump.h"
#include "string_keys.h"

bool TimerManagerDummy::Init() {
    SPDLOG_INFO("{}", __PRETTY_FUNCTION__);

    m_strategy_engine = m_pool.at(k_StrategyEngine).get();

    ZRT_ADD_HANDLER(kStartEpochGenerator, TimerManagerDummy::OnStartEpochGenerator);
    ZRT_ADD_HANDLER(kSetTimer, TimerManagerDummy::OnSetTimer);
    ZRT_ADD_HANDLER(kKillTimer, TimerManagerDummy::OnKillTimer);
    ZRT_ADD_HANDLER(kClearAllTimer, TimerManagerDummy::OnClearAllTimer);
    ZRT_ADD_HANDLER(kPauseAllTimer, TimerManagerDummy::OnPauseAllTimer);
    ZRT_ADD_HANDLER(kResumeAllTimer, TimerManagerDummy::OnResumeAllTimer);
    ZRT_ADD_HANDLER(kBacktestTimerEvent, TimerManagerDummy::OnBacktestTimerEvent);

    ZRT_ADD_SYNC_HANDLER(kListAllTimer, TimerManagerDummy::OnListAllTimer);
    return true;

}
bool TimerManagerDummy::Start() {
    return true;
}

void TimerManagerDummy::UpdateTime(int64_t new_time) {
	m_now_ms = new_time;
    m_time_machine.ForEach<TagPrimeKey>([this](const auto& x) {
        if (!x.paused && m_now_ms >= x.target_ms) {
            OnTimerEventPush(x);
        }
    });
}

// void TimerManagerBacktest::OnStartEpochGenerator(int msg_id, const BufPtr buffer) {
//     SPDLOG_INFO("{}", msg_id);
//     m_epoch_generator.timer = std::make_shared<boost::asio::steady_timer>(*RefIoService());
//     zrt::fill_field(m_epoch_generator.delay_ms, 1000);
//     zrt::fill_field(m_epoch_generator.repeat, true);
//     ResetTimer(m_epoch_generator);
// }

void TimerManagerDummy::OnTimerEventPush(const DummyTimerInfo& timer_info) {
    TimerEventPush push_body {};
    zrt::fill_field(push_body.service_name, timer_info.service_name);
    zrt::fill_field(push_body.setter_id, timer_info.setter_id);
    zrt::fill_field(push_body.timer_id, timer_info.timer_id);
    zrt::fill_field(push_body.target_ms, m_now_ms.load());
    // todo pool需要添加广播功能, 这样就可以推送到所有相关的模块中
    if (zrt::equal(push_body.service_name, k_StrategyEngine)) {
        m_strategy_engine->PostMsg(kTimerEvent, std::make_shared<TBuffer>(push_body));
    }
    else {
        SPDLOG_ERROR("unknown service name({})", push_body.service_name);
    }
    SPDLOG_INFO("{}", zrt::to_str(push_body));
    if (timer_info.repeat) {
        ResetTimer(timer_info);
    }
}

void TimerManagerDummy::ResetTimer(const DummyTimerInfo& timer_info) {
    DummyTimerInfo tmp {};
    zrt::fill_field(tmp.service_name, timer_info.service_name);
    zrt::fill_field(tmp.setter_id, timer_info.setter_id);
    zrt::fill_field(tmp.timer_id, timer_info.timer_id);
    zrt::fill_field(tmp.delay_ms, timer_info.delay_ms);
    zrt::fill_field(tmp.target_ms, timer_info.delay_ms + m_now_ms);
    zrt::fill_field(tmp.repeat, timer_info.repeat);
    zrt::fill_field(tmp.paused, false);
    m_time_machine.Update<TagPrimeKey>(GetPKey(timer_info), timer_info);

    // timer_info.timer->expires_after(std::chrono::milliseconds(timer_info.delay_ms));
    // timer_info.timer->async_wait([this,&timer_info](const boost::system::error_code& e) {
        // if (e) {
            // SPDLOG_ERROR("{}", e.message());
            // return;
        // }
        // OnTimerEventPush(timer_info);
        // if (timer_info.repeat) {
            // ResetTimer(timer_info);
        // }
    // });
}

void TimerManagerDummy::OnSetTimer(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("{}", msg_id);
    const auto& req = *reinterpret_cast<const SetTimerReq*>(buffer->Data());

    DummyTimerInfo timer_info {};
    zrt::fill_field(timer_info.service_name, req.service_name);
    zrt::fill_field(timer_info.setter_id, req.setter_id);
    zrt::fill_field(timer_info.timer_id, req.timer_id);
    zrt::fill_field(timer_info.delay_ms, req.delay_ms);
    zrt::fill_field(timer_info.target_ms, req.delay_ms + m_now_ms);
    zrt::fill_field(timer_info.repeat, req.repeat);
    m_time_machine.Update<TagPrimeKey>(GetPKey(timer_info), timer_info);
}

void TimerManagerDummy::OnKillTimer(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("{}", msg_id);
    const auto& req = *reinterpret_cast<const TimerKey*>(buffer->Data());
    m_time_machine.Remove<TagPrimeKey>(GetPKey(req));
}

void TimerManagerDummy::OnClearAllTimer(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("{}", msg_id);
    const auto& req = *reinterpret_cast<const TimerKey*>(buffer->Data());
    m_time_machine.RemoveRange<TagPrimeKey>(std::make_tuple(req.service_name, req.setter_id));
}

void TimerManagerDummy::OnPauseAllTimer(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("{}", msg_id);
    const auto& req = *reinterpret_cast<const TimerKey*>(buffer->Data());
    m_time_machine.UpdateRange<TagPrimeKey>(std::make_tuple(req.service_name, req.setter_id), [this](auto x) {
        x.paused = true;
        x.delay_ms = x.target_ms - m_now_ms;
        return x;
    });
}

void TimerManagerDummy::OnResumeAllTimer(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("{}", msg_id);
    const auto& req = *reinterpret_cast<const TimerKey*>(buffer->Data());
    m_time_machine.UpdateRange<TagPrimeKey>(std::make_tuple(req.service_name, req.setter_id), [this](auto x) {
        x.paused = false;
        // todo 需确认replace()是否能够修改索引涉及的字段
        x.target_ms = x.delay_ms + m_now_ms;
        return x;
    });
    // for (auto& p: m_timer_map[req.service_name][req.setter_id]) {
        // ResetTimer(p.second);
    // }
}

BufPtr TimerManagerDummy::OnListAllTimer(int msg_id, const BufPtr buffer)
{
    SPDLOG_INFO("{}", msg_id);
    TBufferPtr buf = std::make_shared<TBuffer>();
    const auto& req = *reinterpret_cast<const TimerKey*>(buffer->Data());
    m_time_machine.ForEach<TagPrimeKey>([&buf](const auto& x) {
        buf->Append(x);
    });
    // for (auto& p: m_timer_map[req.service_name][req.setter_id]) {
        // TimerKey key {};
        // zrt::fill_field(key.service_name, p.second.service_name);
        // zrt::fill_field(key.setter_id, p.second.setter_id);
        // zrt::fill_field(key.timer_id, p.second.timer_id);
        // buf->Append(key);
    // }
    return buf;
}

void TimerManagerDummy::OnBacktestTimerEvent(int msg_id, const BufPtr buffer) {
    const auto& backtest_ms = *reinterpret_cast<const int64_t*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(backtest_ms));
    UpdateTime(backtest_ms);
}

