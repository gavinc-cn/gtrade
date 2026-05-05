//
// Created by dell on 2024/7/14.
//

#pragma once

#include <iostream>
#include <boost/stacktrace.hpp>
#include <exception>
#include <cstdlib>
#include <spdlog/spdlog.h>

#define SPDLOG_ERROR_STACK_TRACE(...) SPDLOG_ERROR("{}\nTerminate called. Stack trace:\n{}", fmt::format(__VA_ARGS__), zrt::to_str(boost::stacktrace::stacktrace(0, 1)))

#define SAFE_TERMINATE_LOG(prefix, msg) do { \
    try { \
        if (const auto logger = spdlog::default_logger()) { \
            logger->critical(prefix ": \n{}", msg); \
        } else { \
            throw std::runtime_error("Logger unavailable"); \
        } \
    } catch (...) { \
        std::cerr << prefix ": \n" << msg << std::endl; \
    } \
} while (false)

namespace zrt {
    class RunTimeError : public std::runtime_error
    {
    public:
        RunTimeError() :
                std::runtime_error("")
        {
        }

        template <typename... Args>
        explicit RunTimeError(const std::string& format_str, Args&&... args) :
                std::runtime_error(fmt::format(format_str, std::forward<Args>(args)...))
        {
        }

        template <typename... Args>
        explicit RunTimeError(int err_id, const std::string& format_str, Args&&... args) :
                std::runtime_error(fmt::format(format_str, std::forward<Args>(args)...)),
                m_err_id(err_id)
        {
        }

        int err_id() const
        {
            return m_err_id;
        }

    private:
        int m_err_id;
    };

    template<typename T, typename U>
    class TypeConvertError : std::exception
    {
    public:
        explicit TypeConvertError(const T& value): m_value(value) {}

        const char* what() const noexcept override
        {
            return fmt::format("type {} with val {} cannot be converted to {}",
                               typeid(T).name(), m_value, typeid(U).name());
        }

    private:
        T m_value;
    };

    inline void safe_terminate_hdl() noexcept {
        static std::atomic<bool> in_terminate{false};
        if (in_terminate.exchange(true)) { std::abort(); }

        try {
            // 尝试获取当前异常信息
            if (const auto eptr = std::current_exception()) {
                try {
                    std::rethrow_exception(eptr);
                } catch (const std::exception& e) {
                    SAFE_TERMINATE_LOG("Exception", e.what());
                } catch (...) {
                    SAFE_TERMINATE_LOG("Unknown exception", "");
                }
            } else {
                SAFE_TERMINATE_LOG("Terminated without active exception", "");
            }

            SAFE_TERMINATE_LOG("Traceback", boost::stacktrace::to_string(boost::stacktrace::stacktrace()));

        } catch (...) {}

        try {spdlog::shutdown();} catch (...) {}
        std::abort();
    }

    inline void simple_terminate_hdl() noexcept {
        try {
            if (const auto eptr = std::current_exception()) {
                try {
                    std::rethrow_exception(eptr);
                } catch (const std::exception& e) {
                    SPDLOG_CRITICAL("Exception: {}", e.what());
                } catch (...) {
                    SPDLOG_CRITICAL("Unknown exception");
                }
            } else {
                SPDLOG_CRITICAL("Terminated without active exception");
            }

            SPDLOG_CRITICAL("Traceback:\n{}", boost::stacktrace::to_string(boost::stacktrace::stacktrace()));

        } catch (...) {}

        try {spdlog::shutdown();} catch (...) {}
        std::abort();
    }
}