#pragma once

#include "ha_types.h"
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <atomic>
#include <optional>

namespace ha {

/**
 * 本地状态管理器
 *
 * 模拟共享内存的功能：
 * 1. 内存中的订单状态（快速访问）
 * 2. 持久化到本地文件（模拟共享内存的持久性）
 * 3. 支持从文件恢复
 *
 * 在真实系统中，这里应该使用SharedMemory（如DirectShm）
 * 这里用文件模拟，便于在不同进程间验证
 *
 * 用途场景：
 * 1. 程序崩溃但服务器正常：可以从本地文件快速恢复
 * 2. 服务器宕机：本地文件不可用，需要从Redis恢复
 */
class LocalState {
public:
    explicit LocalState(const std::string& node_id, const std::string& data_dir = "/tmp/ha_demo");
    ~LocalState();

    // 禁用拷贝
    LocalState(const LocalState&) = delete;
    LocalState& operator=(const LocalState&) = delete;

    // ============ 订单操作 ============

    // 保存订单到内存和本地文件
    bool SaveOrder(const Order& order);

    // 获取订单
    std::optional<Order> GetOrder(const std::string& order_id);

    // 获取所有订单
    std::unordered_map<std::string, Order> GetAllOrders();

    // 删除订单
    bool DeleteOrder(const std::string& order_id);

    // 更新订单状态
    bool UpdateOrderStatus(const std::string& order_id, OrderStatus status);

    // ============ 持久化 ============

    // 将当前状态持久化到文件
    bool PersistToFile();

    // 从文件恢复状态
    bool RecoverFromFile();

    // 检查本地文件是否可用
    bool IsLocalFileAvailable() const;

    // 获取持久化文件路径
    std::string GetPersistFilePath() const;

    // ============ 元数据 ============

    // 设置/获取最后处理的term
    void SetLastTerm(int64_t term);
    int64_t GetLastTerm() const;

    // 设置/获取最后处理的消息ID
    void SetLastMsgId(int64_t msg_id);
    int64_t GetLastMsgId() const;

    // ============ 统计 ============

    struct LocalStats {
        size_t order_count{0};
        int64_t last_term{0};
        int64_t last_msg_id{0};
        int64_t last_persist_time_ms{0};
    };

    LocalStats GetStats() const;

    // 清空所有数据（用于测试）
    void Clear();

private:
    void EnsureDataDir();

    std::string node_id_;
    std::string data_dir_;
    std::string persist_file_;

    std::unordered_map<std::string, Order> orders_;
    mutable std::mutex mutex_;

    std::atomic<int64_t> last_term_{0};
    std::atomic<int64_t> last_msg_id_{0};
    std::atomic<int64_t> last_persist_time_ms_{0};
};

}  // namespace ha
