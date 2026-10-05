//
// Created by dell on 2025/2/26.
//

#pragma once

#include "pch.h"
#include <dict.h>
#include <algorithm>
#include <chrono>
#include "zrtools/zrt_time.h"
#include "i_exchange_data_dump.h"
#include "i_strategy_engine_dump.h"
#include "zrtools/zrt_bmic.h"
#include "tuple_hash_map.h"
#include "zrtools/zrt_define.h"
#include "zrtools/zrt_shm_direct.h"
#include "persistence_buffer.h"
#include "db_structures_dump.h"
#include "my_utc.h"
#include "zrtools/zrt_bmic_hashed.h"
#include "wal_manager.h"
#include "i_strategy_channel.h"  // RemoteSyncRespItem（远程模式对账）
#include <vector>


class OrderManager {
public:
    // 构造函数：初始化共享内存
    OrderManager();

    // 业务号生成：基数 + 进程内自增（基数由 SetIdBase 在启动期按高水位设置）
    static int64_t CreateOrderId() {return start_ordno.load(std::memory_order_relaxed) + ++order_id_seq;}
    static int64_t CreateTradeId() {return start_trdno.load(std::memory_order_relaxed) + ++trade_id_seq;}

    // 设置号段基数（启动期调用一次，重复调用只会抬高不会降低）：
    // 取"当前时间基数"与"已见最大号 + 1"的较大者，保证跨重启（含同一秒内重启）
    // 生成的 entno/tdno 单调递增、不与历史号重叠（方案 rev4 §8）。
    static void SetIdBase(int64_t seen_max_entno, int64_t seen_max_tdno);
    // 取当前号段基数（诊断/日志用）
    static int64_t GetOrderIdBase() {return start_ordno.load(std::memory_order_relaxed);}
    static int64_t GetTradeIdBase() {return start_trdno.load(std::memory_order_relaxed);}
    // 已见最大号（内存 + 从 SHM 恢复的部分），供启动高水位取上界
    int64_t GetMaxOrderNo() const;
    int64_t GetMaxTradeNo() const;

    // ===== 补查（web_server ↔ 引擎）：读内存权威态，不查库 =====
    // 取 entno > cursor_entno 的委托，按 entno 升序，最多 limit 条（limit==0 视为不限）
    std::vector<Order> QueryOrdersAfter(int64_t cursor_entno, size_t limit) const;
    // 按委托号集合取委托（含终态；不存在的号自动跳过），返回顺序与 entnos 一致
    std::vector<Order> QueryOrdersByEntnos(const int64_t* entnos, size_t count) const;
    // 取 tdno > cursor_tdno 的成交，按 tdno 升序，最多 limit 条（limit==0 视为不限）
    std::vector<Trade> QueryTradesAfter(int64_t cursor_tdno, size_t limit) const;

    // 补查通用实现（纯函数，静态可单测，不依赖实例/共享内存）：
    // 取 号 > cursor 的记录，按号升序，最多 limit 条（limit==0 视为不限）
    template <typename T>
    static std::vector<T> SelectAfterCursor(const std::unordered_map<int64_t, T>& rows,
                                            const int64_t cursor, const size_t limit) {
        std::vector<int64_t> keys {};
        keys.reserve(rows.size());
        for (const auto& [no, row] : rows) {
            if (no > cursor) {
                keys.push_back(no);
            }
        }
        std::sort(keys.begin(), keys.end());
        if (limit > 0 && keys.size() > limit) {
            keys.resize(limit);
        }
        std::vector<T> result {};
        result.reserve(keys.size());
        for (const int64_t no : keys) {
            const auto iter = rows.find(no);
            if (iter != rows.end()) {
                result.push_back(iter->second);
            }
        }
        return result;
    }

    // 取 号 ∈ keys 的记录（含终态），按 keys 顺序返回，缺失的号跳过
    template <typename T>
    static std::vector<T> SelectByKeys(const std::unordered_map<int64_t, T>& rows,
                                       const int64_t* keys, const size_t count) {
        std::vector<T> result {};
        if (keys == nullptr) {
            return result;
        }
        result.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const auto iter = rows.find(keys[i]);
            if (iter != rows.end()) {
                result.push_back(iter->second);
            } else {
                // 不在内存里（超出启动加载窗口 / 非本引擎下单）：跳过并记 DEBUG 便于排查
                SPDLOG_DEBUG("query by keys: no={} not in memory", keys[i]);
            }
        }
        return result;
    }

    size_t GetOrderCount() const {return m_order_map.size();}
    size_t GetTradeCount() const {return m_trade_map.size();}
    std::string DumpOrder() const {return zrt::to_str(m_order_map);}
    std::string DumpTrade() const {return zrt::to_str(m_trade_map);}

    void RecoverOrder(const Order& order);
    void SaveOrder2Shm(const Order& order);
    void SaveTrade2Shm(const Trade& trade);
    void SavePos2Shm(const Position& pos);
    void SavePortfolioPos2Shm(const Position& pos);
    void SaveBalance2Shm(const Balance& bal);
    Order* AddOrder(const Order& order);
    void UpdateOrder(const Order& order);
    void AddTrade(const Trade& trade);
    static bool IsForwardStatus(const char old_status, const char new_status);
    static bool IsForwardOrder(const int64_t old_update_time, const double old_filled, const CharCs old_status, const Order& new_order);

    static bool IsForwardOrder(const Order& old_order, const Order& new_order) {
        return IsForwardOrder(old_order.update_time, old_order.filled, old_order.status, new_order);
    }

    // void UpdateEntrust(Entrust& entrust);
    // void UpdateEntrustByQuery(const Entrust& entrust);
    // Entrust& RefEntrust(Entrust& recv_ent, const std::function<void(Entrust&)>& func);
    // Entrust& RefEntrust(const Entrust& recv_ent);
    // Entrust* FindEntrust(const Entrust& entrust);
    Order* FindLocalOrder(const int64_t entno);
    Order* FindLocalOrder(const Order& order) {return FindLocalOrder(order.entno);}
    int64_t FindOrderNo(const std::string& market, const int64_t ex_entno);
    int64_t FindOrderNo(const Order& order) {return FindOrderNo(order.market, order.ex_entno);}
    Order* FindLocalOrderByPrivateNo(const std::string_view private_no);

    // 表示已经从websocket收到了最新的委托, 不再采纳查询收到的委托, 当websocket断线时应当清空Up2date相关的map
    bool IsUp2date(const std::string_view account_id, const int entno) {
        return m_order_up2date_map[account_id.data()].count(entno);
    }

    void SetUp2date(const std::string_view account_id, const int entno) {
        m_order_up2date_map[account_id.data()].emplace(entno);
    }

    void ClearOrderUp2date(const std::string_view account_id) {
        m_order_up2date_map[account_id.data()].clear();
    }

    bool IsUp2date(const Position& pos) {
        return m_pos_up2date_map[pos.account_id].count(GetPKey(pos));
    }

    void SetUp2date(const Position& pos) {
        m_pos_up2date_map[pos.account_id].emplace(GetPKey(pos));
    }

    void ClearPosUp2date(const std::string_view account_id) {
        m_pos_up2date_map[account_id.data()].clear();
    }

    bool IsUp2date(const Balance& bal) {
        return m_balance_up2date_map[bal.account_id].count(GetPKey(bal));
    }

    void SetUp2date(const Balance& bal) {
        m_balance_up2date_map[bal.account_id].emplace(GetPKey(bal));
    }

    void ClearBalanceUp2date(const std::string_view account_id) {
        m_balance_up2date_map[account_id.data()].clear();
    }

    void UpdatePos(const Position& pos) {
        m_pos_map[GetPKey(pos)] = pos;
        SavePos2Shm(pos);
    }

    void UpdateBalance(const Balance& bal) {
        m_balance_map[GetPKey(bal)] = bal;
        SaveBalance2Shm(bal);
    }

    Position& GetPos(const std::string_view account_id, const std::string_view instrument, const char trade_mode, const char pos_side) {
        auto key = std::make_tuple(account_id.data(), instrument.data(), trade_mode, pos_side);
        if (const auto iter = m_pos_map.find(key);
            ZRT_LIKELY(iter != m_pos_map.end())) {
            return iter->second;
        }
        Position pos {};
        zrt::fill_field(pos.account_id, account_id);
        zrt::fill_field(pos.instrument, instrument);
        zrt::fill_field(pos.margin_mode, trade_mode);
        zrt::fill_field(pos.pos_side, pos_side);
        return m_pos_map.emplace(key, pos).first->second;
    }

    Position& GetPos(const Order& order) {
        return GetPos(order.account_id, order.inst_id, order.trade_mode, order.pos_side);
    }

    // ========== 策略持仓管理 ==========

    // 获取策略持仓
    Position& GetPortfolioPos(const std::string& strat_id,
                            const std::string& market,
                            const std::string& account_id,
                            const std::string& instrument,
                            char margin_mode,
                            char pos_side);
    Position& GetPortfolioPos(const Trade& trade) {
        return GetPortfolioPos(trade.strat_id, trade.market, trade.account_id, trade.instrument, trade.margin_mode, trade.pos_side);
    }

    // 更新策略持仓（基于成交）
    void UpdateStrategyPosition(const Trade& trade);
    // 全量更新所有策略持仓的未实现盈亏（用于定时器触发）
    // 参数：last_price_map - market -> instrument -> last_price 的嵌套map
    void UpdateAllStrategyPositionUpl(const std::unordered_map<std::string,std::unordered_map<std::string, double>>& last_price_map);
    // 从共享内存恢复委托和成交
    size_t RecoverFromShm();

    /**
     * 查询指定策略的全量活跃订单（用于远程模式 kRemoteSyncResp）。
     *
     * 活跃订单：status 不是 '4'（全部成交）、'6'（场内撤单）、'8'（部成部撤）、'9'（废单）的订单。
     * 字段映射：
     *   client_order_id ← Order.private_no （Runner 侧的 client_order_id）
     *   ex_order_id     ← Order.ex_entno 转字符串（0 时留空）
     *   status          ← Order.status.get()
     *   filled_qty      ← Order.filled
     *   avg_price       ← Order.filled_px
     *   remain_qty      ← Order.remain
     *
     * @param strat_id  策略 ID（与 Order.policy_no 对应）
     * @param out       输出列表，追加方式（不清空）
     */
    void QueryOpenOrdersByStratId(const std::string& strat_id,
                                  std::vector<RemoteSyncRespItem>& out) const;

    // 获取持久化统计信息
    struct PersistenceStats {
        size_t order_write_seq;
        size_t order_confirmed_seq;
        size_t order_unconfirmed;
        size_t trade_write_seq;
        size_t trade_confirmed_seq;
        size_t trade_unconfirmed;
    };
    PersistenceStats GetPersistenceStats() const;

private:
    // <account_id, instrument, margin_mode, pos_side>
    using PosPKey = std::tuple<std::string,std::string,char,char>;
    // <market,account_id,instrument,margin_mode,pos_side,strat_id>
    using StratPosPKey = std::tuple<std::string,std::string,std::string,char,char,std::string>;

    // 号段基数：默认取进程启动时刻（秒 ×1e9，即 epoch19 纳秒量级）；
    // 启动加载历史数据后由 SetIdBase 抬到高水位（见头文件声明处注释）。
    // 注意：不能改成毫秒基数——Epoch10()*kGiga 已是 1.8e18 量级，
    //       再乘 1e3 会溢出 int64（上限 9.22e18）。
    static inline std::atomic<int64_t> start_ordno {MyUTC().Epoch10() * zrt::kGiga};
    static inline std::atomic<int64_t> start_trdno {MyUTC().Epoch10() * zrt::kGiga};
    static inline std::atomic<int64_t> order_id_seq {};
    static inline std::atomic<int64_t> trade_id_seq {};
    // <entno,Order>
    std::unordered_map<int64_t,Order> m_order_map {};
    // <market,<ex_entno,entno>>
    std::unordered_map<std::string,std::unordered_map<int64_t,int64_t>> m_foreign_order_map {};
    std::unordered_map<int64_t,Trade> m_trade_map {};
    // 已收到websocket推送的委托号集合
    // todo 需要按照account_id区分?
    std::unordered_map<std::string,std::unordered_set<int64_t>> m_order_up2date_map {};
    std::unordered_map<std::string,std::unordered_set<PosPKey,zrt::TupleHasher>> m_pos_up2date_map {};
    // std::unordered_map<std::string,std::unordered_set<zrt::PrimeKey<Position>,zrt::TupleHasher>> m_hold_up2date_map {};
    std::unordered_map<std::string,std::unordered_set<zrt::PrimeKey<Balance>,zrt::TupleHasher>> m_balance_up2date_map {};
    zrt::TupleHashMap<Balance> m_balance_map {};

    // zrt::TupleHashMap<Position> m_hold_map {};
    // 由持仓推送获得
    std::unordered_map<PosPKey,Position,zrt::TupleHasher> m_pos_map {};
    // 通过成交计算出
    std::unordered_map<StratPosPKey,Position,zrt::TupleHasher> m_strat_pos_map {};
    // zrt::BMIC<ZRT_BMIC(Position,
    //     ZRT_BMI_HASHED(6, unique, TagPrimeKey, Position, market, account_id, instrument, margin_mode,pos_side, policy_no),
    //     ZRT_BMI_HASHED(2, non_unique, TagPrimeKey2, Position, market, instrument)),
    //     Position> m_strat_pos_map2 {};

    // 持久化共享内存
    zrt::DirectShm<OrderPersistenceBuffer> m_order_shm {kOrderShmName};
    zrt::DirectShm<TradePersistenceBuffer> m_trade_shm {kTradeShmName};
    zrt::DirectShm<PositionPersistenceBuffer> m_pos_shm {kPositionShmName};
    zrt::DirectShm<PortfolioPositionPersistenceBuffer> m_portfolio_pos_shm {kPortfolioPositionShmName};
    zrt::DirectShm<BalancePersistenceBuffer> m_balance_shm {kBalanceShmName};

    // WAL 管理器
    std::unique_ptr<gtrade::WalManager> m_wal_manager;

public:
    // ========== WAL 相关方法 ==========

    /**
     * 初始化 WAL 管理器
     */
    void InitWal(const gtrade::WalConfig& config);

    /**
     * 检查 WAL 是否已启用
     */
    bool IsWalEnabled() const { return m_wal_manager && m_wal_manager->IsEnabled(); }

    /**
     * 获取 WAL 管理器
     */
    gtrade::WalManager* GetWalManager() const { return m_wal_manager.get(); }

    /**
     * 获取共享内存 WAL（用于复制服务）
     */
    gtrade::ShmWalRing* GetShmWal() const {
        return m_wal_manager ? m_wal_manager->GetShmWal() : nullptr;
    }

    /**
     * 保存快照
     */
    std::string SaveSnapshot();

    /**
     * 完整恢复（快照 + WAL）
     */
    size_t FullRecover();

    /**
     * 获取 WAL 统计信息
     */
    gtrade::WalStats GetWalStats() const {
        return m_wal_manager ? m_wal_manager->GetStats() : gtrade::WalStats{};
    }

private:
    /**
     * 写入数据到 WAL
     */
    template<typename T>
    void WriteToWal(gtrade::WalEntryType type, const T& data) {
        if (m_wal_manager) {
            m_wal_manager->Append(type, data);
        }
    }
};

