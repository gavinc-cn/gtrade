//
// Created by Claude Code
// 共享内存 WAL 环形缓冲区
//
// 设计目标:
// - 支持变长 WAL 条目
// - 单生产者单消费者无锁访问
// - 共享内存持久化
// - 支持崩溃恢复和主备同步
//
// 基于 zrt::ShmRing 泛型实现，提供 gtrade 特定的类型别名
//

#pragma once

#include "wal_entry.h"
#include "zrtools/zrt_shm_ring.h"

namespace gtrade {

/**
 * 共享内存 WAL 环形缓冲区
 *
 * 使用 zrt::ShmRing 泛型实现
 *
 * 特性:
 * - 变长条目存储（每个条目包含 WalEntryHeader + 变长数据）
 * - 单生产者单消费者无锁设计
 * - 三个序号追踪: write_pos, read_pos, confirmed_pos
 * - 支持批量读取未确认条目（用于恢复和同步）
 *
 * 内存布局:
 * +-------------------+
 * | ShmRingHeader     | 512 bytes (固定)
 * +-------------------+
 * | buffer_[]         | 16MB bytes
 * +-------------------+
 */
using ShmWalRing = zrt::ShmRing<WalEntryType>;

// 兼容旧代码的头部类型别名
using ShmWalRingHeader = ShmWalRing::ShmRingHeader;

// WAL 共享内存名称常量
constexpr char kOrderWalShmName[] = "gtrade_order_wal";
constexpr char kHaStateShmName[] = "gtrade_ha_state";

}  // namespace gtrade
