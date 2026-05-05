#pragma once

#include "ha_types.h"
#include "redis_client.h"
#include "leader_election.h"
#include "state_store.h"
#include "message_broker.h"
#include "local_state.h"
#include <memory>
#include <functional>

namespace ha {

/**
 * 高可用节点
 *
 * 整合所有HA组件，提供完整的主备切换功能
 *
 * 设计要点：
 *
 * 1. 状态恢复优先级：
 *    a) 本地文件（共享内存）- 最快，适用于程序崩溃重启
 *    b) Redis - 适用于服务器宕机后在新机器启动
 *
 * 2. 主备切换流程：
 *    a) 检测到Leader故障（心跳超时）
 *    b) 竞争获取分布式锁
 *    c) 获取成功后，进入状态恢复阶段：
 *       - 从Redis加载最新状态
 *       - 将pending消息重新入队
 *       - 与本地状态对比（如果有）
 *    d) 状态恢复完成后，开始处理新消息
 *
 * 3. 消息不丢失保证：
 *    a) 消息首先持久化到Redis队列
 *    b) Leader取出消息后放入pending
 *    c) 处理完成后才从pending删除
 *    d) 主备切换时，pending消息重新入队
 *
 * 4. 状态一致性保证：
 *    a) 每个操作都带term号
 *    b) 旧term的操作会被拒绝
 *    c) 状态更新同时写Redis和本地
 */
class HANode {
public:
    explicit HANode(const HAConfig& config);
    ~HANode();

    // 禁用拷贝
    HANode(const HANode&) = delete;
    HANode& operator=(const HANode&) = delete;

    // ============ 生命周期 ============

    // 启动节点
    bool Start();

    // 停止节点（优雅关闭）
    void Stop();

    // 是否正在运行
    bool IsRunning() const { return running_.load(); }

    // ============ 状态查询 ============

    // 获取节点ID
    const std::string& GetNodeId() const { return config_.node_id; }

    // 获取当前角色
    NodeRole GetRole() const;

    // 是否是Leader
    bool IsLeader() const;

    // 获取当前term
    int64_t GetCurrentTerm() const;

    // ============ 业务操作（仅Leader可执行）============

    // 提交订单（通过消息队列）
    // 注意：这是上游调用的接口，会通过消息队列处理
    int64_t SubmitOrder(const Order& order);

    // 直接处理订单（仅内部使用）
    bool ProcessOrder(const Order& order);

    // 获取订单状态
    std::optional<Order> GetOrder(const std::string& order_id);

    // 获取所有订单
    std::vector<Order> GetAllOrders();

    // ============ 回调注册 ============

    // 角色变更回调
    using RoleChangeCallback = std::function<void(NodeRole old_role, NodeRole new_role)>;
    void SetRoleChangeCallback(RoleChangeCallback callback);

    // 订单处理回调（用于业务逻辑）
    using OrderCallback = std::function<void(const Order& order)>;
    void SetOrderCallback(OrderCallback callback);

    // ============ 统计和调试 ============

    struct NodeStats {
        std::string node_id;
        NodeRole role;
        int64_t current_term;
        bool is_healthy;

        // 订单统计
        int64_t total_orders;
        int64_t pending_orders;
        int64_t completed_orders;

        // 消息统计
        int64_t queue_length;
        int64_t pending_messages;
        int64_t processed_messages;

        // 时间统计
        int64_t uptime_ms;
        int64_t last_heartbeat_ms;
    };

    NodeStats GetStats() const;

    // 获取各组件指针（用于调试）
    RedisClient* GetRedisClient() { return redis_.get(); }
    LeaderElection* GetLeaderElection() { return election_.get(); }
    StateStore* GetStateStore() { return state_store_.get(); }
    MessageBroker* GetMessageBroker() { return broker_.get(); }
    LocalState* GetLocalState() { return local_state_.get(); }

private:
    // 初始化各组件
    bool InitComponents();

    // Leader选举回调
    void OnLeadershipChange(bool became_leader, int64_t term);

    // 成为Leader时的处理
    void OnBecomeLeader(int64_t term);

    // 失去Leadership时的处理
    void OnLoseLeadership();

    // 状态恢复
    bool RecoverState();

    // 消息处理回调
    void OnMessage(const Message& msg);

    // 处理订单请求消息
    void HandleOrderRequest(const Message& msg);

    HAConfig config_;

    // 组件
    std::unique_ptr<RedisClient> redis_;
    std::unique_ptr<LeaderElection> election_;
    std::unique_ptr<StateStore> state_store_;
    std::unique_ptr<MessageBroker> broker_;
    std::unique_ptr<LocalState> local_state_;

    // 状态
    std::atomic<bool> running_{false};
    std::atomic<int64_t> start_time_ms_{0};

    // 回调
    RoleChangeCallback role_change_callback_;
    OrderCallback order_callback_;
};

}  // namespace ha
