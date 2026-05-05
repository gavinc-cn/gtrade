//
// Created by dell on 2024/6/16.
//

#pragma once

#include "spdlog/spdlog.h"
#include "i_thread.h"

class SyncThread: public IThread
{
    using Task = std::function<void()>;
public:
    ~SyncThread() override = default;
    bool ThreadStart() override {
        if (m_is_started) {
            return true;
        }
        m_is_started = true;
        return true;
    }

    bool Post(const Task& task) override {
        task();
        return true;
    }

    // SyncThread 直接在调用线程执行，所以总是返回 true
    bool IsInThread() const override {
        return true;
    }

    bool ThreadStop() override {
        if (!m_is_started) {
            return true;
        }
        m_is_started = false;
        return true;
    }
private:
    std::atomic<bool> m_is_started {};
};
