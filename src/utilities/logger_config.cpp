#include "logger_config.h"
#include "zrtools/zrt_misc.h"
#include "zrtools/zrt_time-inl.h"

namespace zrt
{
    std::shared_ptr<spdlog::details::thread_pool> logger_tp = std::make_shared<spdlog::details::thread_pool>(kLoggerQueueSize, kNThread);
//    std::shared_ptr<spdlog::details::thread_pool> logger_tp;

std::shared_ptr<spdlog::logger> create_logger2(const logger_config& config) {
    std::vector<spdlog::sink_ptr> sinks {};
    std::unordered_set<std::string> log_level_set {};
    boost::split(log_level_set, config.m_log_level, boost::is_any_of(","));

#ifdef __linux__
    const std::string time_str = zrt::DateTimeLocal().ToFormat("%Y%m%d_%H%M%S");
#else
    // Windows: use simpler time format
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", &tm);
    const std::string time_str(time_buf);
#endif
    for (const auto& i: log_level_set) {
        std::shared_ptr<spdlog::sinks::sink> rotateSink {};
        if (config.m_max_size && config.m_max_files) {
            rotateSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    config.m_log_file + "." + time_str + "." + i, config.m_max_size, config.m_max_files);
        } else {
            rotateSink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
        config.m_log_file + "." + i, 1, 0);
        }
        rotateSink->set_level(str_to_log_level(i));
        rotateSink->set_pattern(config.file_sink_format);
        sinks.push_back(rotateSink);
    }

    if (!config.m_show_level.empty()) {
        const auto stdoutSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        stdoutSink->set_level(str_to_log_level(config.m_show_level));
        stdoutSink->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] [%s:%#] [%!] %v");
        sinks.push_back(stdoutSink);
    }

    std::shared_ptr<spdlog::logger> logger {};
    if (config.m_async) {
        auto &registry_inst = spdlog::details::registry::instance();
        if (registry_inst.get_tp() == nullptr) {
            registry_inst.set_tp(logger_tp);
        }
        logger = std::make_shared<spdlog::async_logger>(config.m_log_file,
            sinks.begin(), sinks.end(), registry_inst.get_tp(),
            spdlog::async_overflow_policy::block);
    } else {
        logger = std::make_shared<spdlog::logger>(config.m_log_file,
            sinks.begin(), sinks.end());
    }
    logger->set_level(spdlog::level::trace);

    if (config.m_set_default) {
        spdlog::set_default_logger(logger);
        SPDLOG_INFO("[Logger] set logger({}) default", logger->name());
    } else {
        // 如果logger_name重复会报错, 所以先drop一下, 首次创建也可以drop
        spdlog::drop(config.m_log_file);
        spdlog::register_logger(logger);
        SPDLOG_INFO("[Logger] register logger({})", logger->name());
    }

    spdlog::flush_on(spdlog::level::err);
    spdlog::flush_every(std::chrono::seconds(5));
    SPDLOG_INFO("[Logger] *** Logger({}) started", logger->name());
    return logger;
}

std::shared_ptr<spdlog::logger> create_error_logger(const logger_config& config) {
#ifdef __linux__
    const std::string time_str = zrt::DateTimeLocal().ToFormat("%Y%m%d_%H%M%S");
#else
    // Windows: use simpler time format
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", &tm);
    const std::string time_str(time_buf);
#endif
    std::shared_ptr<spdlog::sinks::sink> rotateSink {};
    if (config.m_max_size && config.m_max_files) {
        rotateSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                config.m_log_file + "." + time_str + ".sync.", config.m_max_size, config.m_max_files);
    } else {
        rotateSink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
    config.m_log_file + ".sync.", 1, 0);
    }
    rotateSink->set_level(spdlog::level::err);
    rotateSink->set_pattern(config.file_sink_format);

    auto logger = std::make_shared<spdlog::logger>(k_sync_logger, rotateSink);
    logger->set_level(spdlog::level::err);

    // 如果logger_name重复会报错, 所以先drop一下, 首次创建也可以drop
    spdlog::drop(k_sync_logger);
    spdlog::register_logger(logger);
    SPDLOG_INFO("[Logger] register logger({})", logger->name());
    return logger;
}

}