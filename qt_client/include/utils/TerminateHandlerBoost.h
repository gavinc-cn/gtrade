#pragma once

#include <exception>
#include <cstdlib>
#include <csignal>
#include <sstream>
#include <QDebug>
#include <spdlog/spdlog.h>
#include <boost/stacktrace.hpp>

namespace QtTerminate {

/**
 * @brief Print stack trace using Boost.Stacktrace
 */
inline void printStackTraceBoost() {
    // 获取当前堆栈
    boost::stacktrace::stacktrace st{};

    if (st.empty()) {
        SPDLOG_CRITICAL("[Terminate]   <empty stack trace>");
        std::fprintf(stderr, "  <empty stack trace>\n");
        return;
    }

    QString stackTrace = QString("Stack trace (%1 frames):").arg(st.size());
    SPDLOG_CRITICAL("[Terminate] {}", stackTrace.toStdString());
    std::fprintf(stderr, "Stack trace (%zu frames):\n", st.size());

    // 使用Boost的格式化输出
    std::ostringstream oss;
    oss << st;

    std::string stack_str = oss.str();

    // 逐行输出
    std::istringstream iss(stack_str);
    std::string line;
    int frame_no = 0;
    while (std::getline(iss, line)) {
        QString frame = QString("  [%1] %2").arg(frame_no++).arg(QString::fromStdString(line));
        SPDLOG_CRITICAL("[Terminate] {}", frame.toStdString());
        std::fprintf(stderr, "  %s\n", line.c_str());
    }
}

/**
 * @brief Custom terminate handler using Boost.Stacktrace
 */
inline void terminateHandlerBoost() {
    std::exception_ptr exptr = std::current_exception();

    if (exptr != nullptr) {
        try {
            std::rethrow_exception(exptr);
        } catch (const std::exception& ex) {
            QString msg = QString("Terminated due to exception: %1").arg(ex.what());
            SPDLOG_CRITICAL("[Terminate] {}", msg.toStdString());
            std::fprintf(stderr, "\n*** Terminated due to exception: %s ***\n", ex.what());
        } catch (...) {
            SPDLOG_CRITICAL("[Terminate] Terminated due to unknown exception");
            std::fprintf(stderr, "\n*** Terminated due to unknown exception ***\n");
        }
    } else {
        SPDLOG_CRITICAL("[Terminate] Terminated (no active exception)");
        std::fprintf(stderr, "\n*** Terminated (no active exception) ***\n");
    }

    printStackTraceBoost();

    // Flush spdlog before exit
    spdlog::shutdown();

    std::abort();
}

/**
 * @brief Install Boost.Stacktrace terminate handler
 */
inline void installTerminateHandlerBoost() {
    std::set_terminate(terminateHandlerBoost);
    SPDLOG_INFO("[Terminate] Boost.Stacktrace terminate handler installed");
}

}  // namespace QtTerminate
