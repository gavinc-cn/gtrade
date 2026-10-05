//
// Created by dell on 2025/2/26.
//

#include <bitset>
#include <cstring>
#include "order_manager.h"
#include <filesystem>
#include "define.h"
#include "i_client.h"
#include "my_utc.h"
#include "notify_msg_helper.h"
#include "service_map.h"
#include "string_keys.h"
#include "zrtools/zrt_define.h"
#include "sonic_helper.h"

// 构造函数：初始化共享内存
OrderManager::OrderManager()
{
    // 第一次 IsValid(): 检查 DirectShm 对象是否成功创建（共享内存映射）
    if (!m_order_shm.IsValid()) {
        SPDLOG_ERROR("Failed to create/map entrust shared memory");
    } else {
        // 第二次 IsValid(): 检查共享内存内容的魔数是否有效
        if (!m_order_shm->IsValid()) {
            SPDLOG_WARN("Entrust shared memory content invalid (bad magic), resetting...");
            m_order_shm->Reset();
        }
        SPDLOG_INFO("Entrust shared memory initialized: write_seq={}, confirmed_seq={}, unconfirmed={}",
                    m_order_shm->GetWriteSeq(), m_order_shm->GetConfirmedSeq(),
                    m_order_shm->GetUnconfirmedCount());
    }

    if (!m_trade_shm.IsValid()) {
        SPDLOG_ERROR("Failed to create/map done shared memory");
    } else {
        if (!m_trade_shm->IsValid()) {
            SPDLOG_WARN("Done shared memory content invalid (bad magic), resetting...");
            m_trade_shm->Reset();
        }
        SPDLOG_INFO("Done shared memory initialized: write_seq={}, confirmed_seq={}, unconfirmed={}",
                    m_trade_shm->GetWriteSeq(), m_trade_shm->GetConfirmedSeq(),
                    m_trade_shm->GetUnconfirmedCount());
    }

    if (!m_pos_shm.IsValid()) {
        SPDLOG_ERROR("Failed to create/map position shared memory");
    } else {
        if (!m_pos_shm->IsValid()) {
            SPDLOG_WARN("Position shared memory content invalid (bad magic), resetting...");
            m_pos_shm->Reset();
        }
        SPDLOG_INFO("Position shared memory initialized: write_seq={}, confirmed_seq={}, unconfirmed={}",
                    m_pos_shm->GetWriteSeq(), m_pos_shm->GetConfirmedSeq(),
                    m_pos_shm->GetUnconfirmedCount());
    }

    if (!m_portfolio_pos_shm.IsValid()) {
        SPDLOG_ERROR("Failed to create/map portfolio position shared memory");
    } else {
        if (!m_portfolio_pos_shm->IsValid()) {
            SPDLOG_WARN("Portfolio position shared memory content invalid (bad magic), resetting...");
            m_portfolio_pos_shm->Reset();
        }
        SPDLOG_INFO("Portfolio position shared memory initialized: write_seq={}, confirmed_seq={}, unconfirmed={}",
                    m_portfolio_pos_shm->GetWriteSeq(), m_portfolio_pos_shm->GetConfirmedSeq(),
                    m_portfolio_pos_shm->GetUnconfirmedCount());
    }

    if (!m_balance_shm.IsValid()) {
        SPDLOG_ERROR("Failed to create/map balance shared memory");
    } else {
        if (!m_balance_shm->IsValid()) {
            SPDLOG_WARN("Balance shared memory content invalid (bad magic), resetting...");
            m_balance_shm->Reset();
        }
        SPDLOG_INFO("Balance shared memory initialized: write_seq={}, confirmed_seq={}, unconfirmed={}",
                    m_balance_shm->GetWriteSeq(), m_balance_shm->GetConfirmedSeq(),
                    m_balance_shm->GetUnconfirmedCount());
    }

    SPDLOG_INFO("OrderManager initialized");
}

// 用于在系统启动时恢复订单, 所以不用写入共享内存
void OrderManager::RecoverOrder(const Order& order) {
    if (ZRT_UNLIKELY(!order.entno)) {
        SPDLOG_ERROR("unexpected entno={}", order.entno);
        return;
    }
    m_order_map[order.entno] = order;
}

void OrderManager::SetIdBase(const int64_t seen_max_entno, const int64_t seen_max_tdno) {
    // 时间基数：进程启动时刻（秒 ×1e9），与历史号同量纲，故可直接比较
    const int64_t time_base = MyUTC().Epoch10() * zrt::kGiga;
    // 已见最大号 + 1 才是安全起点；无历史数据时退回时间基数
    const int64_t order_base = (seen_max_entno > 0) ? std::max(time_base, seen_max_entno + 1) : time_base;
    const int64_t trade_base = (seen_max_tdno > 0) ? std::max(time_base, seen_max_tdno + 1) : time_base;

    // CAS 循环：只抬高不降低，重复调用安全（并发时取更大者）
    int64_t current = start_ordno.load(std::memory_order_relaxed);
    while (current < order_base &&
           !start_ordno.compare_exchange_weak(current, order_base, std::memory_order_relaxed)) {
    }
    current = start_trdno.load(std::memory_order_relaxed);
    while (current < trade_base &&
           !start_trdno.compare_exchange_weak(current, trade_base, std::memory_order_relaxed)) {
    }

    SPDLOG_INFO("id base set: start_ordno={} (seen_max_entno={}), start_trdno={} (seen_max_tdno={}), time_base={}",
                start_ordno.load(std::memory_order_relaxed), seen_max_entno,
                start_trdno.load(std::memory_order_relaxed), seen_max_tdno, time_base);
}

int64_t OrderManager::GetMaxOrderNo() const {
    int64_t max_no = 0;
    for (const auto& [entno, order] : m_order_map) {
        if (entno > max_no) {
            max_no = entno;
        }
    }
    return max_no;
}

int64_t OrderManager::GetMaxTradeNo() const {
    int64_t max_no = 0;
    for (const auto& [tdno, trade] : m_trade_map) {
        if (tdno > max_no) {
            max_no = tdno;
        }
    }
    return max_no;
}

// ===== 补查：读内存权威态（web_server 断线重连后按游标补齐）=====
// 具体筛选逻辑在头文件的 SelectAfterCursor / SelectByKeys（静态纯函数，便于单测）；
// 补查是低频操作（重连触发），单次 O(n log n) 可接受，n 为启动窗口内加载的记录数。

std::vector<Order> OrderManager::QueryOrdersAfter(const int64_t cursor_entno, const size_t limit) const {
    return SelectAfterCursor(m_order_map, cursor_entno, limit);
}

std::vector<Order> OrderManager::QueryOrdersByEntnos(const int64_t* entnos, const size_t count) const {
    return SelectByKeys(m_order_map, entnos, count);
}

std::vector<Trade> OrderManager::QueryTradesAfter(const int64_t cursor_tdno, const size_t limit) const {
    return SelectAfterCursor(m_trade_map, cursor_tdno, limit);
}

void OrderManager::SaveOrder2Shm(const Order& order) {
    if constexpr (GlobalConst::IsBackTest) {
        return;
    }
    SPDLOG_INFO("");
    // 写入共享内存（用于MySqlGateway异步持久化）
    if (m_order_shm.IsValid()) {
        const bool added = m_order_shm->Write(order);
        if (!added) {
            // 共享内存满了！记录日志并发送Slack消息
            const size_t unconfirmed = m_order_shm->GetUnconfirmedCount();
            SPDLOG_ERROR("Entrust shared memory FULL! entno={}, write_seq={}, confirmed_seq={}, "
                        "unconfirmed={}/{}, data may be lost!",
                        order.entno, m_order_shm->GetWriteSeq(), m_order_shm->GetConfirmedSeq(),
                        unconfirmed, MAX_ENTRUST_BUFFER_SIZE);

            // 发送Slack告警（不限流）
            SendNotifyMsg(k_error, "委托共享内存已满",
                          fmt::format("委托号: {}, 未确认委托数: {}/{}, 数据可能丢失！",
                                    order.entno, unconfirmed, MAX_ENTRUST_BUFFER_SIZE));
        }
    } else {
        SPDLOG_ERROR("Entrust shared memory not valid for entno={}", order.entno);
        SendNotifyMsg(k_error, "ERROR: 委托共享内存不可用",
                      fmt::format("委托号: {}, 共享内存不可用", order.entno));
    }
}

void OrderManager::SaveTrade2Shm(const Trade& trade) {
    if constexpr (GlobalConst::IsBackTest) {
        return;
    }
    SPDLOG_INFO("");
    // 写入共享内存（用于MySqlGateway异步持久化）
    if (m_trade_shm.IsValid()) {
        const bool added = m_trade_shm->Write(trade);
        if (!added) {
            // 共享内存满了！记录日志并发送Slack消息
            const size_t unconfirmed = m_trade_shm->GetUnconfirmedCount();
            SPDLOG_ERROR("Done shared memory FULL! trade_no={}, write_seq={}, confirmed_seq={}, "
                        "unconfirmed={}/{}, data may be lost!",
                        trade.tdno, m_trade_shm->GetWriteSeq(), m_trade_shm->GetConfirmedSeq(),
                        unconfirmed, MAX_DONE_BUFFER_SIZE);

            // 发送Slack告警（不限流）
            SendNotifyMsg(k_error, "CRITICAL: 成交共享内存已满",
                          fmt::format("成交号: {}, 未确认成交数: {}/{}, 数据可能丢失！",
                                    trade.tdno, unconfirmed, MAX_DONE_BUFFER_SIZE));
        }
    } else {
        SPDLOG_ERROR("Done shared memory not valid for trade_no={}", trade.tdno);
        SendNotifyMsg(k_error, "ERROR: 成交共享内存不可用",
                      fmt::format("成交号: {}, 共享内存不可用", trade.tdno));
    }
}

void OrderManager::SavePos2Shm(const Position& pos) {
    if constexpr (GlobalConst::IsBackTest) {
        return;
    }
    SPDLOG_INFO("");
    // 写入共享内存（用于MySqlGateway异步持久化）
    if (m_pos_shm.IsValid()) {
        const bool added = m_pos_shm->Write(pos);
        if (!added) {
            // 共享内存满了！记录日志并发送Slack消息
            const size_t unconfirmed = m_pos_shm->GetUnconfirmedCount();
            SPDLOG_ERROR("Position shared memory FULL! account={}, inst={}, write_seq={}, confirmed_seq={}, "
                        "unconfirmed={}/{}, data may be lost!",
                        pos.account_id, pos.instrument, m_pos_shm->GetWriteSeq(), m_pos_shm->GetConfirmedSeq(),
                        unconfirmed, MAX_POSITION_BUFFER_SIZE);

            // 发送Slack告警（不限流）
            SendNotifyMsg(k_error, "持仓共享内存已满",
                          fmt::format("账户: {}, 合约: {}, 未确认持仓数: {}/{}, 数据可能丢失！",
                                    pos.account_id, pos.instrument, unconfirmed, MAX_POSITION_BUFFER_SIZE));
        }
    } else {
        SPDLOG_ERROR("Position shared memory not valid for account={}, inst={}", pos.account_id, pos.instrument);
        SendNotifyMsg(k_error, "ERROR: 持仓共享内存不可用",
                      fmt::format("账户: {}, 合约: {}, 共享内存不可用", pos.account_id, pos.instrument));
    }
}

void OrderManager::SaveBalance2Shm(const Balance& bal) {
    if constexpr (GlobalConst::IsBackTest) {
        return;
    }
    SPDLOG_INFO("");
    // 写入共享内存（用于MySqlGateway异步持久化）
    if (m_balance_shm.IsValid()) {
        const bool added = m_balance_shm->Write(bal);
        if (!added) {
            const size_t unconfirmed = m_balance_shm->GetUnconfirmedCount();
            SPDLOG_ERROR("Balance shared memory FULL! account={}, currency={}, write_seq={}, confirmed_seq={}, "
                        "unconfirmed={}/{}, data may be lost!",
                        bal.account_id, bal.currency,
                        m_balance_shm->GetWriteSeq(), m_balance_shm->GetConfirmedSeq(),
                        unconfirmed, MAX_BALANCE_BUFFER_SIZE);
            SendNotifyMsg(k_error, "资金共享内存已满",
                          fmt::format("账户: {}, 币种: {}, 未确认资金数: {}/{}, 数据可能丢失！",
                                    bal.account_id, bal.currency, unconfirmed, MAX_BALANCE_BUFFER_SIZE));
        }
    } else {
        SPDLOG_ERROR("Balance shared memory not valid for account={}, currency={}", bal.account_id, bal.currency);
        SendNotifyMsg(k_error, "ERROR: 资金共享内存不可用",
                      fmt::format("账户: {}, 币种: {}, 共享内存不可用", bal.account_id, bal.currency));
    }
}

void OrderManager::SavePortfolioPos2Shm(const Position& pos) {
    if constexpr (GlobalConst::IsBackTest) {
        return;
    }
    SPDLOG_INFO("");
    // 写入共享内存（用于MySqlGateway异步持久化）
    if (m_portfolio_pos_shm.IsValid()) {
        const bool added = m_portfolio_pos_shm->Write(pos);
        if (!added) {
            // 共享内存满了！记录日志并发送Slack消息
            const size_t unconfirmed = m_portfolio_pos_shm->GetUnconfirmedCount();
            SPDLOG_ERROR("Portfolio position shared memory FULL! strat={}, account={}, inst={}, "
                        "write_seq={}, confirmed_seq={}, unconfirmed={}/{}, data may be lost!",
                        pos.portfolio, pos.account_id, pos.instrument,
                        m_portfolio_pos_shm->GetWriteSeq(), m_portfolio_pos_shm->GetConfirmedSeq(),
                        unconfirmed, MAX_PORTFOLIO_POSITION_BUFFER_SIZE);

            // 发送Slack告警（不限流）
            SendNotifyMsg(k_error, "组合持仓共享内存已满",
                          fmt::format("策略: {}, 账户: {}, 合约: {}, 未确认组合持仓数: {}/{}, 数据可能丢失！",
                                    pos.portfolio, pos.account_id, pos.instrument,
                                    unconfirmed, MAX_PORTFOLIO_POSITION_BUFFER_SIZE));
        }
    } else {
        SPDLOG_ERROR("Portfolio position shared memory not valid for strat={}, account={}, inst={}",
                     pos.portfolio, pos.account_id, pos.instrument);
        SendNotifyMsg(k_error, "ERROR: 组合持仓共享内存不可用",
                      fmt::format("策略: {}, 账户: {}, 合约: {}, 共享内存不可用",
                                pos.portfolio, pos.account_id, pos.instrument));
    }
}

Order* OrderManager::AddOrder(const Order& order) {
    if (!order.entno) {
        SPDLOG_ERROR("unexpected entno={}", order.entno);
        return nullptr;
    }

    // 检查是否已存在
    if (const auto iter = m_order_map.find(order.entno);
        iter != m_order_map.end())
    {
        SPDLOG_ERROR("entno already exist: {}", order.entno);
        return nullptr;
    }

    // 写入 WAL（HA 同步）
    WriteToWal(gtrade::WalEntryType::kOrder, order);

    SaveOrder2Shm(order);
    auto [it, inserted] = m_order_map.emplace(order.entno, order);
    return inserted ? &it->second : nullptr;
}

void OrderManager::UpdateOrder(const Order& order) {
    if (!order.entno) {
        SPDLOG_ERROR("unexpected entno={}", order.entno);
        return;
    }

    // 写入 WAL（HA 同步），状态信息已在 Order 结构体中
    WriteToWal(gtrade::WalEntryType::kOrder, order);

    SaveOrder2Shm(order);
    m_order_map.insert_or_assign(order.entno, order);
}

void OrderManager::AddTrade(const Trade& trade) {
    if (!trade.tdno) {
        SPDLOG_ERROR("unexpected trade_no={}", trade.tdno);
        return;
    }

    // 检查是否已存在
    const auto iter = m_trade_map.find(trade.tdno);
    if (iter != m_trade_map.end()) {
        SPDLOG_WARN("trade_no already exist: {}", trade.tdno);
        return;
    }

    // 写入 WAL（HA 同步）
    WriteToWal(gtrade::WalEntryType::kTrade, trade);

    SaveTrade2Shm(trade);

    // 添加到内存map（主存储）
    m_trade_map.emplace(trade.tdno, trade);

    if (!zrt::is_empty(trade.strat_id)) {
        UpdateStrategyPosition(trade);
    }
}

// void OrderManager::UpdateEntrust(Entrust& entrust) {
//     if (!entrust.entno) {
//         entrust.entno = CreateEntrustId();
//         zrt::fill_field(entrust.source, OrderSource::Foreign);
//         SPDLOG_ERROR("entno not set, create entno={} for foreign entrust", entrust.entno);
//     }
//     const auto iter = m_entrust_map.find(entrust.entno);
//     if (iter == m_entrust_map.end()) {
//         m_entrust_map.emplace(entrust.entno, entrust);
//         SPDLOG_ERROR("add foreign entno={} to order manager", entrust.entno);
//     } else {
//         iter->second = entrust;
//     }
// }

// void OrderManager::UpdateEntrust(Entrust& entrust) {
//     if (likely(entrust.entno)) {
//         m_entrust_map[entrust.entno] = entrust;
//     }
//     // else if (entrust.ex_entno && !zrt::is_empty(entrust.market)) {
//         // m_foreign_entrust_map[entrust.market][entrust.ex_entno] = entrust;
//     // }
//     else {
//         SPDLOG_ERROR("unexpected entrust={}", zrt::to_str(entrust));
//     }
// }

// ============ 编译期状态机实现 ============
namespace {
    // 使用两个 uint64_t 表示 128 位的状态转换掩码
    // 这样可以在 C++17 中实现完全 constexpr 的状态机（std::bitset 在 C++17 不支持 constexpr set）
    struct StatusMask {
        uint64_t low;   // 表示 ASCII 0-63
        uint64_t high;  // 表示 ASCII 64-127

        constexpr StatusMask() : low(0), high(0) {}

        constexpr void set(const char c) {
            const size_t pos = static_cast<size_t>(c);
            if (pos < 64) {
                low |= (1ULL << pos);
            } else {
                high |= (1ULL << (pos - 64));
            }
        }

        constexpr bool test(const char c) const {
            const size_t pos = static_cast<size_t>(c);
            if (pos < 64) {
                return (low & (1ULL << pos)) != 0;
            } else {
                return (high & (1ULL << (pos - 64))) != 0;
            }
        }
    };

    // 编译期生成禁止转换表
    constexpr auto BuildForbiddenTransitions() {
        std::array<StatusMask, 128> transitions {};

        // 辅助 lambda：添加禁止的状态转换（完全 constexpr）
        auto forbid = [&](const char from, const std::initializer_list<char> to_list) {
            for (const char to : to_list) {
                transitions[static_cast<size_t>(from)].set(to);
            }
        };

        forbid(OrderStatus::_0, {});
        forbid(OrderStatus::_1, {OrderStatus::_0});
        forbid(OrderStatus::_2, {OrderStatus::_0, OrderStatus::_1});
        forbid(OrderStatus::_3, {OrderStatus::_0, OrderStatus::_1, OrderStatus::_2, OrderStatus::_5, OrderStatus::_6});
        forbid(OrderStatus::_5, {OrderStatus::_0, OrderStatus::_1});
        forbid(OrderStatus::_7, {OrderStatus::_0, OrderStatus::_1, OrderStatus::_2, OrderStatus::_5, OrderStatus::_6});
        forbid(OrderStatus::_I, {OrderStatus::_0, OrderStatus::_1});

        return transitions;
    }

    // 编译期生成的禁止转换表（完全在编译期完成，零运行时开销）
    constexpr auto kForbiddenTransitions = BuildForbiddenTransitions();

} // anonymous namespace

bool OrderManager::IsForwardStatus(const char old_status, const char new_status) {
    if (ZRT_LIKELY(zrt::equal_any_of(old_status, new_status, OrderStatus::_4, OrderStatus::_6, OrderStatus::_8, OrderStatus::_9))) {
        return false;
    }

    // 负向判断：检查是否在禁止列表中，不在则允许
    return !kForbiddenTransitions[static_cast<size_t>(old_status)].test(new_status);
}

bool OrderManager::IsForwardOrder(const int64_t old_update_time,
                         const double old_filled,
                         const CharCs old_status,
                         const Order& new_order) {
    // 优化: 使用 early return 减少嵌套，优先判断最常见的情况（时间不同）
    if (new_order.update_time != old_update_time) {
        return new_order.update_time > old_update_time;
    }

    // 时间相同时，判断成交量（保留 zrt::equal 用于 double 的 epsilon 比较）
    if (!zrt::equal(new_order.filled, old_filled)) {
        return new_order.filled > old_filled;
    }

    // 时间和成交量都相同，检查状态转换是否合法
    return IsForwardStatus(old_status, new_order.status);
}

// void OrderManager::UpdateEntrustByQuery(const Entrust& entrust) {
//     if (likely(entrust.entno)) {
//         auto ret = m_entrust_map.emplace(entrust.entno, entrust);
//         if (!ret.second) {
//             if (Check(ret.first->second, entrust)) {
//                 ret.first->second = entrust;
//             }
//         }
//     }
//     else if (entrust.ex_entno && !zrt::is_empty(entrust.market)) {
//         auto ret = m_foreign_entrust_map[entrust.market].emplace(entrust.ex_entno, entrust);
//         if (!ret.second) {
//             if (Check(ret.first->second, entrust)) {
//                 ret.first->second = entrust;
//             }
//         }
//     }
//     else {
//         SPDLOG_ERROR("unexpected entrust={}", zrt::to_str(entrust));
//     }
// }
//
// Entrust& OrderManager::RefEntrust(Entrust& recv_ent, const std::function<void(Entrust&)>& func) {
//     if (!recv_ent.entno) {
//         recv_ent.entno = CreateEntrustId();
//         zrt::fill_field(recv_ent.source, OrderSource::Foreign);
//         SPDLOG_ERROR("entno not set, create entno={} for foreign entrust", recv_ent.entno);
//     }
//     const auto iter = m_entrust_map.find(recv_ent.entno);
//     if (iter != m_entrust_map.end()) {
//         func(iter->second);
//     } else {
//         m_entrust_map.emplace(recv_ent.entno, recv_ent);
//         SPDLOG_ERROR("add foreign entno={} to order manager", recv_ent.entno);
//     }
//     return m_entrust_map.at(recv_ent.entno);
// }

// Entrust& OrderManager::RefEntrust(const Entrust& recv_ent) {
//     int64_t new_entno = recv_ent.entno;
//     char source = recv_ent.source;
//     if (!new_entno) {
//         new_entno = CreateEntrustId();
//         zrt::fill_field(source, OrderSource::Foreign);
//         SPDLOG_ERROR("entno not set, create entno={} for foreign entrust", new_entno);
//     }
//     const auto iter = m_entrust_map.find(new_entno);
//     if (iter != m_entrust_map.end()) {
//         return iter->second;
//     } else {
//         m_entrust_map.emplace(new_entno, recv_ent);
//         SPDLOG_ERROR("add foreign entno={} to order manager", new_entno);
//         return m_entrust_map.at(new_entno);
//     }
// }

// Entrust& OrderManager::RefEntrust(const Entrust& entrust) {
//     if (likely(entrust.entno)) {
//         return m_entrust_map.emplace(entrust.entno, entrust).first->second;
//     }
//     else if (entrust.ex_entno && !zrt::is_empty(entrust.market)) {
//         return m_foreign_entrust_map[entrust.market].emplace(entrust.ex_entno, entrust).first->second;
//     }
//     else {
//         SPDLOG_ERROR("unexpected entrust={}", zrt::to_str(entrust));
//     }
// }
//
// Entrust* OrderManager::FindEntrust(const Entrust& entrust) {
//     if (likely(entrust.entno)) {
//         return &m_entrust_map.emplace(entrust.entno, entrust).first->second;
//     }
//     else if (entrust.ex_entno && !zrt::is_empty(entrust.market)) {
//         return &m_foreign_entrust_map[entrust.market].emplace(entrust.ex_entno, entrust).first->second;
//     }
//     else {
//         SPDLOG_ERROR("unexpected entrust={}", zrt::to_str(entrust));
//         return nullptr;
//     }
// }

Order* OrderManager::FindLocalOrder(const int64_t entno) {
    if (ZRT_LIKELY(entno)) {
        const auto iter = m_order_map.find(entno);
        if (iter != m_order_map.end()) {
            return &iter->second;
        }
        SPDLOG_ERROR("not found entno={}", entno);
        return nullptr;
    }
    SPDLOG_WARN("empty ordno, maybe foreign order");
    return nullptr;
}

int64_t OrderManager::FindOrderNo(const std::string& market, const int64_t ex_entno) {
    if (ZRT_UNLIKELY(!ex_entno)) {
        SPDLOG_ERROR("invalid ex_entno={}", ex_entno);
        return 0;
    }
    const auto iter = m_foreign_order_map[market].find(ex_entno);
    if (iter == m_foreign_order_map[market].end()) {
        SPDLOG_INFO("not found ex_entno={}", ex_entno);
        return 0;
    }
    return iter->second;
}

Order* OrderManager::FindLocalOrderByPrivateNo(const std::string_view private_no) {
    if (ZRT_LIKELY(!private_no.empty())) {
        for (auto& [entno, entrust] : m_order_map) {
            if (std::string_view(entrust.private_no) == private_no) {
                return &entrust;
            }
        }
        SPDLOG_DEBUG("not found private_no={}", private_no);
        return nullptr;
    }
    SPDLOG_ERROR("invalid private_no");
    return nullptr;
}

// 从共享内存恢复委托和成交
size_t OrderManager::RecoverFromShm() {
    static MyHandler* mysql_gateway = ServiceMap::GetInstance().at(k_MySqlGateway).get();

    size_t recovered_count = 0;
    // 恢复委托
    if (m_order_shm.IsValid() && m_order_shm->IsValid()) {
        constexpr size_t TEMP_BUFFER_SIZE = MAX_ENTRUST_BUFFER_SIZE;
        std::vector<Order> temp_entrusts(TEMP_BUFFER_SIZE);

        const size_t count = m_order_shm->ReadUnconfirmed(temp_entrusts.data(), TEMP_BUFFER_SIZE);

        SPDLOG_INFO("Recovering {} entrusts from shared memory", count);

        for (size_t i = 0; i < count; i++) {
            if (const auto& entrust = temp_entrusts[i];
                entrust.entno) {
                m_order_map[entrust.entno] = entrust;
                recovered_count++;

                // // 立即写入委托到数据库（仅实盘模式）
                // if constexpr (GlobalConst::IsRealTrading) {
                //     mysql_gateway->PostMsg(kDbSetOrder, std::make_shared<TBuffer>(entrust));
                // }
            }
        }

        SPDLOG_INFO("Recovered {} entrusts from shared memory", recovered_count);
    } else {
        SPDLOG_WARN("Entrust shared memory not valid, skip recovery");
    }

    // 恢复成交
    size_t recovered_done_count = 0;
    if (m_trade_shm.IsValid() && m_trade_shm->IsValid()) {
        constexpr size_t TEMP_BUFFER_SIZE = MAX_DONE_BUFFER_SIZE;
        std::vector<Trade> temp_dones(TEMP_BUFFER_SIZE);

        size_t count = m_trade_shm->ReadUnconfirmed(temp_dones.data(), TEMP_BUFFER_SIZE);

        SPDLOG_INFO("Recovering {} dones from shared memory", count);

        for (size_t i = 0; i < count; i++) {
            const auto& done = temp_dones[i];
            if (done.tdno) {
                m_trade_map[done.tdno] = done;
                recovered_done_count++;
            }
        }

        SPDLOG_INFO("Recovered {} dones from shared memory", recovered_done_count);
    } else {
        SPDLOG_WARN("Done shared memory not valid, skip recovery");
    }

    return recovered_count + recovered_done_count;
}

// 获取持久化统计信息
OrderManager::PersistenceStats OrderManager::GetPersistenceStats() const {
    PersistenceStats stats{};

    if (m_order_shm.IsValid()) {
        stats.order_write_seq = m_order_shm->GetWriteSeq();
        stats.order_confirmed_seq = m_order_shm->GetConfirmedSeq();
        stats.order_unconfirmed = m_order_shm->GetUnconfirmedCount();
    }

    if (m_trade_shm.IsValid()) {
        stats.trade_write_seq = m_trade_shm->GetWriteSeq();
        stats.trade_confirmed_seq = m_trade_shm->GetConfirmedSeq();
        stats.trade_unconfirmed = m_trade_shm->GetUnconfirmedCount();
    }

    return stats;
}

// ========== 策略持仓管理实现 ==========

Position& OrderManager::GetPortfolioPos(const std::string& strat_id,
                                      const std::string& market,
                                      const std::string& account_id,
                                      const std::string& instrument,
                                      char margin_mode,
                                      char pos_side) {
    const auto key = std::make_tuple(market, account_id, instrument, margin_mode, pos_side, strat_id);

    if (const auto iter = m_strat_pos_map.find(key);
        iter != m_strat_pos_map.end()) {
        return iter->second;
    }

    // 创建新的策略持仓
    Position pos {};
    zrt::fill_field(pos.market, market);
    zrt::fill_field(pos.account_id, account_id);
    zrt::fill_field(pos.instrument, instrument);
    zrt::fill_field(pos.margin_mode, margin_mode);
    zrt::fill_field(pos.pos_side, pos_side);
    zrt::fill_field(pos.portfolio, strat_id);  // 用 policy_no 存储 strat_id
    zrt::fill_field(pos.pos_source, PosSource::Strategy);      // 标记为策略持仓
    pos.local_time = MyUTC().Epoch19();

    auto [new_iter, inserted] = m_strat_pos_map.emplace(key, pos);
    return new_iter->second;
}

void OrderManager::UpdateStrategyPosition(const Trade& trade) {
    if (zrt::is_empty(trade.strat_id)) {
        SPDLOG_WARN("strat_id is empty for trade_no={}", trade.tdno);
        return;
    }

    // 获取或创建策略持仓
    // 注意：这里需要根据 pos_side 确定是多头还是空头
    auto& pos = GetPortfolioPos(trade);

    // 根据买卖方向和持仓方向更新持仓
    // 使用加权平均成本法

    const double done_amt = trade.td_qty;
    const double done_px = trade.td_px;
    const double done_value = done_amt * done_px;  // 成交金额

    if (zrt::equal(trade.td_side, TradeSide::Buy)) {  // 买入
        // 现金交易, 自动开平仓, 都按照多头逻辑计算持仓
        if (!zrt::equal(trade.pos_side, PosSide::Short)) {  // 开多
            // 增加多头持仓
            const double old_cost = pos.total_cost;
            const double old_amt = pos.available;

            pos.available += done_amt;
            pos.total_cost += done_value;
            pos.avg_px = pos.total_cost / pos.available;

            SPDLOG_INFO("Strategy position updated [OPEN LONG]: strat_id={}, inst={}, "
                       "old_amt={}, old_cost={}, add_amt={}, add_cost={}, "
                       "new_amt={}, new_cost={}, avg_px={}",
                       trade.strat_id, trade.instrument,
                       old_amt, old_cost, done_amt, done_value,
                       pos.available, pos.total_cost, pos.avg_px);
        }
        else {  // 平空
            // 减少空头持仓，计算已实现盈亏
            const double avg_short_px = pos.total_cost / pos.available;
            const double pnl = done_amt * (avg_short_px - done_px);  // 空头盈亏

            pos.realized_pnl += pnl;
            pos.available -= done_amt;
            pos.total_cost -= done_amt * avg_short_px;

            // 如果持仓清零，重置成本
            if (zrt::equal(pos.available, 0)) {
                pos.total_cost = 0;
                pos.avg_px = 0;
            }

            SPDLOG_INFO("Strategy position updated [CLOSE SHORT]: strat_id={}, inst={}, "
                       "close_amt={}, avg_px={}, close_px={}, pnl={}, "
                       "remain_amt={}, realized_pnl={}",
                       trade.strat_id, trade.instrument,
                       done_amt, avg_short_px, done_px, pnl,
                       pos.available, pos.realized_pnl);

            if (zrt::less(pos.available, 0)) {
                SPDLOG_ERROR("Insufficient short position: {}", zrt::to_str(pos));
            }
        }
    }
    else if (zrt::equal(trade.td_side, TradeSide::Sell)) {  // 卖出
        if (zrt::equal(trade.pos_side, PosSide::Short)) {  // 开空
            // 增加空头持仓
            const double old_cost = pos.total_cost;
            const double old_amt = pos.available;

            pos.available += done_amt;
            pos.total_cost += done_value;
            pos.avg_px = pos.total_cost / pos.available;

            SPDLOG_INFO("Strategy position updated [OPEN SHORT]: strat_id={}, inst={}, "
                       "old_amt={}, old_cost={}, add_amt={}, add_cost={}, "
                       "new_amt={}, new_cost={}, avg_px={}",
                       trade.strat_id, trade.instrument,
                       old_amt, old_cost, done_amt, done_value,
                       pos.available, pos.total_cost, pos.avg_px);
        }
        // 现金交易, 自动开平仓, 都按照多头逻辑计算持仓
        else {  // 平多
            // 减少多头持仓，计算已实现盈亏
            const double avg_long_px = pos.total_cost / pos.available;
            const double pnl = done_amt * (done_px - avg_long_px);  // 多头盈亏

            pos.realized_pnl += pnl;
            pos.available -= done_amt;
            pos.total_cost -= done_amt * avg_long_px;

            // 如果持仓清零，重置成本
            if (zrt::equal(pos.available, 0)) {
                pos.total_cost = 0;
                pos.avg_px = 0;
            }

            SPDLOG_INFO("Strategy position updated [CLOSE LONG]: strat_id={}, inst={}, "
                       "close_amt={}, avg_px={}, close_px={}, pnl={}, "
                       "remain_amt={}, realized_pnl={}",
                       trade.strat_id, trade.instrument,
                       done_amt, avg_long_px, done_px, pnl,
                       pos.available, pos.realized_pnl);

            if (zrt::less(pos.available, 0)) {
                SPDLOG_ERROR("Insufficient short position: {}", zrt::to_str(pos));
            }
        }
    }
    pos.local_time = MyUTC().Epoch19();
    SPDLOG_INFO("{}", zrt::to_str(pos));

    // 保存组合持仓到共享内存
    SavePortfolioPos2Shm(pos);
}

void OrderManager::UpdateAllStrategyPositionUpl(
    const std::unordered_map<std::string, std::unordered_map<std::string, double>>& last_price_map) {
    // 快速退出：如果没有任何策略持仓，直接返回
    if (m_strat_pos_map.empty()) {
        return;
    }

    int updated_count = 0;

    // 遍历所有策略持仓，根据最新价格更新未实现盈亏
    for (auto& [key, pos] : m_strat_pos_map) {
        // 如果持仓为0，跳过
        if (zrt::equal(pos.available, 0.0)) {
            continue;
        }

        // 查找该持仓对应的最新价格
        const auto market_it = last_price_map.find(std::string(pos.market));
        if (market_it == last_price_map.end()) {
            continue;
        }

        const auto inst_it = market_it->second.find(std::string(pos.instrument));
        if (inst_it == market_it->second.end()) {
            continue;
        }

        const double last_price = inst_it->second;

        // 计算未实现盈亏
        if (zrt::equal(pos.pos_side, PosSide::Long)) {
            pos.upl = (last_price - pos.avg_px) * pos.available;
        } else if (zrt::equal(pos.pos_side, PosSide::Short)) {
            pos.upl = (pos.avg_px - last_price) * pos.available;
        }

        ++updated_count;
    }

    if (updated_count > 0) {
        SPDLOG_DEBUG("Updated {} strategy positions' UPL", updated_count);
    }
}

// ========== WAL 相关实现 ==========

void OrderManager::InitWal(const gtrade::WalConfig& config) {
    if constexpr (GlobalConst::IsBackTest) {
        SPDLOG_INFO("WAL disabled in backtest mode");
        return;
    }

    try {
        m_wal_manager = std::make_unique<gtrade::WalManager>(config);
        if (!m_wal_manager->Init()) {
            SPDLOG_ERROR("WAL manager initialization failed");
            m_wal_manager.reset();
            return;
        }

        // 设置自动快照回调
        m_wal_manager->SetSnapshotCallback([this]() -> bool {
            const std::string path = SaveSnapshot();
            return !path.empty();
        });

        SPDLOG_INFO("WAL manager initialized, auto_snapshot_threshold={}MB",
                    config.auto_snapshot_wal_threshold_mb);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("WAL manager initialization exception: {}", e.what());
        m_wal_manager.reset();
    }
}

std::string OrderManager::SaveSnapshot() {
    if (!m_wal_manager) {
        SPDLOG_WARN("WAL manager not initialized, cannot save snapshot");
        return "";
    }

    // 收集当前状态
    std::vector<Order> orders;
    orders.reserve(m_order_map.size());
    for (const auto& [entno, order] : m_order_map) {
        orders.push_back(order);
    }

    std::vector<Trade> trades;
    trades.reserve(m_trade_map.size());
    for (const auto& [tdno, trade] : m_trade_map) {
        trades.push_back(trade);
    }

    std::vector<Position> positions;
    positions.reserve(m_pos_map.size());
    for (const auto& [key, pos] : m_pos_map) {
        positions.push_back(pos);
    }

    return m_wal_manager->SaveSnapshot(orders, trades, positions);
}

size_t OrderManager::FullRecover() {
    if (!m_wal_manager) {
        SPDLOG_WARN("WAL manager not initialized, cannot recover");
        return 0;
    }

    return m_wal_manager->FullRecover(
        [this](const Order& order) {
            if (order.entno) {
                m_order_map[order.entno] = order;
            }
        },
        [this](const Trade& trade) {
            if (trade.tdno) {
                m_trade_map[trade.tdno] = trade;
            }
        },
        [this](const Position& pos) {
            const auto key = GetPKey(pos);
            m_pos_map[key] = pos;
        }
    );
}

void OrderManager::QueryOpenOrdersByStratId(const std::string& strat_id,
                                             std::vector<RemoteSyncRespItem>& out) const {
    /**
     * 遍历 m_order_map，过滤指定策略的未完结订单。
     *
     * 未完结状态（活跃）：
     *   '0' 未报，'1' 正报，'2' 已报，'3' 部成，'5' 已报待撤，'7' 部成待撤，'I' 冻结，'A' 预埋
     * 已完结状态（跳过）：
     *   '4' 全部成交，'6' 场内撤单，'8' 部成部撤，'9' 废单
     *
     * client_order_id 使用 Order.private_no（Runner 侧分配的 client-side order ID）。
     * SyncResp 返回全量结果，Runner 的 pending_ids 仅用于日志/调试，不作过滤依据。
     */
    for (const auto& [entno, order] : m_order_map) {
        // 过滤策略：policy_no = strat_id
        if (strncmp(order.policy_no, strat_id.c_str(), sizeof(order.policy_no)) != 0) {
            continue;
        }

        // 跳过已完结订单
        const char s = order.status.get();
        if (s == OrderStatus::_4 || s == OrderStatus::_6 ||
            s == OrderStatus::_8 || s == OrderStatus::_9) {
            continue;
        }

        RemoteSyncRespItem item{};

        // client_order_id ← private_no
        strncpy(item.client_order_id, order.private_no,
                sizeof(item.client_order_id) - 1);

        // ex_order_id ← ex_entno（0 表示尚未确认，留空）
        if (order.ex_entno != 0) {
            snprintf(item.ex_order_id, sizeof(item.ex_order_id),
                     "%lld", static_cast<long long>(order.ex_entno));
        }

        item.status     = static_cast<int32_t>(s);
        item.filled_qty = order.filled;
        item.avg_price  = order.filled_px;
        item.remain_qty = order.remain;

        out.push_back(item);
    }
}


