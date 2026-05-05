#include "ha_node.h"
#include <iostream>
#include <sstream>

namespace ha {

HANode::HANode(const HAConfig& config)
    : config_(config) {}

HANode::~HANode() {
    Stop();
}

bool HANode::Start() {
    if (running_.exchange(true)) {
        return true;  // 已经在运行
    }

    start_time_ms_.store(NowMs());

    std::cout << "========================================" << std::endl;
    std::cout << "[HANode] Starting node: " << config_.node_id << std::endl;
    std::cout << "========================================" << std::endl;

    // 初始化组件
    if (!InitComponents()) {
        running_.store(false);
        return false;
    }

    // 尝试从本地状态恢复
    if (local_state_->IsLocalFileAvailable()) {
        std::cout << "[HANode] Found local state file, recovering..." << std::endl;
        local_state_->RecoverFromFile();
    }

    // 启动Leader选举
    election_->Start();

    std::cout << "[HANode] Node " << config_.node_id << " started successfully" << std::endl;
    return true;
}

void HANode::Stop() {
    if (!running_.exchange(false)) {
        return;  // 已经停止
    }

    std::cout << "[HANode] Stopping node: " << config_.node_id << std::endl;

    // 停止消息消费
    if (broker_) {
        broker_->StopConsumer();
    }

    // 停止选举
    if (election_) {
        election_->Stop();
    }

    // 持久化本地状态
    if (local_state_) {
        local_state_->PersistToFile();
    }

    std::cout << "[HANode] Node " << config_.node_id << " stopped" << std::endl;
}

bool HANode::InitComponents() {
    // 1. 初始化Redis连接
    redis_ = std::make_unique<RedisClient>(config_.redis_host, config_.redis_port);
    if (!redis_->Connect()) {
        std::cerr << "[HANode] Failed to connect to Redis" << std::endl;
        return false;
    }
    std::cout << "[HANode] Connected to Redis at "
              << config_.redis_host << ":" << config_.redis_port << std::endl;

    // 2. 初始化本地状态
    local_state_ = std::make_unique<LocalState>(config_.node_id);
    std::cout << "[HANode] Local state initialized, file: "
              << local_state_->GetPersistFilePath() << std::endl;

    // 3. 初始化状态存储
    state_store_ = std::make_unique<StateStore>(config_, redis_.get());
    std::cout << "[HANode] State store initialized" << std::endl;

    // 4. 初始化消息代理
    broker_ = std::make_unique<MessageBroker>(config_, redis_.get());
    std::cout << "[HANode] Message broker initialized" << std::endl;

    // 5. 初始化Leader选举
    election_ = std::make_unique<LeaderElection>(config_, redis_.get());
    election_->SetLeadershipCallback(
        [this](bool became_leader, int64_t term) {
            OnLeadershipChange(became_leader, term);
        }
    );
    std::cout << "[HANode] Leader election initialized" << std::endl;

    return true;
}

NodeRole HANode::GetRole() const {
    if (election_) {
        return election_->GetRole();
    }
    return NodeRole::kFollower;
}

bool HANode::IsLeader() const {
    if (election_) {
        return election_->IsLeader();
    }
    return false;
}

int64_t HANode::GetCurrentTerm() const {
    if (election_) {
        return election_->GetCurrentTerm();
    }
    return 0;
}

int64_t HANode::SubmitOrder(const Order& order) {
    // 通过消息队列提交，确保持久化
    return broker_->PushOrderRequest(order);
}

bool HANode::ProcessOrder(const Order& order) {
    // 只有Leader才能处理订单
    if (!IsLeader()) {
        std::cerr << "[HANode] Cannot process order: not a leader" << std::endl;
        return false;
    }

    // 更新订单状态
    Order updated_order = order;
    updated_order.status = OrderStatus::kProcessing;
    updated_order.update_time_ms = NowMs();
    updated_order.processed_by_term = GetCurrentTerm();

    // 保存到Redis（分布式状态）
    if (!state_store_->SaveOrder(updated_order)) {
        std::cerr << "[HANode] Failed to save order to Redis" << std::endl;
        return false;
    }

    // 保存到本地（快速恢复）
    if (!local_state_->SaveOrder(updated_order)) {
        std::cerr << "[HANode] Failed to save order to local state" << std::endl;
        // 不返回失败，Redis已经保存成功
    }

    // 模拟处理
    std::cout << "[HANode] Processing order: " << order.order_id
              << ", symbol=" << order.symbol
              << ", price=" << order.price
              << ", qty=" << order.quantity << std::endl;

    // 标记为完成
    updated_order.status = OrderStatus::kCompleted;
    updated_order.update_time_ms = NowMs();

    state_store_->SaveOrder(updated_order);
    local_state_->SaveOrder(updated_order);

    // 调用业务回调
    if (order_callback_) {
        order_callback_(updated_order);
    }

    return true;
}

std::optional<Order> HANode::GetOrder(const std::string& order_id) {
    // 先查本地
    auto local_order = local_state_->GetOrder(order_id);
    if (local_order) {
        return local_order;
    }

    // 再查Redis
    return state_store_->GetOrder(order_id);
}

std::vector<Order> HANode::GetAllOrders() {
    // 返回本地缓存的订单
    auto local_orders = local_state_->GetAllOrders();
    std::vector<Order> result;
    result.reserve(local_orders.size());

    for (const auto& [id, order] : local_orders) {
        result.push_back(order);
    }

    return result;
}

void HANode::SetRoleChangeCallback(RoleChangeCallback callback) {
    role_change_callback_ = std::move(callback);
}

void HANode::SetOrderCallback(OrderCallback callback) {
    order_callback_ = std::move(callback);
}

HANode::NodeStats HANode::GetStats() const {
    NodeStats stats;
    stats.node_id = config_.node_id;
    stats.role = GetRole();
    stats.current_term = GetCurrentTerm();
    stats.is_healthy = running_.load();

    // 订单统计
    auto local_stats = local_state_->GetStats();
    stats.total_orders = local_stats.order_count;
    stats.pending_orders = 0;  // 需要遍历计算
    stats.completed_orders = 0;

    // 消息统计
    auto broker_stats = broker_->GetStats();
    stats.queue_length = broker_stats.queue_length;
    stats.pending_messages = broker_stats.pending_count;
    stats.processed_messages = broker_stats.processed_count;

    // 时间统计
    stats.uptime_ms = NowMs() - start_time_ms_.load();
    stats.last_heartbeat_ms = 0;  // TODO

    return stats;
}

void HANode::OnLeadershipChange(bool became_leader, int64_t term) {
    NodeRole old_role = became_leader ? NodeRole::kFollower : NodeRole::kLeader;
    NodeRole new_role = became_leader ? NodeRole::kLeader : NodeRole::kFollower;

    std::cout << "========================================" << std::endl;
    std::cout << "[HANode] LEADERSHIP CHANGE: " << config_.node_id << std::endl;
    std::cout << "  Old Role: " << RoleToString(old_role) << std::endl;
    std::cout << "  New Role: " << RoleToString(new_role) << std::endl;
    std::cout << "  Term: " << term << std::endl;
    std::cout << "========================================" << std::endl;

    if (became_leader) {
        OnBecomeLeader(term);
    } else {
        OnLoseLeadership();
    }

    // 调用用户回调
    if (role_change_callback_) {
        role_change_callback_(old_role, new_role);
    }
}

void HANode::OnBecomeLeader(int64_t term) {
    std::cout << "[HANode] Becoming Leader at term " << term << std::endl;

    // 1. 恢复状态
    RecoverState();

    // 2. 重新入队未处理的消息
    int requeued = broker_->RequeueAllPending();
    std::cout << "[HANode] Requeued " << requeued << " pending messages" << std::endl;

    // 3. 启动消息消费
    broker_->StartConsumer([this](const Message& msg) {
        OnMessage(msg);
    });

    // 4. 更新本地状态
    local_state_->SetLastTerm(term);
}

void HANode::OnLoseLeadership() {
    std::cout << "[HANode] Losing Leadership" << std::endl;

    // 停止消息消费
    broker_->PauseConsumer();

    // 持久化当前状态
    local_state_->PersistToFile();
}

bool HANode::RecoverState() {
    std::cout << "[HANode] Recovering state..." << std::endl;

    // 策略：
    // 1. 优先从本地恢复（如果可用且term匹配）
    // 2. 否则从Redis恢复

    int64_t local_term = local_state_->GetLastTerm();
    int64_t redis_term = state_store_->GetCurrentTerm();

    std::cout << "[HANode] Local term: " << local_term
              << ", Redis term: " << redis_term << std::endl;

    if (local_state_->IsLocalFileAvailable() && local_term >= redis_term - 1) {
        // 本地状态较新，直接使用
        std::cout << "[HANode] Using local state for recovery" << std::endl;
        local_state_->RecoverFromFile();
    } else {
        // 需要从Redis恢复
        std::cout << "[HANode] Recovering from Redis..." << std::endl;
        auto orders = state_store_->LoadAllOrders();
        for (const auto& [id, order] : orders) {
            local_state_->SaveOrder(order);
        }
    }

    return true;
}

void HANode::OnMessage(const Message& msg) {
    std::cout << "[HANode] Received message: id=" << msg.msg_id
              << ", type=" << static_cast<int>(msg.type) << std::endl;

    switch (msg.type) {
        case MessageType::kOrderRequest:
            HandleOrderRequest(msg);
            break;

        case MessageType::kOrderCancel:
            // TODO: 处理取消订单
            break;

        case MessageType::kHeartbeat:
            // 忽略心跳
            break;

        case MessageType::kStateSync:
            // TODO: 处理状态同步
            break;
    }
}

void HANode::HandleOrderRequest(const Message& msg) {
    // 反序列化订单
    Order order = Order::Deserialize(msg.payload);

    std::cout << "[HANode] Handling order request: " << order.order_id << std::endl;

    // 处理订单
    if (ProcessOrder(order)) {
        // 更新最后处理的消息ID
        local_state_->SetLastMsgId(msg.msg_id);
    }
}

}  // namespace ha
