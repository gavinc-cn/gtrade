//
// Created by Claude Code
// 高可用控制器
//
// 职责:
// - 管理主备角色
// - 心跳检测
// - 故障检测与自动切换
// - 协调复制服务和 Reconciliation
//

#pragma once

#include <atomic>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <chrono>
#include <functional>
#include "spdlog/spdlog.h"
#include "global.h"
#include "replication_sender.h"
#include "replication_receiver.h"
#include "reconciliation_service.h"
#include "shm_wal_ring.h"
#include "zrtools/zrt_shm_direct.h"

namespace gtrade {

// 前向声明
class OrderManager;

/**
 * HA 状态（持久化到共享内存）
 */
struct HAState {
    uint32_t magic {0x48415354};  // "HAST"
    uint32_t version {1};
    HARole role {HARole::kUnknown};
    int64_t last_heartbeat_ns {0};
    int64_t role_change_time_ns {0};
    uint64_t term {0};  // 任期号，用于防止脑裂
    char peer_addr[128] {};
    char reserved[108] {};
};
static_assert(sizeof(HAState) == 256, "HAState should be 256 bytes");

/**
 * 高可用控制器
 */
class HAController {
public:
    using RoleChangeCallback = std::function<void(HARole old_role, HARole new_role)>;

    HAController(const HAConfig& config,
                 OrderManager* order_manager,
                 ShmWalRing* wal)
        : config_(config)
        , order_manager_(order_manager)
        , wal_(wal)
        , ha_state_shm_(kHaStateShmName)
        , running_(false)
        , current_role_(HARole::kUnknown)
        , peer_alive_(false)
        , last_peer_heartbeat_ns_(0) {
    }

    ~HAController() {
        Stop();
    }

    // 禁止拷贝
    HAController(const HAController&) = delete;
    HAController& operator=(const HAController&) = delete;

    /**
     * 启动 HA 控制器
     */
    bool Start() {
        if (running_.load()) {
            SPDLOG_WARN("HAController already running");
            return false;
        }

        // 初始化 HA 状态共享内存
        if (!ha_state_shm_.IsValid()) {
            SPDLOG_ERROR("Failed to initialize HA state shared memory");
            return false;
        }

        // 恢复或初始化状态
        HAState* state = ha_state_shm_.Get();
        if (state->magic != 0x48415354) {
            // 首次初始化
            state->magic = 0x48415354;
            state->version = 1;
            state->role = config_.initial_role;
            state->term = 1;
            state->last_heartbeat_ns = GetNanoTimestamp();
            state->role_change_time_ns = state->last_heartbeat_ns;
            std::strncpy(state->peer_addr, config_.peer_addr.c_str(),
                         sizeof(state->peer_addr) - 1);
        }

        current_role_.store(state->role);
        SPDLOG_INFO("Initial HA role: {}", GetHARoleName(current_role_.load()));

        running_.store(true);

        // 根据角色启动相应服务
        if (current_role_.load() == HARole::kPrimary) {
            StartAsPrimary();
        } else if (current_role_.load() == HARole::kStandby) {
            StartAsStandby();
        }

        // 启动心跳检测线程
        heartbeat_thread_ = std::thread([this]() { HeartbeatLoop(); });

        SPDLOG_INFO("HAController started");
        return true;
    }

    /**
     * 停止 HA 控制器
     */
    void Stop() {
        if (!running_.load()) {
            return;
        }

        running_.store(false);

        // 停止复制服务
        if (replication_sender_) {
            replication_sender_->Stop();
        }
        if (replication_receiver_) {
            replication_receiver_->Stop();
        }

        // 等待心跳线程退出
        if (heartbeat_thread_.joinable()) {
            heartbeat_thread_.join();
        }

        SPDLOG_INFO("HAController stopped");
    }

    /**
     * 获取当前角色
     */
    HARole GetRole() const { return current_role_.load(); }

    /**
     * 是否为主机
     */
    bool IsPrimary() const { return current_role_.load() == HARole::kPrimary; }

    /**
     * 是否为备机
     */
    bool IsStandby() const { return current_role_.load() == HARole::kStandby; }

    /**
     * 设置角色变更回调
     */
    void SetRoleChangeCallback(RoleChangeCallback callback) {
        role_change_callback_ = std::move(callback);
    }

    /**
     * 手动切换到主机
     */
    bool PromoteToPrimary() {
        if (current_role_.load() == HARole::kPrimary) {
            SPDLOG_INFO("Already primary");
            return true;
        }

        SPDLOG_INFO("Promoting to primary...");

        HARole old_role = current_role_.load();

        // 停止备机服务
        if (replication_receiver_) {
            replication_receiver_->Stop();
            replication_receiver_.reset();
        }

        // 更新状态
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            HAState* state = ha_state_shm_.Get();
            state->role = HARole::kPrimary;
            state->term++;
            state->role_change_time_ns = GetNanoTimestamp();
            current_role_.store(HARole::kPrimary);
        }

        // 启动主机服务
        StartAsPrimary();

        // 执行 Reconciliation
        if (config_.auto_reconcile) {
            PerformReconciliation();
        }

        // 回调通知
        if (role_change_callback_) {
            role_change_callback_(old_role, HARole::kPrimary);
        }

        SPDLOG_INFO("Promoted to primary successfully");
        return true;
    }

    /**
     * 手动切换到备机
     */
    bool DemoteToStandby() {
        if (current_role_.load() == HARole::kStandby) {
            SPDLOG_INFO("Already standby");
            return true;
        }

        SPDLOG_INFO("Demoting to standby...");

        HARole old_role = current_role_.load();

        // 停止主机服务
        if (replication_sender_) {
            replication_sender_->Stop();
            replication_sender_.reset();
        }

        // 更新状态
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            HAState* state = ha_state_shm_.Get();
            state->role = HARole::kStandby;
            state->role_change_time_ns = GetNanoTimestamp();
            current_role_.store(HARole::kStandby);
        }

        // 启动备机服务
        StartAsStandby();

        // 回调通知
        if (role_change_callback_) {
            role_change_callback_(old_role, HARole::kStandby);
        }

        SPDLOG_INFO("Demoted to standby successfully");
        return true;
    }

    /**
     * 获取复制发送器（主机使用）
     */
    ReplicationSender* GetReplicationSender() {
        return replication_sender_.get();
    }

    /**
     * 获取复制接收器（备机使用）
     */
    ReplicationReceiver* GetReplicationReceiver() {
        return replication_receiver_.get();
    }

    /**
     * 获取 HA 统计信息
     */
    struct HAStats {
        HARole role;
        bool peer_alive;
        int64_t last_peer_heartbeat_ns;
        uint64_t term;
        int64_t uptime_as_role_ms;
    };

    HAStats GetStats() const {
        HAStats stats {};
        stats.role = current_role_.load();
        stats.peer_alive = peer_alive_.load();
        stats.last_peer_heartbeat_ns = last_peer_heartbeat_ns_.load();

        if (ha_state_shm_.IsValid()) {
            const HAState* state = ha_state_shm_.Get();
            stats.term = state->term;
            stats.uptime_as_role_ms =
                (GetNanoTimestamp() - state->role_change_time_ns) / 1000000;
        }

        return stats;
    }

private:
    /**
     * 作为主机启动
     */
    void StartAsPrimary() {
        SPDLOG_INFO("Starting as primary");

        // 创建并启动复制发送器
        replication_sender_ = std::make_unique<ReplicationSender>(config_, wal_);
        replication_sender_->Start();
    }

    /**
     * 作为备机启动
     */
    void StartAsStandby() {
        SPDLOG_INFO("Starting as standby");

        // 创建并启动复制接收器
        replication_receiver_ = std::make_unique<ReplicationReceiver>(
            config_, order_manager_);
        replication_receiver_->Start();
    }

    /**
     * 心跳检测循环
     */
    void HeartbeatLoop() {
        SPDLOG_INFO("HeartbeatLoop started");

        while (running_.load()) {
            const int64_t now_ns = GetNanoTimestamp();

            // 更新本地心跳时间
            if (ha_state_shm_.IsValid()) {
                HAState* state = ha_state_shm_.Get();
                state->last_heartbeat_ns = now_ns;
            }

            // 检查对端心跳
            CheckPeerHeartbeat(now_ns);

            std::this_thread::sleep_for(
                std::chrono::milliseconds(config_.heartbeat_interval_ms));
        }

        SPDLOG_INFO("HeartbeatLoop stopped");
    }

    /**
     * 检查对端心跳
     */
    void CheckPeerHeartbeat(int64_t now_ns) {
        // 如果是主机，检查复制连接状态
        if (current_role_.load() == HARole::kPrimary && replication_sender_) {
            const bool connected = replication_sender_->IsConnected();
            if (connected) {
                last_peer_heartbeat_ns_.store(now_ns);
                peer_alive_.store(true);
            } else {
                // 连接断开
                if (peer_alive_.load()) {
                    SPDLOG_WARN("Standby connection lost");
                    peer_alive_.store(false);
                }
            }
        }

        // 如果是备机，检查是否需要接管
        if (current_role_.load() == HARole::kStandby) {
            const int64_t last_heartbeat = last_peer_heartbeat_ns_.load();
            const int64_t timeout_ns =
                static_cast<int64_t>(config_.takeover_timeout_ms) * 1000000;

            if (last_heartbeat > 0 &&
                (now_ns - last_heartbeat) > timeout_ns) {
                // 主机超时，考虑接管
                OnPeerDown();
            }
        }
    }

    /**
     * 对端宕机处理
     */
    void OnPeerDown() {
        if (current_role_.load() != HARole::kStandby) {
            return;
        }

        SPDLOG_WARN("Primary appears to be down, considering takeover...");

        if (config_.auto_failover) {
            SPDLOG_INFO("Auto-failover enabled, promoting to primary");
            PromoteToPrimary();
        } else {
            SPDLOG_INFO("Auto-failover disabled, waiting for manual intervention");
        }
    }

    /**
     * 执行 Reconciliation
     */
    void PerformReconciliation() {
        SPDLOG_INFO("Performing reconciliation after failover...");

        // TODO: 需要注入交易所客户端
        // ReconciliationService reconciliation(order_manager_, exchange_client_);
        // auto result = reconciliation.Reconcile(account_id);

        SPDLOG_INFO("Reconciliation completed (stub)");
    }

    static int64_t GetNanoTimestamp() {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return static_cast<int64_t>(ts.tv_sec) * 1000000000LL +
               static_cast<int64_t>(ts.tv_nsec);
    }

    const HAConfig& config_;
    OrderManager* order_manager_;
    ShmWalRing* wal_;

    zrt::DirectShm<HAState> ha_state_shm_;

    std::atomic<bool> running_;
    std::atomic<HARole> current_role_;
    std::atomic<bool> peer_alive_;
    std::atomic<int64_t> last_peer_heartbeat_ns_;

    std::mutex state_mutex_;

    std::unique_ptr<ReplicationSender> replication_sender_;
    std::unique_ptr<ReplicationReceiver> replication_receiver_;

    std::thread heartbeat_thread_;

    RoleChangeCallback role_change_callback_;
};

}  // namespace gtrade
