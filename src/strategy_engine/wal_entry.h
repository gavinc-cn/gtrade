//
// Created by Claude Code
// WAL (Write-Ahead Log) 条目定义
//
// 用于记录关键操作，支持崩溃恢复和主备同步
// 基于 zrt::WalEntry 泛型实现，提供 gtrade 特定的类型定义
//

#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>
#include "zrtools/zrt_wal_entry.h"

namespace gtrade {

/**
 * WAL 条目类型（gtrade 业务特定）
 */
enum class WalEntryType : uint16_t {
    kInvalid = 0,

    // 订单相关
    kOrder = 1,             // 订单（创建、更新、成交、撤销等所有状态）

    // 成交相关
    kTrade = 10,            // 成交

    // 持仓相关
    kPosition = 20,         // 持仓

    // 策略相关
    kStrategyCheckpoint = 30,  // 策略检查点
};

// 使用 zrt 泛型 WAL 条目类型
using WalEntryHeader = zrt::WalEntryHeader<WalEntryType>;
using WalEntry = zrt::WalEntry<WalEntryType>;
using WalEntryBuilder = zrt::WalEntryBuilder<WalEntryType>;

static_assert(sizeof(WalEntryHeader) == 24, "WalEntryHeader must be 24 bytes");

/**
 * 获取 WAL 条目类型名称
 */
inline const char* GetWalEntryTypeName(const WalEntryType type) {
    switch (type) {
        case WalEntryType::kInvalid:            return "Invalid";
        case WalEntryType::kOrder:              return "Order";
        case WalEntryType::kTrade:              return "Trade";
        case WalEntryType::kPosition:           return "Position";
        case WalEntryType::kStrategyCheckpoint: return "StrategyCheckpoint";
        default:                                return "Unknown";
    }
}

}  // namespace gtrade
