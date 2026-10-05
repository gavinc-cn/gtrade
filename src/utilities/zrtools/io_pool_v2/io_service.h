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
#include "zrtools/io_pool_v2/sync_promise_helper.h"

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

    // 异步发送：低延时热路径,排列约定为异步函数(OnMessage/PostMsg)必须排在同步函数之前
    void PostMsg(const int msg_type, const Buffer &buffer)
    {
        Engine::Post([this, msg_type, buffer] { OnMessage(msg_type, buffer); });
    }

    void OnSyncMessage(const int msg_type, const Buffer buffer, std::promise<Buffer>& promise_obj)
    {
        // handler 抛出的异常若逃逸，除了 promise 不会被兑现（调用方永久阻塞），异常还会冲出
        // boost::asio 的 run() 导致目标线程终止；这里统一兜住并记录，兑现与否由 PostSyncMsg 兜底检查
        try {
            Buffer rsp {};
            if (m_msg_sync_handler_map.count(msg_type)) {
                // 返回值版 handler：直接返回响应，promise 兑现由框架完成，handler 无从漏兑现
                rsp = m_msg_sync_handler_map.at(msg_type)(msg_type, buffer);
            } else if (m_default_sync_handler) {
                rsp = m_default_sync_handler(msg_type, buffer);
            } else {
                SPDLOG_ERROR("no handler for message type: {}", msg_type);
            }
            // handler 返回 nullptr 视同未产生响应：兜底替换为非空空响应（部分调用方拿到响应会直接解引用）
            if (!rsp) {
                SPDLOG_ERROR("sync handler for msg_type({}) returned null response, auto-fulfill empty response", msg_type);
                rsp = zrt::MakeEmptyResponse<Buffer>();
            }
            promise_obj.set_value(std::move(rsp));
        } catch (const std::exception& e) {
            SPDLOG_ERROR("sync handler for msg_type({}) threw exception: {}", msg_type, e.what());
        } catch (...) {
            SPDLOG_ERROR("sync handler for msg_type({}) threw unknown exception", msg_type);
        }
    }

    // 发送同步消息并等待结果
    // 不变式：handler 无论走哪条返回路径（含抛异常），调用方都能拿到结果 ——
    //         handler 未兑现 promise 时由 EnsurePromiseFulfilled 兜底兑现空响应并打印 ERROR
    void PostSyncMsg(const int msg_type, const Buffer buffer, Buffer& result)
    {
        // promise 用 shared_ptr 按值捕获：调用方即使不再等待，处理线程后续兑现也不会悬垂
        auto promise_obj = std::make_shared<std::promise<Buffer>>();
        std::shared_future<Buffer> future_obj = promise_obj->get_future();
        Engine::Post([this, msg_type, buffer, promise_obj, future_obj] {
            OnSyncMessage(msg_type, buffer, *promise_obj);
            zrt::EnsurePromiseFulfilled(msg_type, *promise_obj, future_obj);
        });
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
