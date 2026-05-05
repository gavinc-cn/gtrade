//
// Created by dell on 2024/6/22.
//

#pragma once

#include <deque>
#include "zrtools/zrt_time.h"
#include "zrtools/mutex.h"

template <typename T, typename Mutex = zrt::null_mutex>
class TimeBuffer {
    struct Element {
        double time;
        T data;
    };

public:
    explicit TimeBuffer(double limit_seconds): m_limit_seconds(limit_seconds) {}

    void Push(const T& data) {
        double now = zrt::time_now();
        std::lock_guard<Mutex> lg(m_mutex);
        Element element {};
        element.time = now;
        element.data = data;
        m_buffer.push_back(element);
        while (!m_buffer.empty() && now - m_buffer.front().time > m_limit_seconds) {
            m_buffer.pop_front();
        }
    }

//    typename std::deque<Element>::iterator begin() {
//        return m_buffer.begin();
//    }
//
//    typename std::deque<Element>::iterator end() {
//        return m_buffer.end();
//    }

    void ForEach(std::function<void(const T&)> func) {
        double now = zrt::time_now();
        std::lock_guard<Mutex> lg(m_mutex);
        while (!m_buffer.empty() && now - m_buffer.front().time > m_limit_seconds) {
            m_buffer.pop_front();
        }
        for (const auto& element : m_buffer) {
            func(element.data);
        }
    }

private:
    std::deque<Element> m_buffer {};
    const double m_limit_seconds {};
    Mutex m_mutex {};
};