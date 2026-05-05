#pragma once

#include <unordered_map>
#include "i_thread.h"
#include "zrtools/zrt_define.h"

namespace zrt {
    struct EnginePoolConfig
    {
        std::vector<std::string> named_threads;
        int anonymous_thread_num;
    };

    class EnginePool {
        ZRT_DECLARE_SINGLETON(EnginePool);
    public:
        template<typename T>
        void AddSharedEngine(const int n) {
            for (int i=0; i<n; ++i) {
                m_anonymous_threads.emplace_back(new T());
            }
        }

        template<typename T>
        void AddNamedEngine(const std::string& name) {
            m_named_threads.emplace(name, new T());
        }

        // void Init() {
        //
        // }

        void EngineStart() {
            if (m_is_started) {
                return;
            }
            for (auto& i: m_named_threads) {
                i.second->ThreadStart();
            }
            for (auto& i: m_anonymous_threads) {
                i->ThreadStart();
            }
            m_is_started = true;
        }

        void EngineStop() {
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

        IThread* GetNamedThread(const std::string& name)
        {
            const auto iter = m_named_threads.find(name);
            if (iter != m_named_threads.end()) {
                return iter->second;
            }
            return nullptr;
        }

        IThread* GetSharedThread()
        {
            if (m_anonymous_threads.empty()) {
                return nullptr;
            }
            if (++m_cur_thread_index >= m_anonymous_threads.size()) {
                m_cur_thread_index = 0;
            }
            return m_anonymous_threads[m_cur_thread_index];
        }

        void AddPostCnt() {
            ++m_post_cnt;
            ++m_unhandled_cnt;
        }

        void AddHandleCnt() {
            ++m_handle_cnt;
            --m_unhandled_cnt;
        }

        bool AllTaskDone() const {return !m_unhandled_cnt;}
        void WaitStop() {
            while (!AllTaskDone()) {
                SPDLOG_INFO("remain tasks: post({}) - handle({}) = {}", m_post_cnt, m_handle_cnt, m_unhandled_cnt);
                sleep(1);
            }
            SPDLOG_INFO("remain tasks: post({}) - handle({}) = {}", m_post_cnt, m_handle_cnt, m_unhandled_cnt);
            SPDLOG_INFO("all task done");
            EngineStop();
        }

    private:


        std::unordered_map<std::string,IThread*> m_named_threads {};
        std::vector<IThread*> m_anonymous_threads {};
        const EnginePoolConfig m_config {};
        bool m_is_started {};
        size_t m_cur_thread_index {};
        std::atomic<size_t> m_post_cnt {};
        std::atomic<size_t> m_handle_cnt {};
        std::atomic<int64_t> m_unhandled_cnt {};
    };

    inline EnginePool::EnginePool() = default;
    inline EnginePool::~EnginePool() {
        EngineStop();
        for (auto& p: m_named_threads) {
            delete p.second;
            p.second = nullptr;
        }
        for (auto& e: m_anonymous_threads) {
            delete e;
            e = nullptr;
        }
    }
}