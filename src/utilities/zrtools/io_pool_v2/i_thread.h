//
// Created by dell on 2024/6/28.
//

#pragma once

#include <boost/thread.hpp>
#include <boost/assign.hpp>
#include <boost/asio.hpp>
#include <thread>


class IThread: public boost::noncopyable
{
    using Task = std::function<void()>;
public:
    virtual ~IThread() = default;
    virtual bool ThreadStart() = 0;
    virtual bool ThreadStop() = 0;
    virtual inline bool Post(const Task& task) = 0;
    virtual boost::asio::io_service* RefIoService() {
        SPDLOG_ERROR("Not implemented");
        return nullptr;
    }

    // 检查当前调用是否在本线程执行
    // 用于同步消息处理时检测死锁风险
    // 默认实现：比较当前线程ID与工作线程ID
    virtual bool IsInThread() const {
        return std::this_thread::get_id() == m_thread_id;
    }

protected:
    // 在工作线程内部调用，设置当前线程ID
    void SetCurrentThreadId() {
        m_thread_id = std::this_thread::get_id();
    }

private:
    std::thread::id m_thread_id {};  // 工作线程ID
};
