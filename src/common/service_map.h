//
// Created by dell on 2025/4/10.
//

#pragma once

#include "pch.h"
#include "zrtools/zrt_define.h"
#include "type_define.h"

class ServiceMap {
    ZRT_DECLARE_SINGLETON(ServiceMap);
public:
    template<typename... _Args>
    auto emplace(_Args&&... __args) {
        return m_handlers.emplace(std::forward<_Args>(__args)...);
    }

    const auto& at(const std::string& __k) const {
        return m_handlers.at(__k);
    }

    void ForEach(const std::function<void(std::unique_ptr<MyHandler>&)>& func) {
        for (auto& handler : m_handlers) {
            func(handler.second);
        }
    }

    // 停止所有服务，用于优雅退出
    // 在 pool.WaitStop() 之前调用，确保所有服务的额外线程都能正确停止
    void StopAll() {
        for (auto& [name, handler] : m_handlers) {
            SPDLOG_INFO("正在停止服务: {}", name);
            handler->Stop();
        }
    }

private:
    std::unordered_map<std::string,std::unique_ptr<MyHandler>> m_handlers;
};

inline ServiceMap::~ServiceMap() = default;
inline ServiceMap::ServiceMap() = default;
