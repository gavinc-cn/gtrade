#pragma once

#include "ha_types.h"
#include "redis_client.h"
#include <vector>
#include <unordered_map>
#include <mutex>

namespace ha {

/**
 * 分布式状态存储
 *
 * 职责：
 * 1. 持久化订单状态到Redis（跨节点共享）
 * 2. 支持状态查询和恢复
 * 3. 保证状态更新的幂等性
 *
 * 状态存储结构（Redis）：
 * - ha:state:orders:{order_id} -> Order序列化数据
 * - ha:state:orders:index -> Set of order_ids
 * - ha:state:term -> 当前term号
 */
class StateStore {
public:
    explicit StateStore(const HAConfig& config, RedisClient* redis);
    ~StateStore() = default;

    // 禁用拷贝
    StateStore(const StateStore&) = delete;
    StateStore& operator=(const StateStore&) = delete;

    // ============ 订单状态操作 ============

    // 保存订单（幂等，相同order_id覆盖）
    bool SaveOrder(const Order& order);

    // 获取订单
    std::optional<Order> GetOrder(const std::string& order_id);

    // 获取所有订单
    std::vector<Order> GetAllOrders();

    // 获取指定状态的订单
    std::vector<Order> GetOrdersByStatus(OrderStatus status);

    // 删除订单
    bool DeleteOrder(const std::string& order_id);

    // ============ 批量操作 ============

    // 保存多个订单（事务）
    bool SaveOrders(const std::vector<Order>& orders);

    // 加载所有订单到本地缓存
    std::unordered_map<std::string, Order> LoadAllOrders();

    // ============ Term管理 ============

    // 获取当前term
    int64_t GetCurrentTerm();

    // 设置当前term
    bool SetCurrentTerm(int64_t term);

    // 原子递增term并返回新值
    int64_t IncrementTerm();

    // ============ 状态统计 ============

    struct StateStats {
        int64_t total_orders{0};
        int64_t pending_orders{0};
        int64_t processing_orders{0};
        int64_t completed_orders{0};
        int64_t cancelled_orders{0};
    };

    StateStats GetStats();

    // ============ 清理 ============

    // 清理已完成的订单（保留最近N条）
    int CleanupCompletedOrders(int keep_count);

private:
    std::string OrderKey(const std::string& order_id) const;

    HAConfig config_;
    RedisClient* redis_;
};

}  // namespace ha
