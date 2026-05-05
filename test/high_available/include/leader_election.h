#pragma once

#include "ha_types.h"
#include "redis_client.h"
#include <atomic>
#include <thread>
#include <functional>

namespace ha {

/**
 * Leader选举器
 *
 * 使用Redis分布式锁实现Leader选举：
 * 1. 尝试获取锁成为Leader
 * 2. Leader定期续租
 * 3. 续租失败则降级为Follower
 * 4. Follower检测到锁过期则尝试竞选
 *
 * 防脑裂机制：
 * - 使用term（任期号）标识每次选举
 * - 只有持有锁且term最新的节点才是合法Leader
 * - 旧Leader即使网络恢复也无法继续操作（term过期）
 */
class LeaderElection {
public:
    explicit LeaderElection(const HAConfig& config, RedisClient* redis);
    ~LeaderElection();

    // 禁用拷贝
    LeaderElection(const LeaderElection&) = delete;
    LeaderElection& operator=(const LeaderElection&) = delete;

    // 启动/停止选举
    void Start();
    void Stop();

    // 获取当前状态
    bool IsLeader() const { return is_leader_.load(); }
    int64_t GetCurrentTerm() const { return current_term_.load(); }
    NodeRole GetRole() const;
    std::string GetCurrentLeader() const;

    // 注册回调
    // became_leader: true表示成为Leader，false表示失去Leadership
    void SetLeadershipCallback(LeadershipCallback callback);

    // 主动放弃Leadership（用于优雅关闭）
    void StepDown();

    // 获取Leader锁的持有者信息（用于调试）
    struct LeaderInfo {
        std::string node_id;
        int64_t term;
        int64_t acquired_at_ms;
    };
    std::optional<LeaderInfo> GetLeaderInfo() const;

private:
    void ElectionLoop();
    bool TryBecomeLeader();
    bool RenewLeadership();
    void OnBecomeLeader();
    void OnLoseLeadership();
    int64_t IncrementTerm();

    HAConfig config_;
    RedisClient* redis_;

    std::atomic<bool> running_{false};
    std::atomic<bool> is_leader_{false};
    std::atomic<int64_t> current_term_{0};

    std::thread election_thread_;
    LeadershipCallback leadership_callback_;

    // 用于锁释放的Lua脚本（保证原子性）
    static constexpr const char* kReleaseLockScript = R"(
        if redis.call("get", KEYS[1]) == ARGV[1] then
            return redis.call("del", KEYS[1])
        else
            return 0
        end
    )";

    // 用于续租的Lua脚本
    static constexpr const char* kRenewLockScript = R"(
        if redis.call("get", KEYS[1]) == ARGV[1] then
            return redis.call("pexpire", KEYS[1], ARGV[2])
        else
            return 0
        end
    )";
};

}  // namespace ha
