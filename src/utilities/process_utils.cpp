//
// Created by Claude on 2026/01/19.
//

#include "process_utils.h"
#include <cstring>
#include <spdlog/spdlog.h>

#ifdef __linux__
    #include <sys/prctl.h>
    #include <cerrno>
#endif

bool SetProcessName(const std::string& name) {
    if (name.empty()) {
        SPDLOG_ERROR("SetProcessName: name is empty");
        return false;
    }

#ifdef __linux__
    // Linux: 使用 prctl(PR_SET_NAME)
    // 限制: 最多 15 个可见字符 (第 16 个字节是 '\0')

    if (name.size() > 15) {
        SPDLOG_WARN("SetProcessName: name '{}' too long ({}), will be truncated to 15 chars",
                    name, name.size());
    }

    if (prctl(PR_SET_NAME, name.c_str(), 0, 0, 0) == 0) {
        SPDLOG_DEBUG("SetProcessName: successfully set to '{}'", name);
        return true;
    } else {
        SPDLOG_ERROR("SetProcessName: prctl(PR_SET_NAME) failed: {}", strerror(errno));
        return false;
    }

#else
    // 其他平台: 不支持，优雅降级
    SPDLOG_WARN("SetProcessName: not supported on this platform (name='{}')", name);
    return false;
#endif
}

bool SetParamShow(const std::string& name, const int argc, char** argv) {
    if (name.empty()) {
        SPDLOG_ERROR("name is empty");
        return false;
    }

    if (argc < 1 || !argv) {
        SPDLOG_ERROR("invalid argc or argv");
        return false;
    }

    // 2. 修改 argv[0] 以影响 ps -ef
#ifdef __linux__
    // 计算 argv[0] 的可用空间
    // 注意：argv 字符串在内存中通常是连续的
    const size_t argv0_len = strlen(argv[0]);

    // 清空 argv[0]
    memset(argv[0], 0, argv0_len);

    // 写入新的进程名（确保不超过原始长度）
    const size_t copy_len = std::min(name.size(), argv0_len - 1);
    strncpy(argv[0], name.c_str(), copy_len);
    argv[0][copy_len] = '\0';

    SPDLOG_DEBUG("argv[0] set to '{}' (available space: {}, used: {})", argv[0], argv0_len, copy_len);

    // 清空其他 argv 参数（安全做法）
    for (int i = 1; i < argc; ++i) {
        if (argv[i]) {
            memset(argv[i], 0, strlen(argv[i]));
        }
    }

    return true;
#else
    SPDLOG_WARN("SetProcessName: argv modification not supported on this platform");
    return prctl_success;
#endif
}

std::string GetProcessName() {
#ifdef __linux__
    // Linux: 使用 prctl(PR_GET_NAME)
    char name[16] {};
    if (prctl(PR_GET_NAME, name, 0, 0, 0) == 0) {
        name[sizeof(name) - 1] = '\0';
        return name;
    }
#endif
    return "";
}

