#pragma once

#include <thread>
#include <sched.h>
#include <unistd.h>
#include "spdlog/spdlog.h"

namespace zrt {
    inline bool SetThreadAffinity(pthread_t thread, int cpu_id) {
        cpu_set_t cpuset {};
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_id, &cpuset);
        int err_no = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
        if (err_no) {
            SPDLOG_ERROR("pthread_setaffinity_np failed, err_no={}", err_no);
            return false;
        }
        return true;
    }

    inline bool SetThreadSched(pthread_t thread, int policy, const sched_param& param) {
        int err_no = pthread_setschedparam(thread, policy, &param);
        if (err_no) {
            SPDLOG_ERROR("pthread_setschedparam failed, err_no={}", err_no);
            return false;
        }
        return true;
    }
}