//
// Created by dell on 2025/4/1.
//

#pragma once

#include <atomic>
#include <string>
#include <cstdint>

struct GlobalControl {
    static std::atomic<bool> is_running;

    // // 请求程序优雅退出
    // static void RequestShutdown() {
    //     is_running.store(false);
    // }
    //
    // // 检查程序是否运行中
    // static bool IsRunning() {
    //     return is_running.load();
    // }
};

/**
 * HA 角色枚举
 */
enum class HARole {
    kPrimary,   // 主机
    kStandby,   // 备机
    kUnknown,   // 未知（启动时）
};

/**
 * 获取 HA 角色名称
 */
inline const char* GetHARoleName(HARole role) {
    switch (role) {
        case HARole::kPrimary: return "Primary";
        case HARole::kStandby: return "Standby";
        case HARole::kUnknown: return "Unknown";
        default: return "Invalid";
    }
}

/**
 * HA 配置结构
 *
 * 用于配置高可用相关参数
 */
struct HAConfig {
    // 是否启用 HA
    bool enabled {false};

    // 初始角色
    HARole initial_role {HARole::kPrimary};

    // 对端地址（IP:Port 或 hostname:port）
    std::string peer_addr {};

    // 复制服务端口
    int replication_port {9999};

    // 心跳间隔（毫秒）
    int heartbeat_interval_ms {100};

    // 接管超时（毫秒）- 超过此时间没有收到心跳则认为对端宕机
    int takeover_timeout_ms {500};

    // 报单同步等待备机确认
    // true: 报单前等待备机确认 WAL 写入（更安全，延迟略高）
    // false: 异步复制（延迟低，但切换时可能丢失最后几笔订单）
    bool sync_order_send {true};

    // 备机同步超时（毫秒）
    int sync_timeout_ms {50};

    // 自动故障转移
    bool auto_failover {true};

    // 故障转移后自动 Reconcile
    bool auto_reconcile {true};
};

