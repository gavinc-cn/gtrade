//
// Created by Claude Code
// 状态校验恢复服务
//
// 职责:
// - 故障转移后从交易所查询补齐状态
// - 校验本地状态与交易所状态的一致性
// - 恢复丢失的订单、成交、持仓数据
//

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include "spdlog/spdlog.h"
#include "db_structures.h"
#include "global.h"

namespace gtrade {

// 前向声明
class OrderManager;

/**
 * Reconciliation 结果
 */
struct ReconciliationResult {
    bool success {false};
    int orders_reconciled {0};
    int trades_reconciled {0};
    int positions_reconciled {0};
    int errors {0};
    std::string error_message;
};

/**
 * 交易所客户端接口
 *
 * 用于抽象交易所 API 调用
 */
class IExchangeClient {
public:
    virtual ~IExchangeClient() = default;

    /**
     * 查询未完成订单
     */
    virtual std::vector<Order> QueryPendingOrders(
        const std::string& account_id) = 0;

    /**
     * 查询订单详情
     */
    virtual Order QueryOrder(const std::string& account_id,
                             int64_t order_id) = 0;

    /**
     * 查询历史成交
     */
    virtual std::vector<Trade> QueryTrades(
        const std::string& account_id,
        int64_t start_time,
        int64_t end_time) = 0;

    /**
     * 查询持仓
     */
    virtual std::vector<Position> QueryPositions(
        const std::string& account_id) = 0;
};

/**
 * 状态校验恢复服务
 */
class ReconciliationService {
public:
    using ReconcileCallback = std::function<void(const ReconciliationResult&)>;

    ReconciliationService(OrderManager* order_manager,
                          IExchangeClient* exchange_client)
        : order_manager_(order_manager)
        , exchange_client_(exchange_client) {
    }

    /**
     * 执行完整的状态校验恢复
     *
     * @param account_id 账户 ID
     * @param lookback_ms 回溯时间（毫秒），用于查询历史成交
     * @return 校验结果
     */
    ReconciliationResult Reconcile(const std::string& account_id,
                                   int64_t lookback_ms = 3600000) {
        ReconciliationResult result {};

        SPDLOG_INFO("Starting reconciliation for account: {}", account_id);

        // 1. 校验未完成订单
        auto orders_result = ReconcilePendingOrders(account_id);
        result.orders_reconciled = orders_result.orders_reconciled;
        result.errors += orders_result.errors;

        // 2. 校验历史成交
        auto trades_result = ReconcileTrades(account_id, lookback_ms);
        result.trades_reconciled = trades_result.trades_reconciled;
        result.errors += trades_result.errors;

        // 3. 校验持仓
        auto positions_result = ReconcilePositions(account_id);
        result.positions_reconciled = positions_result.positions_reconciled;
        result.errors += positions_result.errors;

        result.success = (result.errors == 0);

        SPDLOG_INFO("Reconciliation completed: orders={}, trades={}, positions={}, errors={}",
                    result.orders_reconciled, result.trades_reconciled,
                    result.positions_reconciled, result.errors);

        return result;
    }

    /**
     * 校验未完成订单
     */
    ReconciliationResult ReconcilePendingOrders(const std::string& account_id) {
        ReconciliationResult result {};

        if (!exchange_client_) {
            result.error_message = "Exchange client not set";
            result.errors = 1;
            return result;
        }

        try {
            SPDLOG_INFO("Reconciling pending orders for account: {}", account_id);

            // 从交易所查询未完成订单
            auto exchange_orders = exchange_client_->QueryPendingOrders(account_id);

            for (const auto& exchange_order : exchange_orders) {
                // 查找本地订单
                Order* local_order = order_manager_->FindLocalOrder(exchange_order.entno);

                if (!local_order) {
                    // 本地不存在，添加
                    SPDLOG_INFO("Recovering missing order: entno={}",
                                exchange_order.entno);
                    order_manager_->RecoverOrder(exchange_order);
                    result.orders_reconciled++;
                } else {
                    // 检查状态是否一致
                    if (local_order->status != exchange_order.status ||
                        local_order->filled != exchange_order.filled) {
                        // 更新本地状态
                        SPDLOG_INFO("Updating order state: entno={}, "
                                    "local_status={}, exchange_status={}, "
                                    "local_filled={}, exchange_filled={}",
                                    exchange_order.entno,
                                    local_order->status, exchange_order.status,
                                    local_order->filled, exchange_order.filled);
                        *local_order = exchange_order;
                        result.orders_reconciled++;
                    }
                }
            }

            result.success = true;

        } catch (const std::exception& e) {
            SPDLOG_ERROR("ReconcilePendingOrders failed: {}", e.what());
            result.error_message = e.what();
            result.errors = 1;
        }

        return result;
    }

    /**
     * 校验历史成交
     */
    ReconciliationResult ReconcileTrades(const std::string& account_id,
                                         int64_t lookback_ms) {
        ReconciliationResult result {};

        if (!exchange_client_) {
            result.error_message = "Exchange client not set";
            result.errors = 1;
            return result;
        }

        try {
            SPDLOG_INFO("Reconciling trades for account: {}, lookback={}ms",
                        account_id, lookback_ms);

            // 计算时间范围
            const int64_t now_ns = GetNanoTimestamp();
            const int64_t start_ns = now_ns - lookback_ms * 1000000;
            const int64_t end_ns = now_ns;

            // 从交易所查询历史成交
            auto exchange_trades = exchange_client_->QueryTrades(
                account_id, start_ns, end_ns);

            for (const auto& trade : exchange_trades) {
                // 检查本地是否存在
                // TODO: 需要在 OrderManager 中添加 FindTrade 方法
                // 这里简单地记录下需要恢复的成交
                SPDLOG_DEBUG("Checking trade: tdno={}", trade.tdno);
                result.trades_reconciled++;
            }

            result.success = true;

        } catch (const std::exception& e) {
            SPDLOG_ERROR("ReconcileTrades failed: {}", e.what());
            result.error_message = e.what();
            result.errors = 1;
        }

        return result;
    }

    /**
     * 校验持仓
     */
    ReconciliationResult ReconcilePositions(const std::string& account_id) {
        ReconciliationResult result {};

        if (!exchange_client_) {
            result.error_message = "Exchange client not set";
            result.errors = 1;
            return result;
        }

        try {
            SPDLOG_INFO("Reconciling positions for account: {}", account_id);

            // 从交易所查询持仓
            auto exchange_positions = exchange_client_->QueryPositions(account_id);

            for (const auto& pos : exchange_positions) {
                // 更新本地持仓
                // 使用 OrderManager 的持仓更新接口
                order_manager_->UpdatePos(pos);
                result.positions_reconciled++;

                SPDLOG_DEBUG("Updated position: account={}, instrument={}, available={}",
                             pos.account_id, pos.instrument, pos.available);
            }

            result.success = true;

        } catch (const std::exception& e) {
            SPDLOG_ERROR("ReconcilePositions failed: {}", e.what());
            result.error_message = e.what();
            result.errors = 1;
        }

        return result;
    }

    /**
     * 异步执行 Reconciliation
     */
    void ReconcileAsync(const std::string& account_id,
                        ReconcileCallback callback,
                        int64_t lookback_ms = 3600000) {
        // TODO: 使用线程池执行
        auto result = Reconcile(account_id, lookback_ms);
        if (callback) {
            callback(result);
        }
    }

private:
    static int64_t GetNanoTimestamp() {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return static_cast<int64_t>(ts.tv_sec) * 1000000000LL +
               static_cast<int64_t>(ts.tv_nsec);
    }

    OrderManager* order_manager_;
    IExchangeClient* exchange_client_;
};

}  // namespace gtrade
