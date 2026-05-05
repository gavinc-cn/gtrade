//
// Created by Claude Code
// 委托和成交持久化缓冲区定义
//
// 使用SPSC (Single Producer Single Consumer) 无锁环形缓冲区实现
// 支持高性能的委托和成交数据持久化
//

#pragma once

#include "db_structures.h"
#include "i_exchange_data.h"
#include "spsc_ring_buffer.h"
#include <cstdint>
#include <string>

// 环形缓冲区大小配置
constexpr size_t MAX_ENTRUST_BUFFER_SIZE = 10000;  // 可存储1万条委托
constexpr size_t MAX_DONE_BUFFER_SIZE = 50000;     // 可存储5万条成交

/**
 * 委托持久化缓冲区
 *
 * 直接使用SPSCRingBuffer，策略在编译期决定：
 * - FullPolicy::ReturnFalse: 队列满时立即返回失败，不阻塞策略引擎（安全优先）
 * - EmptyPolicy::ReturnFalse: 队列空时立即返回，不阻塞（PollSharedMemory需要串行轮询多个队列）
 *
 * 权衡：
 * - 优点：永不阻塞策略引擎主线程和持久化线程，保证系统可用性
 * - 缺点：队列满时共享内存持久化失败（数据仍在内存map中）
 */
using OrderPersistenceBuffer = zrt::SPSCRingBuffer<Order,
                                                 MAX_ENTRUST_BUFFER_SIZE,
                                                 zrt::FullPolicy::ReturnFalse,
                                                 zrt::EmptyPolicy::ReturnFalse>;

/**
 * 成交持久化缓冲区
 *
 * 直接使用SPSCRingBuffer，策略在编译期决定
 * - FullPolicy::ReturnFalse: 队列满时立即返回失败，不阻塞策略引擎（安全优先）
 * - EmptyPolicy::ReturnFalse: 队列空时立即返回，不阻塞（PollSharedMemory需要串行轮询多个队列）
 */
using TradePersistenceBuffer = zrt::SPSCRingBuffer<Trade,
                                             MAX_DONE_BUFFER_SIZE,
                                             zrt::FullPolicy::ReturnFalse,
                                             zrt::EmptyPolicy::ReturnFalse>;

// 环形缓冲区大小配置 - 持仓
constexpr size_t MAX_POSITION_BUFFER_SIZE = 5000;  // 可存储5千条持仓
constexpr size_t MAX_PORTFOLIO_POSITION_BUFFER_SIZE = 10000;  // 可存储1万条组合持仓

/**
 * 持仓持久化缓冲区
 *
 * 直接使用SPSCRingBuffer，策略在编译期决定：
 * - FullPolicy::ReturnFalse: 队列满时立即返回失败，不阻塞策略引擎（安全优先）
 * - EmptyPolicy::ReturnFalse: 队列空时立即返回，不阻塞（PollSharedMemory需要串行轮询多个队列）
 */
using PositionPersistenceBuffer = zrt::SPSCRingBuffer<Position,
                                                      MAX_POSITION_BUFFER_SIZE,
                                                      zrt::FullPolicy::ReturnFalse,
                                                      zrt::EmptyPolicy::ReturnFalse>;

/**
 * 组合持仓持久化缓冲区
 *
 * 直接使用SPSCRingBuffer，策略在编译期决定
 * - FullPolicy::ReturnFalse: 队列满时立即返回失败，不阻塞策略引擎（安全优先）
 * - EmptyPolicy::ReturnFalse: 队列空时立即返回，不阻塞（PollSharedMemory需要串行轮询多个队列）
 */
using PortfolioPositionPersistenceBuffer = zrt::SPSCRingBuffer<Position,
                                                               MAX_PORTFOLIO_POSITION_BUFFER_SIZE,
                                                               zrt::FullPolicy::ReturnFalse,
                                                               zrt::EmptyPolicy::ReturnFalse>;

// 环形缓冲区大小配置 - 资金
constexpr size_t MAX_BALANCE_BUFFER_SIZE = 1000;  // 可存储1千条资金记录

/**
 * 资金持久化缓冲区
 *
 * 直接使用SPSCRingBuffer，策略在编译期决定：
 * - FullPolicy::ReturnFalse: 队列满时立即返回失败，不阻塞策略引擎（安全优先）
 * - EmptyPolicy::ReturnFalse: 队列空时立即返回，不阻塞（PollSharedMemory需要串行轮询多个队列）
 */
// todo balance的结构体应该和数据库表统一
using BalancePersistenceBuffer = zrt::SPSCRingBuffer<Balance,
                                                     MAX_BALANCE_BUFFER_SIZE,
                                                     zrt::FullPolicy::ReturnFalse,
                                                     zrt::EmptyPolicy::ReturnFalse>;

constexpr char kOrderShmName[] = "gtrade_order_persistence";
constexpr char kTradeShmName[] = "gtrade_trade_persistence";
constexpr char kPositionShmName[] = "gtrade_position_persistence";
constexpr char kPortfolioPositionShmName[] = "gtrade_portfolio_position_persistence";
constexpr char kBalanceShmName[] = "gtrade_balance_persistence";

// WAL 共享内存名称（已在 shm_wal_ring.h 中定义）
// constexpr char kOrderWalShmName[] = "gtrade_order_wal";
// constexpr char kHaStateShmName[] = "gtrade_ha_state";

// 策略检查点共享内存名称前缀
constexpr char kStrategyCheckpointShmPrefix[] = "gtrade_strategy_checkpoint_";
