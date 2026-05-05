#pragma once

#include <exception>
#include <cstdlib>
#include <csignal>
#include <QDebug>
#include <spdlog/spdlog.h>

#ifndef _WIN32
#include <execinfo.h>
#include <cxxabi.h>
#endif

namespace QtTerminate {

/**
 * @brief Print stack trace to stderr and log
 * @param max_frames Maximum number of stack frames to print
 */
inline void printStackTrace(int max_frames = 64) {
#ifndef _WIN32
    void* addrlist[max_frames + 1];

    // Retrieve current stack addresses
    int addrlen = backtrace(addrlist, sizeof(addrlist) / sizeof(void*));

    if (addrlen == 0) {
        SPDLOG_CRITICAL("[Terminate]   <empty stack trace>");
        std::fprintf(stderr, "  <empty stack trace>\n");
        return;
    }

    // Resolve addresses into strings containing "filename(function+address)"
    char** symbollist = backtrace_symbols(addrlist, addrlen);

    QString stackTrace = QString("Stack trace (%1 frames):").arg(addrlen);
    SPDLOG_CRITICAL("[Terminate] {}", stackTrace.toStdString());
    std::fprintf(stderr, "Stack trace (%d frames):\n", addrlen);

    // Print the stack trace
    for (int i = 0; i < addrlen; i++) {
        char* begin_name = nullptr;
        char* begin_offset = nullptr;
        char* end_offset = nullptr;

        // Find parentheses and +address offset surrounding the mangled name
        for (char* p = symbollist[i]; *p; ++p) {
            if (*p == '(') {
                begin_name = p;
            } else if (*p == '+') {
                begin_offset = p;
            } else if (*p == ')' && begin_offset) {
                end_offset = p;
                break;
            }
        }

        if (begin_name && begin_offset && end_offset && begin_name < begin_offset) {
            *begin_name++ = '\0';
            *begin_offset++ = '\0';
            *end_offset = '\0';

            // Demangle the name
            int status{};
            char* real_name = abi::__cxa_demangle(begin_name, nullptr, nullptr, &status);

            if (status == 0) {
                QString frame = QString("  [%1] %2 + %3").arg(i).arg(real_name).arg(begin_offset);
                SPDLOG_CRITICAL("[Terminate] {}", frame.toStdString());
                std::fprintf(stderr, "  [%d] %s + %s\n", i, real_name, begin_offset);
                std::free(real_name);
            } else {
                // Demangling failed, print mangled name
                QString frame = QString("  [%1] %2 + %3").arg(i).arg(begin_name).arg(begin_offset);
                SPDLOG_CRITICAL("[Terminate] {}", frame.toStdString());
                std::fprintf(stderr, "  [%d] %s + %s\n", i, begin_name, begin_offset);
            }
        } else {
            // Couldn't parse the line, just print the whole line
            QString frame = QString("  [%1] %2").arg(i).arg(symbollist[i]);
            SPDLOG_CRITICAL("[Terminate] {}", frame.toStdString());
            std::fprintf(stderr, "  [%d] %s\n", i, symbollist[i]);
        }
    }

    std::free(symbollist);
#else
    // Windows: backtrace not available with standard library
    SPDLOG_CRITICAL("[Terminate] Stack trace not available on Windows (requires external library like DbgHelp)");
    std::fprintf(stderr, "Stack trace not available on Windows\n");
#endif
}

/**
 * @brief Custom terminate handler that prints stack trace and flushes logger
 */
inline void terminateHandler() {
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

    printStackTrace();

    // Flush spdlog before exit
    spdlog::shutdown();

    std::abort();
}

/**
 * @brief Install terminate handler
 */
inline void installTerminateHandler() {
    std::set_terminate(terminateHandler);
    SPDLOG_INFO("[Terminate] Terminate handler installed");
}

}  // namespace QtTerminate
