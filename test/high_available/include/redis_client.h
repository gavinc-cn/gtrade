#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <mutex>

struct redisContext;

namespace ha {

/**
 * 简单的Redis客户端封装
 * 线程安全（内部加锁）
 */
class RedisClient {
public:
    RedisClient(const std::string& host, int port);
    ~RedisClient();

    // 禁用拷贝
    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    // 连接管理
    bool Connect();
    bool IsConnected() const;
    void Disconnect();
    bool Reconnect();

    // 基础操作
    bool Set(const std::string& key, const std::string& value);
    bool SetEx(const std::string& key, const std::string& value, int ttl_seconds);
    std::optional<std::string> Get(const std::string& key);
    bool Del(const std::string& key);
    bool Exists(const std::string& key);

    // 分布式锁（使用 SET NX PX）
    // 返回是否成功获取锁
    bool TryLock(const std::string& key, const std::string& value, int ttl_ms);
    // 续租（只有持有者能续租）
    bool RenewLock(const std::string& key, const std::string& value, int ttl_ms);
    // 释放锁（只有持有者能释放）
    bool ReleaseLock(const std::string& key, const std::string& value);

    // 列表操作（用于消息队列）
    bool LPush(const std::string& key, const std::string& value);
    bool RPush(const std::string& key, const std::string& value);
    std::optional<std::string> LPop(const std::string& key);
    std::optional<std::string> RPop(const std::string& key);
    std::optional<std::string> BLPop(const std::string& key, int timeout_seconds);
    int64_t LLen(const std::string& key);
    std::vector<std::string> LRange(const std::string& key, int start, int stop);

    // 集合操作（用于去重）
    bool SAdd(const std::string& key, const std::string& member);
    bool SIsMember(const std::string& key, const std::string& member);
    bool SRem(const std::string& key, const std::string& member);

    // Hash操作（用于存储结构化状态）
    bool HSet(const std::string& key, const std::string& field, const std::string& value);
    std::optional<std::string> HGet(const std::string& key, const std::string& field);
    bool HDel(const std::string& key, const std::string& field);
    std::vector<std::pair<std::string, std::string>> HGetAll(const std::string& key);

    // 原子操作
    int64_t Incr(const std::string& key);

    // 发布订阅（简化版，用于通知）
    bool Publish(const std::string& channel, const std::string& message);

private:
    std::string host_;
    int port_;
    redisContext* ctx_{nullptr};
    mutable std::mutex mutex_;

    bool ExecuteCommand(const char* format, ...);
    std::optional<std::string> ExecuteStringCommand(const char* format, ...);
    int64_t ExecuteIntCommand(const char* format, ...);
};

}  // namespace ha
