//
// Created by dell on 2024/6/16.
//

#pragma once

#include <unordered_map>
#include "spdlog/spdlog.h"
#include "lock_deque.h"
#include "i_thread.h"

class LockDequeThread: public IThread
{
    using Task = std::function<void()>;
    using Queue = LockDeque<Task>;
public:
    ~LockDequeThread() override = default;
    bool ThreadStart() override {
        if (m_is_started) {
            return true;
        }
        m_is_started = true;
        m_thread = std::thread([this]{
            SetCurrentThreadId();
            while (m_is_started) {
                Task task {};
                if (m_queue.pop_front(task)) {
                    task();
                } else {
                    std::this_thread::yield();
                }
            }
        });
        return true;
    }

    bool Post(const Task& task) override {
        return m_queue.push_back(task);
    };

    bool ThreadStop() override {
        if (!m_is_started) {
            return true;
        }
        m_is_started = false;
        if (m_thread.joinable()) {
            m_thread.join();
        }
        return true;
    }
private:
    Queue m_queue {};
    std::thread m_thread {};
    std::atomic<bool> m_is_started {};
};
