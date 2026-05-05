#include "message_broker.h"
#include <iostream>
#include <sstream>

namespace ha {

MessageBroker::MessageBroker(const HAConfig& config, RedisClient* redis)
    : config_(config), redis_(redis) {
    // 初始化消息ID计数器（从Redis获取）
    auto counter_key = config_.state_prefix + "msg_counter";
    auto value = redis_->Get(counter_key);
    if (value) {
        msg_id_counter_.store(std::stoll(*value));
    }
}

MessageBroker::~MessageBroker() {
    StopConsumer();
}

int64_t MessageBroker::PushMessage(MessageType type, const std::string& payload) {
    // 生成消息ID
    auto counter_key = config_.state_prefix + "msg_counter";
    int64_t msg_id = redis_->Incr(counter_key);

    // 构造消息
    Message msg;
    msg.msg_id = msg_id;
    msg.type = type;
    msg.payload = payload;
    msg.timestamp_ms = NowMs();
    msg.processed = false;

    // 序列化并推送到队列
    std::string serialized = msg.Serialize();
    if (!redis_->RPush(config_.message_queue_key, serialized)) {
        std::cerr << "[Broker] Failed to push message: " << msg_id << std::endl;
        return -1;
    }

    std::cout << "[Broker] Pushed message " << msg_id
              << ", type=" << static_cast<int>(type)
              << ", queue_len=" << redis_->LLen(config_.message_queue_key) << std::endl;

    return msg_id;
}

int64_t MessageBroker::PushOrderRequest(const Order& order) {
    return PushMessage(MessageType::kOrderRequest, order.Serialize());
}

void MessageBroker::StartConsumer(MessageCallback callback) {
    if (consumer_running_.exchange(true)) {
        return;  // 已经在运行
    }

    message_callback_ = std::move(callback);
    consumer_paused_.store(false);

    consumer_thread_ = std::thread(&MessageBroker::ConsumerLoop, this);

    std::cout << "[Broker] Consumer started" << std::endl;
}

void MessageBroker::StopConsumer() {
    if (!consumer_running_.exchange(false)) {
        return;  // 已经停止
    }

    if (consumer_thread_.joinable()) {
        consumer_thread_.join();
    }

    std::cout << "[Broker] Consumer stopped" << std::endl;
}

void MessageBroker::PauseConsumer() {
    consumer_paused_.store(true);
    std::cout << "[Broker] Consumer paused" << std::endl;
}

void MessageBroker::ResumeConsumer() {
    consumer_paused_.store(false);
    std::cout << "[Broker] Consumer resumed" << std::endl;
}

std::optional<Message> MessageBroker::ConsumeOne() {
    return FetchMessage();
}

bool MessageBroker::AckMessage(int64_t msg_id) {
    // 标记为已处理
    std::string msg_id_str = std::to_string(msg_id);
    redis_->SAdd(config_.processed_set_key, msg_id_str);

    // 从pending中移除
    RemoveFromPending(msg_id);

    std::cout << "[Broker] Acked message " << msg_id << std::endl;
    return true;
}

bool MessageBroker::IsMessageProcessed(int64_t msg_id) {
    return redis_->SIsMember(config_.processed_set_key, std::to_string(msg_id));
}

std::vector<Message> MessageBroker::GetPendingMessages() {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    std::vector<Message> result;

    for (const auto& [id, msg] : local_pending_) {
        result.push_back(msg);
    }

    // 也从Redis的pending hash中获取
    std::string pending_key = config_.message_queue_key + ":pending";
    auto pairs = redis_->HGetAll(pending_key);
    for (const auto& [field, value] : pairs) {
        Message msg = Message::Deserialize(value);
        if (local_pending_.find(msg.msg_id) == local_pending_.end()) {
            result.push_back(msg);
        }
    }

    return result;
}

int MessageBroker::RequeueAllPending() {
    auto pending = GetPendingMessages();
    int count = 0;

    for (const auto& msg : pending) {
        // 检查是否已处理（防止重复）
        if (IsMessageProcessed(msg.msg_id)) {
            continue;
        }

        // 重新放入队列头部（LPUSH）
        std::string serialized = msg.Serialize();
        if (redis_->LPush(config_.message_queue_key, serialized)) {
            count++;
        }
    }

    // 清空pending
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        local_pending_.clear();
    }

    std::string pending_key = config_.message_queue_key + ":pending";
    redis_->Del(pending_key);

    std::cout << "[Broker] Requeued " << count << " pending messages" << std::endl;
    return count;
}

MessageBroker::BrokerStats MessageBroker::GetStats() {
    BrokerStats stats;
    stats.queue_length = redis_->LLen(config_.message_queue_key);

    std::string pending_key = config_.message_queue_key + ":pending";
    auto pairs = redis_->HGetAll(pending_key);
    stats.pending_count = pairs.size();

    // processed_count 需要 SCARD，这里简化
    stats.processed_count = 0;

    return stats;
}

int64_t MessageBroker::GetQueueLength() {
    return redis_->LLen(config_.message_queue_key);
}

void MessageBroker::ConsumerLoop() {
    while (consumer_running_.load()) {
        if (consumer_paused_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        auto msg_opt = FetchMessage();
        if (!msg_opt) {
            // 队列为空，短暂休眠
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        Message& msg = *msg_opt;

        // 检查是否已处理
        if (IsMessageProcessed(msg.msg_id)) {
            std::cout << "[Broker] Skipping already processed message: " << msg.msg_id << std::endl;
            continue;
        }

        // 移到pending
        MoveToPending(msg);

        // 调用回调处理
        if (message_callback_) {
            try {
                message_callback_(msg);
                // 处理成功，确认
                AckMessage(msg.msg_id);
            } catch (const std::exception& e) {
                std::cerr << "[Broker] Error processing message " << msg.msg_id
                          << ": " << e.what() << std::endl;
                // 不确认，保留在pending中
            }
        }
    }
}

std::optional<Message> MessageBroker::FetchMessage() {
    // 使用BLPOP阻塞式获取，超时1秒
    auto value = redis_->BLPop(config_.message_queue_key, 1);
    if (!value) {
        return std::nullopt;
    }

    return Message::Deserialize(*value);
}

void MessageBroker::MoveToPending(const Message& msg) {
    // 本地缓存
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        local_pending_[msg.msg_id] = msg;
    }

    // Redis pending hash
    std::string pending_key = config_.message_queue_key + ":pending";
    redis_->HSet(pending_key, std::to_string(msg.msg_id), msg.Serialize());
}

void MessageBroker::RemoveFromPending(int64_t msg_id) {
    // 本地缓存
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        local_pending_.erase(msg_id);
    }

    // Redis pending hash
    std::string pending_key = config_.message_queue_key + ":pending";
    redis_->HDel(pending_key, std::to_string(msg_id));
}

}  // namespace ha
