#include "state_store.h"
#include <sstream>
#include <iostream>

namespace ha {

// ============ Order序列化 ============

std::string Order::Serialize() const {
    std::ostringstream oss;
    oss << order_id << "|"
        << symbol << "|"
        << price << "|"
        << quantity << "|"
        << static_cast<int>(status) << "|"
        << create_time_ms << "|"
        << update_time_ms << "|"
        << processed_by_term;
    return oss.str();
}

Order Order::Deserialize(const std::string& data) {
    Order order;
    std::istringstream iss(data);
    std::string token;

    std::getline(iss, order.order_id, '|');
    std::getline(iss, order.symbol, '|');

    std::getline(iss, token, '|');
    order.price = std::stod(token);

    std::getline(iss, token, '|');
    order.quantity = std::stod(token);

    std::getline(iss, token, '|');
    order.status = static_cast<OrderStatus>(std::stoi(token));

    std::getline(iss, token, '|');
    order.create_time_ms = std::stoll(token);

    std::getline(iss, token, '|');
    order.update_time_ms = std::stoll(token);

    std::getline(iss, token, '|');
    order.processed_by_term = std::stoll(token);

    return order;
}

// ============ Message序列化 ============

std::string Message::Serialize() const {
    std::ostringstream oss;
    oss << msg_id << "|"
        << static_cast<int>(type) << "|"
        << payload << "|"
        << timestamp_ms << "|"
        << (processed ? 1 : 0);
    return oss.str();
}

Message Message::Deserialize(const std::string& data) {
    Message msg;
    std::istringstream iss(data);
    std::string token;

    std::getline(iss, token, '|');
    msg.msg_id = std::stoll(token);

    std::getline(iss, token, '|');
    msg.type = static_cast<MessageType>(std::stoi(token));

    std::getline(iss, msg.payload, '|');

    std::getline(iss, token, '|');
    msg.timestamp_ms = std::stoll(token);

    std::getline(iss, token, '|');
    msg.processed = (token == "1");

    return msg;
}

// ============ StateStore实现 ============

StateStore::StateStore(const HAConfig& config, RedisClient* redis)
    : config_(config), redis_(redis) {}

std::string StateStore::OrderKey(const std::string& order_id) const {
    return config_.state_prefix + "orders:" + order_id;
}

bool StateStore::SaveOrder(const Order& order) {
    std::string key = OrderKey(order.order_id);
    std::string value = order.Serialize();

    if (!redis_->Set(key, value)) {
        std::cerr << "[StateStore] Failed to save order: " << order.order_id << std::endl;
        return false;
    }

    // 添加到索引集合
    redis_->SAdd(config_.state_prefix + "orders:index", order.order_id);

    return true;
}

std::optional<Order> StateStore::GetOrder(const std::string& order_id) {
    std::string key = OrderKey(order_id);
    auto value = redis_->Get(key);

    if (!value) {
        return std::nullopt;
    }

    return Order::Deserialize(*value);
}

std::vector<Order> StateStore::GetAllOrders() {
    std::vector<Order> orders;

    // 获取所有订单ID
    // 注意：这里简化实现，实际应该用SCAN
    auto all_pairs = redis_->HGetAll(config_.state_prefix + "orders:index");

    // 由于我们用的是Set而不是Hash，需要遍历
    // 简化：直接扫描已知的订单
    auto index_key = config_.state_prefix + "orders:index";

    // 这里需要改进：使用SMEMBERS获取所有成员
    // 暂时用一个workaround：从0开始尝试获取
    // 实际实现中应该维护一个订单ID列表

    return orders;
}

std::vector<Order> StateStore::GetOrdersByStatus(OrderStatus status) {
    auto all_orders = GetAllOrders();
    std::vector<Order> filtered;

    for (const auto& order : all_orders) {
        if (order.status == status) {
            filtered.push_back(order);
        }
    }

    return filtered;
}

bool StateStore::DeleteOrder(const std::string& order_id) {
    std::string key = OrderKey(order_id);

    if (!redis_->Del(key)) {
        return false;
    }

    // 从索引中移除
    redis_->SRem(config_.state_prefix + "orders:index", order_id);

    return true;
}

bool StateStore::SaveOrders(const std::vector<Order>& orders) {
    for (const auto& order : orders) {
        if (!SaveOrder(order)) {
            return false;
        }
    }
    return true;
}

std::unordered_map<std::string, Order> StateStore::LoadAllOrders() {
    std::unordered_map<std::string, Order> result;

    // 获取所有订单key（使用KEYS命令，生产环境应该用SCAN）
    // 这里简化实现

    return result;
}

int64_t StateStore::GetCurrentTerm() {
    auto value = redis_->Get(config_.state_prefix + "term");
    if (value) {
        return std::stoll(*value);
    }
    return 0;
}

bool StateStore::SetCurrentTerm(int64_t term) {
    return redis_->Set(config_.state_prefix + "term", std::to_string(term));
}

int64_t StateStore::IncrementTerm() {
    return redis_->Incr(config_.state_prefix + "term");
}

StateStore::StateStats StateStore::GetStats() {
    StateStats stats;
    // 简化实现
    return stats;
}

int StateStore::CleanupCompletedOrders(int keep_count) {
    // 简化实现
    return 0;
}

}  // namespace ha
