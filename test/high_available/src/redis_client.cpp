#include "redis_client.h"
#include <hiredis/hiredis.h>
#include <cstdarg>
#include <cstring>
#include <iostream>

namespace ha {

RedisClient::RedisClient(const std::string& host, int port)
    : host_(host), port_(port) {}

RedisClient::~RedisClient() {
    Disconnect();
}

bool RedisClient::Connect() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }

    struct timeval timeout = {1, 500000};  // 1.5 seconds
    ctx_ = redisConnectWithTimeout(host_.c_str(), port_, timeout);

    if (!ctx_ || ctx_->err) {
        if (ctx_) {
            std::cerr << "Redis connection error: " << ctx_->errstr << std::endl;
            redisFree(ctx_);
            ctx_ = nullptr;
        } else {
            std::cerr << "Redis connection error: can't allocate context" << std::endl;
        }
        return false;
    }

    return true;
}

bool RedisClient::IsConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ctx_ && !ctx_->err;
}

void RedisClient::Disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
}

bool RedisClient::Reconnect() {
    Disconnect();
    return Connect();
}

bool RedisClient::Set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SET %s %s", key.c_str(), value.c_str())
    );

    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_STATUS &&
                    strcmp(reply->str, "OK") == 0);
    freeReplyObject(reply);
    return success;
}

bool RedisClient::SetEx(const std::string& key, const std::string& value, int ttl_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SETEX %s %d %s", key.c_str(), ttl_seconds, value.c_str())
    );

    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_STATUS &&
                    strcmp(reply->str, "OK") == 0);
    freeReplyObject(reply);
    return success;
}

std::optional<std::string> RedisClient::Get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return std::nullopt;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "GET %s", key.c_str())
    );

    if (!reply) return std::nullopt;

    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }

    freeReplyObject(reply);
    return result;
}

bool RedisClient::Del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "DEL %s", key.c_str())
    );

    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER && reply->integer >= 0);
    freeReplyObject(reply);
    return success;
}

bool RedisClient::Exists(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "EXISTS %s", key.c_str())
    );

    if (!reply) return false;

    bool exists = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    freeReplyObject(reply);
    return exists;
}

bool RedisClient::TryLock(const std::string& key, const std::string& value, int ttl_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    // SET key value NX PX ttl_ms
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SET %s %s NX PX %d", key.c_str(), value.c_str(), ttl_ms)
    );

    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_STATUS &&
                    strcmp(reply->str, "OK") == 0);
    freeReplyObject(reply);
    return success;
}

bool RedisClient::RenewLock(const std::string& key, const std::string& value, int ttl_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    // 使用Lua脚本保证原子性：只有持有者才能续租
    const char* script = R"(
        if redis.call("get", KEYS[1]) == ARGV[1] then
            return redis.call("pexpire", KEYS[1], ARGV[2])
        else
            return 0
        end
    )";

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "EVAL %s 1 %s %s %d",
                     script, key.c_str(), value.c_str(), ttl_ms)
    );

    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    return success;
}

bool RedisClient::ReleaseLock(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    // 使用Lua脚本保证原子性：只有持有者才能释放
    const char* script = R"(
        if redis.call("get", KEYS[1]) == ARGV[1] then
            return redis.call("del", KEYS[1])
        else
            return 0
        end
    )";

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "EVAL %s 1 %s %s",
                     script, key.c_str(), value.c_str())
    );

    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    return success;
}

bool RedisClient::LPush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "LPUSH %s %s", key.c_str(), value.c_str())
    );

    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    freeReplyObject(reply);
    return success;
}

bool RedisClient::RPush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "RPUSH %s %s", key.c_str(), value.c_str())
    );

    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    freeReplyObject(reply);
    return success;
}

std::optional<std::string> RedisClient::LPop(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return std::nullopt;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "LPOP %s", key.c_str())
    );

    if (!reply) return std::nullopt;

    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }

    freeReplyObject(reply);
    return result;
}

std::optional<std::string> RedisClient::RPop(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return std::nullopt;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "RPOP %s", key.c_str())
    );

    if (!reply) return std::nullopt;

    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }

    freeReplyObject(reply);
    return result;
}

std::optional<std::string> RedisClient::BLPop(const std::string& key, int timeout_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return std::nullopt;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "BLPOP %s %d", key.c_str(), timeout_seconds)
    );

    if (!reply) return std::nullopt;

    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 2) {
        // BLPOP返回 [key, value]
        result = std::string(reply->element[1]->str, reply->element[1]->len);
    }

    freeReplyObject(reply);
    return result;
}

int64_t RedisClient::LLen(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return -1;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "LLEN %s", key.c_str())
    );

    if (!reply) return -1;

    int64_t len = -1;
    if (reply->type == REDIS_REPLY_INTEGER) {
        len = reply->integer;
    }

    freeReplyObject(reply);
    return len;
}

std::vector<std::string> RedisClient::LRange(const std::string& key, int start, int stop) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;

    if (!ctx_) return result;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "LRANGE %s %d %d", key.c_str(), start, stop)
    );

    if (!reply) return result;

    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            if (reply->element[i]->type == REDIS_REPLY_STRING) {
                result.emplace_back(reply->element[i]->str, reply->element[i]->len);
            }
        }
    }

    freeReplyObject(reply);
    return result;
}

bool RedisClient::SAdd(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SADD %s %s", key.c_str(), member.c_str())
    );

    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);
    return success;
}

bool RedisClient::SIsMember(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SISMEMBER %s %s", key.c_str(), member.c_str())
    );

    if (!reply) return false;

    bool is_member = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    return is_member;
}

bool RedisClient::SRem(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SREM %s %s", key.c_str(), member.c_str())
    );

    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);
    return success;
}

bool RedisClient::HSet(const std::string& key, const std::string& field, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "HSET %s %s %s", key.c_str(), field.c_str(), value.c_str())
    );

    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);
    return success;
}

std::optional<std::string> RedisClient::HGet(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return std::nullopt;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "HGET %s %s", key.c_str(), field.c_str())
    );

    if (!reply) return std::nullopt;

    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }

    freeReplyObject(reply);
    return result;
}

bool RedisClient::HDel(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "HDEL %s %s", key.c_str(), field.c_str())
    );

    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);
    return success;
}

std::vector<std::pair<std::string, std::string>> RedisClient::HGetAll(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, std::string>> result;

    if (!ctx_) return result;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "HGETALL %s", key.c_str())
    );

    if (!reply) return result;

    if (reply->type == REDIS_REPLY_ARRAY && reply->elements % 2 == 0) {
        for (size_t i = 0; i < reply->elements; i += 2) {
            std::string field(reply->element[i]->str, reply->element[i]->len);
            std::string value(reply->element[i + 1]->str, reply->element[i + 1]->len);
            result.emplace_back(std::move(field), std::move(value));
        }
    }

    freeReplyObject(reply);
    return result;
}

int64_t RedisClient::Incr(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return -1;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "INCR %s", key.c_str())
    );

    if (!reply) return -1;

    int64_t value = -1;
    if (reply->type == REDIS_REPLY_INTEGER) {
        value = reply->integer;
    }

    freeReplyObject(reply);
    return value;
}

bool RedisClient::Publish(const std::string& channel, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "PUBLISH %s %s", channel.c_str(), message.c_str())
    );

    if (!reply) return false;

    bool success = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);
    return success;
}

}  // namespace ha
