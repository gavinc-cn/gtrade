//
// Created by dell on 2024/6/16.
//

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <future>
#include "spdlog/spdlog.h"

template<typename Buffer, typename Callback, typename SyncCallback, typename Engine>
class IOService;

template<typename Buffer, typename Callback, typename SyncCallback, typename Engine>
struct InstallHandlerImpl
{
    IOService<Buffer, Callback, SyncCallback, Engine>& outer;
    explicit InstallHandlerImpl(IOService<Buffer, Callback, SyncCallback, Engine>& outer): outer(outer) {}
    InstallHandlerImpl& operator()(int msg_type, const Callback& callback) {
        outer.m_msg_handler_map[msg_type] = callback;
        if (outer.m_msg_id_set.count(msg_type)) {
            SPDLOG_ERROR("msg_type({}) has been overwritten", msg_type);
        } else {
            outer.m_msg_id_set.emplace(msg_type);
        }
        return *this;
    }
};

template<typename Buffer, typename Callback, typename SyncCallback, typename Engine>
struct InstallSyncHandlerImpl
{
    IOService<Buffer, Callback, SyncCallback, Engine>& outer;
    explicit InstallSyncHandlerImpl(IOService<Buffer, Callback, SyncCallback, Engine>& outer): outer(outer) {}
    InstallSyncHandlerImpl& operator()(int msg_type, const SyncCallback& callback) {
        outer.m_msg_sync_handler_map[msg_type] = callback;
        if (outer.m_msg_id_set.count(msg_type)) {
            SPDLOG_ERROR("msg_type({}) has been overwritten", msg_type);
        } else {
            outer.m_msg_id_set.emplace(msg_type);
        }
        return *this;
    }
};

template<typename Buffer, typename Callback, typename SyncCallback, typename Engine>
class IOService: public Engine {
    friend InstallHandlerImpl<Buffer, Callback, SyncCallback, Engine>;
    friend InstallSyncHandlerImpl<Buffer, Callback, SyncCallback, Engine>;
public:
    void InstallDefaultHandler(const Callback& callback)
    {
        m_default_handler = callback;
    }

    void InstallDefaultSyncHandler(const SyncCallback& callback)
    {
        m_default_sync_handler = callback;
    }

    InstallHandlerImpl<Buffer, Callback, SyncCallback, Engine> InstallHandler()
    {
        return InstallHandlerImpl<Buffer, Callback, SyncCallback, Engine>(*this);
    }

    InstallSyncHandlerImpl<Buffer, Callback, SyncCallback, Engine> InstallSyncHandler()
    {
        return InstallSyncHandlerImpl<Buffer, Callback, SyncCallback, Engine>(*this);
    }

    virtual bool Init() = 0;

    void OnMessage(const int msg_type, const Buffer &buffer)
    {
        if (m_msg_handler_map.count(msg_type)) {
            m_msg_handler_map.at(msg_type)(msg_type, buffer);
        } else if (m_default_handler) {
            m_default_handler(msg_type, buffer);
        } else {
            SPDLOG_ERROR("no handler for message type: {}", msg_type);
        }
    }

    void OnSyncMessage(const int msg_type, const Buffer buffer, std::promise<Buffer>& promise_obj)
    {
        if (m_msg_sync_handler_map.count(msg_type)) {
            m_msg_sync_handler_map.at(msg_type)(msg_type, buffer, promise_obj);
        } else if (m_default_sync_handler) {
            m_default_sync_handler(msg_type, buffer, promise_obj);
        } else {
            SPDLOG_ERROR("no handler for message type: {}", msg_type);
        }
    }

    void PostMsg(const int msg_type, const Buffer &buffer)
    {
        Engine::Post([this, msg_type, buffer] { OnMessage(msg_type, buffer); });
    }

    void PostSyncMsg(const int msg_type, const Buffer buffer, Buffer& result)
    {
        std::promise<Buffer> promise_obj;
        std::future<Buffer> future_obj = promise_obj.get_future();
        Engine::Post([this, msg_type, buffer, &promise_obj] { OnSyncMessage(msg_type, buffer, promise_obj); });
        SPDLOG_TRACE("sync processing msg({})", msg_type);
        result = future_obj.get();
    }
private:
    // 因为有两个handler map, 所以要用一个set统一管理
    std::unordered_set<int> m_msg_id_set {};
    std::unordered_map<int, Callback> m_msg_handler_map {};
    std::unordered_map<int, SyncCallback> m_msg_sync_handler_map {};
    Callback m_default_handler {};
    SyncCallback m_default_sync_handler {};
};
