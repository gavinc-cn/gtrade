//
// Created by dell on 2024/6/16.
//

#pragma once

#include "io_service.h"

struct IOPoolConfig
{
    std::vector<std::string> named_threads;
    int anonymous_thread_num;
};

template<typename Service>
class IOPool {
public:
    explicit IOPool(const IOPoolConfig& config): m_config(config) {}
    ~IOPool() {
        Stop();
    }

    void Init() {
        // for (const auto& i: m_config.named_threads) {
        //     m_named_threads.emplace(i, std::make_unique<Service>());
        // }
        // for (int i = 0; i < m_config.anonymous_thread_num; ++i) {
        //     m_anonymous_threads.emplace_back(std::make_unique<Service>());
        // }

        SPDLOG_INFO("init io_pool");
        if (m_is_started) {
            SPDLOG_INFO("IOPool is already started");
            return;
        }
        for (auto& i: m_named_threads) {
            SPDLOG_INFO("init named service {}", i.first);
            i.second->Init();
        }
        for (auto& i: m_anonymous_threads) {
            SPDLOG_INFO("init named anonymous service");
            i->Init();
        }
    }

    void Start() {
        SPDLOG_INFO("start io_pool");
        if (m_is_started) {
            SPDLOG_INFO("IOPool is already started");
            return;
        }
        for (auto& i: m_named_threads) {
            SPDLOG_INFO("start service {}", i.first);
            i.second->ThreadStart();
        }
        for (auto& i: m_anonymous_threads) {
            SPDLOG_INFO("start anonymous service");
            i->ThreadStart();
        }
        m_is_started = true;
    }

    void Stop() {
        SPDLOG_INFO("stop io_pool");
        if (!m_is_started) {
            return;
        }
        m_is_started = false;
        for (auto& i: m_named_threads) {
            i.second->ThreadStop();
        }
        for (auto& i: m_anonymous_threads) {
            i->ThreadStop();
        }
    }

    bool IsStarted() const {
        return m_is_started;
    }

    std::shared_ptr<Service> GetNamedThread(const std::string& thread_name)
    {
        const auto iter = m_named_threads.find(thread_name);
        if (iter != m_named_threads.end()) {
            return iter->second;
        } else {
            SPDLOG_ERROR("service {} not found", thread_name);
            return {};
        }
        // if (!m_named_threads.count(thread_name)) {
        //     m_named_threads.emplace(thread_name, std::make_unique<Service>());
        // }
        // return m_named_threads.at(thread_name);
    }

    std::shared_ptr<Service> GetSharedThread()
    {
        if (++m_cur_thread_index >= m_anonymous_threads.size()) {
            m_cur_thread_index = 0;
        }
        return m_anonymous_threads[m_cur_thread_index];
    }

    bool Register(const std::string& service_name, std::shared_ptr<Service> service) {
        if (m_named_threads.count(service_name)) {
            SPDLOG_ERROR("service {} already exists");
            return false;
        }
        m_named_threads.emplace(service_name, service);
        return true;
    }

private:
    std::unordered_map<std::string,std::shared_ptr<Service>> m_named_threads {};
    std::vector<std::shared_ptr<Service>> m_anonymous_threads {};
    const IOPoolConfig m_config {};
    bool m_is_started {};
    size_t m_cur_thread_index {};
};