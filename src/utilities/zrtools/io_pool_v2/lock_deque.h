//
// Created by dell on 2024/6/15.
//

#pragma once

#include <deque>
#include <mutex>

template<typename T>
class LockDeque {
public:
    LockDeque() = default;
    ~LockDeque() = default;
    bool push_back(const T& t)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_q.push_back(t);
        return true;
    }
    bool pop_front(T& t)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_q.empty()) {
            return false;
        } else {
            t = m_q.front();
            m_q.pop_front();
            return true;
        }
    }
private:
    std::deque<T> m_q;
    std::mutex m_mutex;
};