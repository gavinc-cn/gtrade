#pragma once

#include "ha_types.h"
#include "redis_client.h"
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace ha {

/**
 * 消息代理
 *
 * 职责：
 * 1. 接收上游消息并持久化到Redis队列
 * 2. Leader从队列消费消息并处理
 * 3. 记录已处理的消息ID（防止重复处理）
 * 4. 支持消息确认机制（处理完成后才从队列移除）
 *
 * 消息流程：
 * 1. 上游 -> PushMessage() -> Redis队列
 * 2. Leader -> ConsumeMessage() -> 处理
 * 3. Leader -> AckMessage() -> 标记已处理
 *
 * 主备切换时：
 * - 未确认的消息会被新Leader重新处理
 * - 已确认的消息通过processed集合去重
 *
 * Redis数据结构：
 * - ha:messages -> List（消息队列，FIFO）
 * - ha:messages:pending -> Hash（正在处理的消息）
 * - ha:processed -> Set（已处理的消息ID，用于去重）
 */
class MessageBroker {
public:
    explicit MessageBroker(const HAConfig& config, RedisClient* redis);
    ~MessageBroker();

    // 禁用拷贝
    MessageBroker(const MessageBroker&) = delete;
    MessageBroker& operator=(const MessageBroker&) = delete;

    // ============ 生产者API（上游使用）============

    // 推送消息到队列
    // 返回分配的消息ID
    int64_t PushMessage(MessageType type, const std::string& payload);

    // 推送订单请求
    int64_t PushOrderRequest(const Order& order);

    // ============ 消费者API（Leader使用）============

    // 启动消费者线程
    void StartConsumer(MessageCallback callback);

    // 停止消费者线程
    void StopConsumer();

    // 暂停/恢复消费（主备切换时使用）
    void PauseConsumer();
    void ResumeConsumer();
    bool IsConsumerRunning() const { return consumer_running_.load(); }

    // 手动消费一条消息（用于测试）
    std::optional<Message> ConsumeOne();

    // 确认消息已处理
    bool AckMessage(int64_t msg_id);

    // 检查消息是否已处理
    bool IsMessageProcessed(int64_t msg_id);

    // ============ 恢复API（主备切换时使用）============

    // 获取所有未确认的消息（pending中的）
    std::vector<Message> GetPendingMessages();

    // 将pending消息重新放回队列头部
    int RequeueAllPending();

    // ============ 统计 ============

    struct BrokerStats {
        int64_t queue_length{0};
        int64_t pending_count{0};
        int64_t processed_count{0};
        int64_t total_pushed{0};
        int64_t total_consumed{0};
    };

    BrokerStats GetStats();

    // 获取队列长度
    int64_t GetQueueLength();

private:
    void ConsumerLoop();
    std::optional<Message> FetchMessage();
    void MoveToPending(const Message& msg);
    void RemoveFromPending(int64_t msg_id);

    HAConfig config_;
    RedisClient* redis_;

    std::atomic<bool> consumer_running_{false};
    std::atomic<bool> consumer_paused_{false};
    std::thread consumer_thread_;
    MessageCallback message_callback_;

    // 消息ID生成
    std::atomic<int64_t> msg_id_counter_{0};

    // 本地缓存：正在处理的消息
    std::unordered_map<int64_t, Message> local_pending_;
    std::mutex pending_mutex_;
};

}  // namespace ha
