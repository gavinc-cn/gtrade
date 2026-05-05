//
// Created by Claude Code
// 策略检查点结构定义
//
// 采用模板包装设计：
// - 外层 CheckpointWrapper<T> 包含基类需要的通用字段
// - 内层 T 由策略自己定义，包含策略特定状态
//

#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace gtrade {

/**
 * 策略状态枚举
 */
enum class StrategyStatus : char {
    kUnknown = 0,
    kInit = 'I',        // 初始化
    kRunning = 'R',     // 运行中
    kPaused = 'P',      // 暂停
    kStopped = 'S',     // 停止
    kError = 'E',       // 错误
};

/**
 * 检查点包装模板
 *
 * 设计原则:
 * - 外层包含基类需要的通用字段（market_seq, order_seq 等）
 * - 内层 UserData 由策略自定义
 * - 整体仍然是 POD 类型，支持共享内存
 *
 * @tparam UserData 策略自定义的检查点数据类型
 */
template<typename UserData>
struct CheckpointWrapper {
    static_assert(std::is_trivially_copyable_v<UserData>,
                  "UserData must be trivially copyable");
    // static_assert(std::is_standard_layout_v<UserData>,
                  // "UserData must be standard layout");

    // ========== 基类管理的通用字段 ==========

    // 版本号（用于结构升级兼容）
    uint32_t version {1};

    // 策略 ID（hash 形式）
    uint32_t strategy_id {0};

    // 序号追踪
    uint64_t market_seq {0};      // 最后处理的行情序号
    uint64_t order_seq {0};       // 最后处理的订单回报序号
    uint64_t timestamp_ns {0};    // 检查点创建时间（纳秒）

    // 策略运行状态
    StrategyStatus status {StrategyStatus::kUnknown};

    // 对齐填充
    char _padding[7] {0};

    // ========== 策略自定义数据 ==========

    UserData data {};

    // ========== 校验字段（放在最后） ==========

    uint64_t checksum {0};

    /**
     * 计算 CRC64 校验和
     *
     * 使用指针算术计算 checksum 字段之前的字节数，避免 offsetof 警告
     */
    [[nodiscard]]
    uint64_t CalculateChecksum() const {
        constexpr uint64_t POLY = 0x42F0E1EBA9EA3693ULL;

        const char* start = reinterpret_cast<const char*>(this);
        const char* end = reinterpret_cast<const char*>(&checksum);
        const size_t len = static_cast<size_t>(end - start);

        uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;

        for (size_t i = 0; i < len; ++i) {
            crc ^= static_cast<uint64_t>(static_cast<unsigned char>(start[i])) << 56;
            for (int j = 0; j < 8; ++j) {
                if (crc & 0x8000000000000000ULL) {
                    crc = (crc << 1) ^ POLY;
                } else {
                    crc <<= 1;
                }
            }
        }

        return crc ^ 0xFFFFFFFFFFFFFFFFULL;
    }

    /**
     * 更新校验和
     */
    void UpdateChecksum() {
        checksum = CalculateChecksum();
    }

    /**
     * 验证校验和
     */
    [[nodiscard]]
    bool ValidateChecksum() const {
        return checksum == CalculateChecksum();
    }

    /**
     * 重置为默认状态
     */
    void Reset() {
        version = 1;
        strategy_id = 0;
        market_seq = 0;
        order_seq = 0;
        timestamp_ns = 0;
        status = StrategyStatus::kUnknown;
        std::memset(_padding, 0, sizeof(_padding));
        data = UserData{};
        checksum = 0;
    }
};

// 编译期检查：确保包装结构仍然是 trivially copyable
template<typename UserData>
constexpr bool CheckpointWrapperIsTrivial() {
    return std::is_trivially_copyable_v<CheckpointWrapper<UserData>> &&
           std::is_standard_layout_v<CheckpointWrapper<UserData>>;
}

/**
 * 空检查点数据（用于不需要自定义数据的策略）
 */
struct EmptyCheckpointData {
    char _placeholder {0};
};

/**
 * 策略检查点共享内存名称生成器
 */
inline std::string GetStrategyCheckpointShmName(const std::string& strategy_id) {
    return "gtrade_strategy_checkpoint_" + strategy_id;
}

/**
 * 从字符串策略ID生成数值ID
 *
 * 使用 FNV-1a hash 算法
 */
inline uint32_t HashStrategyId(const std::string& strategy_id) {
    constexpr uint32_t FNV_OFFSET = 2166136261u;
    constexpr uint32_t FNV_PRIME = 16777619u;

    uint32_t hash = FNV_OFFSET;
    for (char c : strategy_id) {
        hash ^= static_cast<uint32_t>(c);
        hash *= FNV_PRIME;
    }
    return hash;
}

}  // namespace gtrade
