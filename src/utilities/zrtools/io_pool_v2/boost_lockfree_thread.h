#pragma once

#include <unordered_map>
#include <boost/lockfree/queue.hpp>
#include "spdlog/spdlog.h"
#include "i_thread.h"

class BoostLockfreeThread: public IThread {
    using Task = std::function<void()>;
    using Queue = boost::lockfree::queue<Task*, boost::lockfree::capacity<65534>>;
public:
    ~BoostLockfreeThread() override = default;
    bool ThreadStart() override {
        if (m_is_started) {
            return true;
        }
        m_is_started = true;
        m_thread = std::thread([this]{
            SetCurrentThreadId();
            while (m_is_started) {
                Task* task = nullptr;
                if (m_queue.pop(task)) {
                    if (task) {
                        (*task)();
                        delete task;
                        task = nullptr;
                    }
                } else {
                    std::this_thread::yield();
                }
            }
        });
        return true;
    }

    bool Post(const Task& task) override {
        Task* task_ptr = new Task(task);
        return m_queue.push(task_ptr);
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
    Queue m_queue;
    std::thread m_thread;
    std::atomic<bool> m_is_started;
};
