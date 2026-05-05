#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <unordered_map>
#include <boost/asio.hpp>

#include "i_thread.h"

class BoostAsioThread: public IThread {
public:
    using Task = std::function<void()>;

    BoostAsioThread() = default;
    BoostAsioThread(BoostAsioThread&& other) noexcept:
    m_io_service(std::move(other.m_io_service)),
    m_work(std::move(other.m_work)),
    m_thread(std::move(other.m_thread)),
    m_is_started(other.m_is_started)
    {
    }
    ~BoostAsioThread() override {
        BoostAsioThread::ThreadStop();
    };
    bool ThreadStart() override {
        if (m_is_started) {
            return true;
        }
        m_io_service = std::make_unique<boost::asio::io_service>();
        m_work = std::make_unique<boost::asio::io_service::work>(*m_io_service);
        m_thread = std::make_unique<std::thread>([&]{
            SetCurrentThreadId();
            m_io_service->run();
        });
        m_is_started = true;
        return true;
    }
    bool ThreadStop() override {
        if (!m_is_started) {
            return true;
        }
        m_is_started = false;
        m_io_service->stop();
        if (m_thread->joinable()) {
            m_thread->join();
        }
        return true;
    }
    bool Post(const Task& task) override {
        m_io_service->post(task);
        return true;
    }

protected:
    boost::asio::io_service* RefIoService() {
        return m_io_service.get();
    }

private:
    std::unique_ptr<boost::asio::io_service> m_io_service {};
    std::unique_ptr<boost::asio::io_service::work> m_work {};
    std::unique_ptr<std::thread> m_thread {};
    bool m_is_started {};
};

