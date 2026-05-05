#pragma once

#include <string>
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <boost/algorithm/string.hpp>
#include "zrtools/zrt_cpp14_compat.h"
#include "zrtools/zrt_string_keys.h"
#include "zrtools/fmt_helper.h"


#define LOG_PREFIX_BUS_SEND "(BUS_SEND) "
#define LOG_PREFIX_BUS_RECV "(BUS_RECV) "
#define LOG_PREFIX_RMQ_SEND "(RMQ_SEND) "
#define LOG_PREFIX_RMQ_RECV "(RMQ_RECV) "
#define LOG_PREFIX_RULE_SEND "(RULE_SEND) "
#define LOG_PREFIX_RULE_RECV "(RULE_RECV) "
#define LOG_PREFIX_RISK     "(RISK) "
#define LOG_PREFIX_SLAVE    "(SLAVE) "
#define LOG_PREFIX_SLACK    "(SLACK) "


#define LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)

#define LOG_ERROR(...) do { \
const std::string tmp_msg = zrt::safe_fmt(__VA_ARGS__); \
SPDLOG_ERROR("{}", tmp_msg); \
auto sync_logger = spdlog::get(k_sync_logger); \
if (sync_logger) SPDLOG_LOGGER_ERROR(sync_logger, "{}", tmp_msg); \
} while (false)

#define LOG_CRITICAL(...) do { \
const std::string tmp_msg = zrt::safe_fmt(__VA_ARGS__); \
SPDLOG_CRITICAL("{}", tmp_msg); \
auto sync_logger = spdlog::get(k_sync_logger); \
if (sync_logger) SPDLOG_LOGGER_CRITICAL(sync_logger, "{}", tmp_msg); \
} while (false)


namespace zrt {
    INLINE_VAR constexpr size_t kLoggerQueueSize = 1000 * 1000;
    INLINE_VAR constexpr size_t kNThread = 1;
    INLINE_VAR constexpr char kLoggerPattern[] = "[%H:%M:%S.%F] [%t] [%l] [%s:%#] %v";
    INLINE_VAR constexpr char kFileLoggerPattern[] = "[%Y-%m-%d %H:%M:%S.%F] [%t] [%^%l%$] [%s:%#] [%!] %v";

    extern std::shared_ptr<spdlog::details::thread_pool> logger_tp;

    inline spdlog::level::level_enum str_to_log_level(const std::string &str) {
        std::string lower_str;
        std::transform(str.begin(), str.end(), back_inserter(lower_str), ::tolower);
        return spdlog::level::from_str(lower_str);
    }

    struct logger_config {
        std::string m_log_file;
        std::string m_show_level = "Info";
        std::string m_log_level = "Info";
        bool m_async = true;
        unsigned m_max_size = 1024 * 1024 * 1024;
        unsigned m_max_files = 10;
        bool m_set_default = true;
        std::string file_sink_format = kFileLoggerPattern;
    };

    // 支持创建多个异步logger
    // 在log中添加函数名
    // 支持同时输出多个log文件
    // 支持rotating sink
    // 支持Windows和Linux平台
    std::shared_ptr<spdlog::logger> create_logger2(const logger_config &config);
    std::shared_ptr<spdlog::logger> create_error_logger(const logger_config &config);
}
