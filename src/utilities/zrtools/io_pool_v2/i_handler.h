//
// Created by dell on 2024/6/16.
//

#pragma once

#include <unordered_map>
#include <functional>
#include <memory>
#include <future>
#include "spdlog/spdlog.h"
#include "zrtools/io_pool_v2/engine_pool.h"

template<typename Buffer, typename Callback, typename SyncCallback>
class IHandler {
public:
    // explicit IHandler(zrt::EnginePool& engine_pool): m_engine_pool(engine_pool) {

    // }
    virtual ~IHandler() = default;

    virtual bool Init() = 0;
    virtual bool Start() = 0;
    // 停止服务，用于优雅退出
    // 默认实现为空，有额外线程或资源的服务需要重写此方法
    virtual void Stop() {}

    void InstallDefaultHandler(const Callback& callback)
    {
        m_default_handler = callback;
    }

    void InstallDefaultSyncHandler(const SyncCallback& callback)
    {
        m_default_sync_handler = callback;
    }

    void InstallHandler(int msg_type, const Callback& callback) {
        m_msg_handler_map[msg_type] = callback;
        if (m_msg_id_set.count(msg_type)) {
            SPDLOG_ERROR("msg_type({}) has been overwritten", msg_type);
        } else {
            m_msg_id_set.emplace(msg_type);
        }
    }

	void InstallSyncHandler(int msg_type, const SyncCallback& callback) {
        m_msg_sync_handler_map[msg_type] = callback;
        if (m_msg_id_set.count(msg_type)) {
            SPDLOG_ERROR("msg_type({}) has been overwritten", msg_type);
        } else {
            m_msg_id_set.emplace(msg_type);
        }
    }

    void OnMessage(const int msg_type, const Buffer &buffer)
    {
        if (m_msg_handler_map.count(msg_type)) {
            m_msg_handler_map.at(msg_type)(msg_type, buffer);
        } else if (m_default_handler) {
            m_default_handler(msg_type, buffer);
        } else {
            SPDLOG_ERROR("no handler for message type: {}", msg_type);
        }
        // 没找到handler也算处理过了
        m_engine_pool.AddHandleCnt();
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
        // 没找到handler也算处理过了
        m_engine_pool.AddHandleCnt();
    }

    void PostMsg(const int msg_type, const Buffer &buffer)
    {
        GetThread()->Post([this, msg_type, buffer] { OnMessage(msg_type, buffer); });
        m_engine_pool.AddPostCnt();
    }

    // 发送同步消息并等待结果
    // 注意：如果当前线程就是处理线程，则直接同步执行以避免死锁
    void PostSyncMsg(const int msg_type, const Buffer buffer, Buffer& result)
    {
        // 检测同线程调用，避免死锁
        // 场景：在Handler线程内调用PostSyncMsg会导致自己等待自己
        if (GetThread()->IsInThread()) {
            SPDLOG_TRACE("sync msg({}) executed directly in same thread", msg_type);
            std::promise<Buffer> promise_obj;
            OnSyncMessage(msg_type, buffer, promise_obj);
            result = promise_obj.get_future().get();
            m_engine_pool.AddPostCnt();
            m_engine_pool.AddHandleCnt();
            return;
        }

        std::promise<Buffer> promise_obj;
        std::future<Buffer> future_obj = promise_obj.get_future();
        GetThread()->Post([this, msg_type, buffer, &promise_obj] { OnSyncMessage(msg_type, buffer, promise_obj); });
        m_engine_pool.AddPostCnt();
        SPDLOG_TRACE("sync processing msg({})", msg_type);
        result = future_obj.get();
    }

    IThread* GetThread() const {
          return m_thread;
    }

    void SetThread(IThread* thread) {
          m_thread = thread;
    }

    boost::asio::io_service* RefIoService() const {
        return GetThread()->RefIoService();
    }

private:
    zrt::EnginePool& m_engine_pool = zrt::EnginePool::GetInstance();
    std::unordered_set<int> m_msg_id_set {};
    std::unordered_map<int, Callback> m_msg_handler_map {};
    std::unordered_map<int, SyncCallback> m_msg_sync_handler_map {};
    Callback m_default_handler {};
    SyncCallback m_default_sync_handler {};
    IThread* m_thread {};
};
