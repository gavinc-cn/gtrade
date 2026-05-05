#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include <functional>
#include <atomic>

namespace ha {

// 节点角色
enum class NodeRole {
    kFollower,   // 备节点
    kLeader,     // 主节点
    kCandidate   // 候选（选举中）
};

inline const char* RoleToString(NodeRole role) {
    switch (role) {
        case NodeRole::kFollower: return "FOLLOWER";
        case NodeRole::kLeader: return "LEADER";
        case NodeRole::kCandidate: return "CANDIDATE";
        default: return "UNKNOWN";
    }
}

// 节点状态
struct NodeState {
    std::string node_id;
    NodeRole role{NodeRole::kFollower};
    int64_t term{0};              // 任期号（防止脑裂）
    int64_t last_heartbeat_ms{0}; // 上次心跳时间
    bool is_healthy{true};
};

// 订单状态（模拟业务数据）
enum class OrderStatus {
    kPending,      // 待处理
    kProcessing,   // 处理中
    kCompleted,    // 已完成
    kCancelled     // 已取消
};

inline const char* OrderStatusToString(OrderStatus status) {
    switch (status) {
        case OrderStatus::kPending: return "PENDING";
        case OrderStatus::kProcessing: return "PROCESSING";
        case OrderStatus::kCompleted: return "COMPLETED";
        case OrderStatus::kCancelled: return "CANCELLED";
        default: return "UNKNOWN";
    }
}

// 订单（模拟业务数据）
struct Order {
    std::string order_id;
    std::string symbol;
    double price{0.0};
    double quantity{0.0};
    OrderStatus status{OrderStatus::kPending};
    int64_t create_time_ms{0};
    int64_t update_time_ms{0};
    int64_t processed_by_term{0};  // 被哪个任期的Leader处理

    std::string Serialize() const;
    static Order Deserialize(const std::string& data);
};

// 消息类型
enum class MessageType {
    kOrderRequest,    // 下单请求
    kOrderCancel,     // 取消订单
    kHeartbeat,       // 心跳
    kStateSync,       // 状态同步
};

// 消息
struct Message {
    int64_t msg_id{0};
    MessageType type{MessageType::kOrderRequest};
    std::string payload;
    int64_t timestamp_ms{0};
    bool processed{false};

    std::string Serialize() const;
    static Message Deserialize(const std::string& data);
};

// 回调类型
using LeadershipCallback = std::function<void(bool became_leader, int64_t term)>;
using MessageCallback = std::function<void(const Message& msg)>;

// 工具函数
inline int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

inline std::string GenerateUUID() {
    static std::atomic<uint64_t> counter{0};
    return std::to_string(NowMs()) + "-" + std::to_string(counter++);
}

// 配置
struct HAConfig {
    std::string node_id;
    std::string redis_host{"127.0.0.1"};
    int redis_port{6379};

    int heartbeat_interval_ms{100};      // 心跳间隔
    int election_timeout_ms{500};        // 选举超时（检测主节点故障）
    int lease_ttl_ms{3000};              // 锁租约TTL

    std::string leader_lock_key{"ha:leader:lock"};
    std::string state_prefix{"ha:state:"};
    std::string message_queue_key{"ha:messages"};
    std::string processed_set_key{"ha:processed"};
};

}  // namespace ha
