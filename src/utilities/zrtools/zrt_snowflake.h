//
// Created by dell on 2025/12/2.
//
// 雪花算法高级模板化实现 - 使用spdlog + 三档时钟回拨处理
//

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <memory>
#include <spdlog/spdlog.h>

/**
 * 高级模板化雪花算法生成器
 *
 * @tparam ServerIdBits         服务器ID位数（默认8位，支持0-255）
 * @tparam SequenceBits         序列号位数（默认14位，每毫秒16384个ID）
 * @tparam TimestampBits        时间戳位数（默认41位，可用69年）
 * @tparam ToleranceMs          小幅回拨容忍阈值（毫秒，默认5ms）
 * @tparam WaitThresholdMs      等待阈值（毫秒，默认10000ms=10s）
 * @tparam ErrorThresholdMs     报错阈值（毫秒，默认10000ms=10s，与WaitThresholdMs相同则不报错）
 *
 * 时钟回拨处理策略：
 * - drift < ToleranceMs       : 容忍，继续使用上次时间戳
 * - ToleranceMs <= drift < WaitThresholdMs : 等待时钟追上
 * - drift >= ErrorThresholdMs : 抛出异常
 */

namespace zrt {

template<
    uint8_t ServerIdBits = 8,
    uint8_t SequenceBits = 14,
    uint8_t TimestampBits = 41,
    uint64_t ToleranceMs = 5,
    uint64_t WaitThresholdMs = 10000,
    uint64_t ErrorThresholdMs = 10000
>
class Snowflake {
public:
    // 编译期位数验证
    static_assert(ServerIdBits + SequenceBits + TimestampBits == 63,
                  "Total bits must equal 63 (plus 1 sign bit = 64)");
    static_assert(ServerIdBits > 0 && ServerIdBits <= 16,
                  "ServerIdBits must be in range [1, 16]");
    static_assert(SequenceBits > 0 && SequenceBits <= 22,
                  "SequenceBits must be in range [1, 22]");
    static_assert(TimestampBits > 0 && TimestampBits <= 50,
                  "TimestampBits must be in range [1, 50]");
    static_assert(ToleranceMs <= WaitThresholdMs,
                  "ToleranceMs must be <= WaitThresholdMs");
    static_assert(WaitThresholdMs <= ErrorThresholdMs,
                  "WaitThresholdMs must be <= ErrorThresholdMs");

    // 配置常量（编译期计算）- 使用枚举以避免C++14链接问题
    enum : uint8_t {
        SERVER_ID_BITS    = ServerIdBits,
        SEQUENCE_BITS     = SequenceBits,
        TIMESTAMP_BITS    = TimestampBits,
        SERVER_ID_SHIFT   = SEQUENCE_BITS,
        TIMESTAMP_SHIFT   = SERVER_ID_SHIFT + SERVER_ID_BITS
    };

    enum : uint64_t {
        TOLERANCE_MS        = ToleranceMs,
        WAIT_THRESHOLD_MS   = WaitThresholdMs,
        ERROR_THRESHOLD_MS  = ErrorThresholdMs,
        MAX_SERVER_ID       = (1ULL << SERVER_ID_BITS) - 1,
        MAX_SEQUENCE        = (1ULL << SEQUENCE_BITS) - 1,
        MAX_TIMESTAMP       = (1ULL << TIMESTAMP_BITS) - 1,
        // 默认纪元时间：2024-01-01 00:00:00 UTC (毫秒)
        DEFAULT_EPOCH       = 1704067200000ULL
    };

    // 构造函数
    explicit Snowflake(const uint64_t server_id, const uint64_t epoch_ms = DEFAULT_EPOCH)
        : server_id_(server_id), epoch_ms_(epoch_ms)
    {
        if (server_id_ > MAX_SERVER_ID) {
            throw std::invalid_argument(
                "server_id must be in range [0, " + std::to_string(MAX_SERVER_ID) + "]"
            );
        }

        state_.store(0, std::memory_order_relaxed);

        // 使用 spdlog 输出初始化信息
        SPDLOG_INFO("===== Snowflake Generator Initialized =====");
        SPDLOG_INFO("Server ID       : {}", server_id_);
        SPDLOG_INFO("Server ID Bits  : {} (range: 0-{})", (int)SERVER_ID_BITS, MAX_SERVER_ID);
        SPDLOG_INFO("Sequence Bits   : {} (capacity: {} IDs/ms)", (int)SEQUENCE_BITS, MAX_SEQUENCE + 1);
        SPDLOG_INFO("Timestamp Bits  : {} (max: {} ms)", (int)TIMESTAMP_BITS, MAX_TIMESTAMP);
        SPDLOG_INFO("Epoch           : {} ({})", epoch_ms_, formatEpoch(epoch_ms_));
        SPDLOG_INFO("--- Clock Drift Policy ---");
        SPDLOG_INFO("Tolerance       : {} ms (tolerate small drift)", TOLERANCE_MS);
        SPDLOG_INFO("Wait Threshold  : {} ms (wait for clock)", WAIT_THRESHOLD_MS);
        SPDLOG_INFO("Error Threshold : {} ms (throw exception)", ERROR_THRESHOLD_MS);
        SPDLOG_INFO("==========================================");
    }

    // 生成下一个ID（无锁实现）
    uint64_t nextId() {
        while (true) {
            uint64_t now = getCurrentMillis();
            uint64_t old_state = state_.load(std::memory_order_acquire);

            // 解析旧状态
            uint64_t last_timestamp = old_state >> SEQUENCE_BITS;
            uint64_t sequence = old_state & MAX_SEQUENCE;

            uint64_t timestamp = now;
            uint64_t new_sequence;

            // 三档时钟回拨处理
            if (timestamp < last_timestamp) {
                uint64_t drift = last_timestamp - timestamp;

                if (drift < TOLERANCE_MS) {
                    // 第一档：小幅回拨，容忍
                    if (drift > 0 && drift_count_.fetch_add(1, std::memory_order_relaxed) % 100 == 0) {
                        SPDLOG_TRACE("Clock drift tolerated: {} ms (< {} ms threshold)",
                                      drift, TOLERANCE_MS);
                    }
                    timestamp = last_timestamp;
                } else if (drift < WAIT_THRESHOLD_MS) {
                    // 第二档：中等回拨，等待
                    if (drift > 100) {  // 只记录较大的等待
                        SPDLOG_WARN("Clock drift detected: {} ms, waiting for clock to catch up...", drift);
                    }
                    wait_count_.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::yield();
                    continue;
                } else if (drift < ERROR_THRESHOLD_MS) {
                    // 在等待阈值和错误阈值之间：严重警告但仍等待
                    SPDLOG_ERROR("SEVERE clock drift: {} ms (>= {} ms), still waiting...",
                                  drift, WAIT_THRESHOLD_MS);
                    severe_drift_count_.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                } else {
                    // 第三档：大幅回拨，报错
                    error_count_.fetch_add(1, std::memory_order_relaxed);
                    SPDLOG_CRITICAL("FATAL clock drift: {} ms (>= {} ms), refusing to generate ID!",
                                     drift, ERROR_THRESHOLD_MS);
                    throw std::runtime_error(
                        "Clock moved backwards by " + std::to_string(drift) +
                        " ms (>= " + std::to_string(ERROR_THRESHOLD_MS) + " ms threshold)"
                    );
                }
            }

            // 同一毫秒内
            if (timestamp == last_timestamp) {
                new_sequence = (sequence + 1) & MAX_SEQUENCE;

                // 序列号溢出，等待下一毫秒
                if (new_sequence == 0) {
                    if (overflow_count_.fetch_add(1, std::memory_order_relaxed) % 100 == 0) {
                        SPDLOG_DEBUG("Sequence overflow, waiting for next millisecond");
                    }
                    timestamp = waitNextMillis(timestamp);
                    new_sequence = 0;
                }
            } else {
                // 新的毫秒，序列号重置为0
                new_sequence = 0;
            }

            // 构造新状态
            uint64_t new_state = (timestamp << SEQUENCE_BITS) | new_sequence;

            // CAS更新状态
            if (state_.compare_exchange_weak(old_state, new_state,
                                            std::memory_order_release,
                                            std::memory_order_acquire))
            {
                // 成功，构造最终ID
                uint64_t time_diff = timestamp - epoch_ms_;

                // 检查时间戳是否溢出
                if (time_diff > MAX_TIMESTAMP) {
                    SPDLOG_CRITICAL("Timestamp overflow! time_diff={} > MAX_TIMESTAMP={}",
                                     time_diff, MAX_TIMESTAMP);
                    throw std::runtime_error(
                        "Timestamp overflow! Please update epoch or increase TIMESTAMP_BITS"
                    );
                }

                id_count_.fetch_add(1, std::memory_order_relaxed);

                return (time_diff << TIMESTAMP_SHIFT) |
                       (server_id_ << SERVER_ID_SHIFT) |
                       new_sequence;
            }

            // CAS失败，重试
            cas_fail_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // 解析ID
    struct ParsedId {
        uint64_t timestamp_ms;  // 真实时间戳（含epoch）
        uint64_t server_id;
        uint64_t sequence;
    };

    ParsedId parseId(uint64_t id) const {
        ParsedId result;
        result.sequence = id & MAX_SEQUENCE;
        result.server_id = (id >> SERVER_ID_SHIFT) & MAX_SERVER_ID;
        uint64_t time_diff = (id >> TIMESTAMP_SHIFT);
        result.timestamp_ms = time_diff + epoch_ms_;
        return result;
    }

    // 获取配置信息
    uint64_t getServerId() const { return server_id_; }
    uint64_t getEpoch() const { return epoch_ms_; }

    // 获取最大可用年限（从epoch开始）
    double getMaxYears() const {
        uint64_t max_ms = MAX_TIMESTAMP;
        return max_ms / 1000.0 / 60.0 / 60.0 / 24.0 / 365.25;
    }

    // 打印配置摘要
    void printConfig() const {
        SPDLOG_INFO("=== Snowflake Configuration ===");
        SPDLOG_INFO("Bit Layout: [sign:1][timestamp:{}][server_id:{}][sequence:{}]",
                     (int)TIMESTAMP_BITS, (int)SERVER_ID_BITS, (int)SEQUENCE_BITS);
        SPDLOG_INFO("Server ID Range     : 0 - {}", MAX_SERVER_ID);
        SPDLOG_INFO("IDs per Millisecond : {}", MAX_SEQUENCE + 1);
        SPDLOG_INFO("Max Years from Epoch: {:.2f} years", getMaxYears());
        SPDLOG_INFO("Clock Tolerance     : {} ms", TOLERANCE_MS);
        SPDLOG_INFO("Wait Threshold      : {} ms", WAIT_THRESHOLD_MS);
        SPDLOG_INFO("Error Threshold     : {} ms", ERROR_THRESHOLD_MS);
        SPDLOG_INFO("==============================");
    }

    // 打印统计信息
    void printStats() const {
        SPDLOG_INFO("=== Snowflake Statistics ===");
        SPDLOG_INFO("Total IDs generated    : {}", id_count_.load());
        SPDLOG_INFO("CAS failures           : {}", cas_fail_count_.load());
        SPDLOG_INFO("Sequence overflows     : {}", overflow_count_.load());
        SPDLOG_INFO("Clock drifts (tolerated): {}", drift_count_.load());
        SPDLOG_INFO("Wait events            : {}", wait_count_.load());
        SPDLOG_INFO("Severe drifts          : {}", severe_drift_count_.load());
        SPDLOG_INFO("Error events           : {}", error_count_.load());
        SPDLOG_INFO("============================");
    }

    // 重置统计信息
    void resetStats() {
        id_count_.store(0);
        cas_fail_count_.store(0);
        overflow_count_.store(0);
        drift_count_.store(0);
        wait_count_.store(0);
        severe_drift_count_.store(0);
        error_count_.store(0);
        SPDLOG_INFO("Statistics reset");
    }

private:
    // 获取当前毫秒时间戳
    uint64_t getCurrentMillis() const {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()
        ).count();
    }

    // 等待直到下一毫秒
    uint64_t waitNextMillis(uint64_t last_timestamp) const {
        uint64_t timestamp = getCurrentMillis();
        while (timestamp <= last_timestamp) {
            timestamp = getCurrentMillis();
        }
        return timestamp;
    }

    // 格式化epoch时间
    std::string formatEpoch(uint64_t epoch_ms) const {
        time_t seconds = epoch_ms / 1000;
        char buffer[64];
        struct tm* tm_info = gmtime(&seconds);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", tm_info);
        return std::string(buffer);
    }

    const uint64_t server_id_;   // 服务器ID
    const uint64_t epoch_ms_;    // 纪元时间

    // 原子状态：高位=时间戳(相对于纪元), 低位=序列号
    std::atomic<uint64_t> state_;

    // 统计信息
    mutable std::atomic<uint64_t> id_count_{0};
    mutable std::atomic<uint64_t> cas_fail_count_{0};
    mutable std::atomic<uint64_t> overflow_count_{0};
    mutable std::atomic<uint64_t> drift_count_{0};
    mutable std::atomic<uint64_t> wait_count_{0};
    mutable std::atomic<uint64_t> severe_drift_count_{0};
    mutable std::atomic<uint64_t> error_count_{0};
};

// ============================================
// 类型别名：常用配置
// ============================================

// 标准配置：容忍5ms，等待10s，超过10s报错
using SnowflakeStandardV2 = Snowflake<8, 14, 41, 5, 10000, 10000>;

// 严格模式：不容忍回拨，等待100ms后报错
using SnowflakeStrictV2 = Snowflake<8, 14, 41, 0, 100, 100>;

// 宽容模式：容忍10ms，等待60s，超过60s报错
using SnowflakeTolerantV2 = Snowflake<8, 14, 41, 10, 60000, 60000>;

// 高可用模式：容忍5ms，等待30s，不报错（等待阈值=报错阈值）
using SnowflakeHighAvailability = Snowflake<8, 14, 41, 5, 30000, UINT64_MAX>;

// 大规模部署
using SnowflakeLargeV2 = Snowflake<10, 12, 41, 5, 10000, 10000>;

// 高吞吐量
using SnowflakeHighThroughputV2 = Snowflake<8, 16, 39, 10, 10000, 10000>;

}