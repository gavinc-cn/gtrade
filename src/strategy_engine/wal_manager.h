//
// WAL 管理器
//
// 统一管理共享内存 WAL、文件 WAL 和快照
//

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include "spdlog/spdlog.h"
#include "shm_wal_ring.h"
#include "wal_entry.h"
#include "wal_file.h"
#include "snapshot.h"
#include "type_define.h"

namespace gtrade {

/**
 * WAL 配置
 */
struct WalConfig {
    // 共享内存 WAL
    bool shm_wal_enabled {false};
    std::string shm_wal_name {"gtrade_order_wal"};

    // 文件 WAL
    bool file_wal_enabled {false};
    std::string file_wal_dir {"/data/gtrade/wal"};
    size_t file_wal_sync_interval {100};  // 每 N 条 flush

    // 快照
    std::string snapshot_dir {"/data/gtrade/snapshot"};
    size_t snapshot_keep_count {3};  // 保留最近 N 个快照

    // 自动快照
    bool auto_snapshot_enabled {true};
    size_t auto_snapshot_wal_threshold_mb {100};  // WAL 超过此大小时自动触发快照 (MB)
};

/**
 * WAL 统计信息
 */
struct WalStats {
    // 共享内存 WAL
    uint64_t shm_write_pos {0};
    uint64_t shm_confirmed_pos {0};
    uint64_t shm_unconfirmed_bytes {0};

    // 文件 WAL
    uint64_t file_current_seq {0};
    size_t file_total_size_bytes {0};

    // 快照
    uint64_t last_snapshot_seq {0};
};

/**
 * 快照回调类型
 *
 * 当需要自动保存快照时调用此回调
 * 返回 true 表示成功保存
 */
using SnapshotCallback = std::function<bool()>;

/**
 * WAL 管理器
 *
 * 封装所有 WAL 相关操作：
 * - 共享内存 WAL（用于主备同步）
 * - 文件 WAL（用于持久化）
 * - 快照（用于加速恢复）
 */
class WalManager {
public:
    explicit WalManager(const WalConfig& config = {})
        : config_(config) {
    }

    ~WalManager() {
        Close();
    }

    // 禁止拷贝
    WalManager(const WalManager&) = delete;
    WalManager& operator=(const WalManager&) = delete;

    /**
     * 初始化
     */
    bool Init() {
        // 初始化共享内存 WAL
        if (config_.shm_wal_enabled) {
            if (!InitShmWal()) {
                return false;
            }
        }

        // 初始化文件 WAL
        if (config_.file_wal_enabled) {
            if (!InitFileWal()) {
                return false;
            }
        }

        return true;
    }

    /**
     * 关闭
     */
    void Close() {
        if (file_wal_) {
            file_wal_->Close();
            file_wal_.reset();
        }
        shm_wal_.reset();
    }

    // ========== 回调设置 ==========

    /**
     * 设置快照回调
     *
     * 当 WAL 超过阈值时自动调用此回调保存快照
     */
    void SetSnapshotCallback(SnapshotCallback callback) {
        snapshot_callback_ = std::move(callback);
    }

    // ========== 写入操作 ==========

    /**
     * 写入条目到共享内存 WAL
     *
     * 注意：文件 WAL 的写入由 gtrade_repl 独立进程负责，不在核心链路中执行，
     * 以避免文件 I/O 增加交易延时。
     *
     * 数据流：
     *   gtrade (主进程) -> 共享内存 WAL -> gtrade_repl -> 文件 WAL + 备机
     */
    template<typename T>
    uint64_t Append(WalEntryType type, const T& data) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

        uint64_t seq = 0;

        // 写入共享内存 WAL（核心链路，低延时）
        if (shm_wal_ && shm_wal_->IsValid()) {
            seq = shm_wal_->Append(type, data);
            if (seq == 0) {
                SPDLOG_WARN("ShmWalRing append failed, buffer may be full");
            }
        }

        // 文件 WAL 写入已移至 gtrade_repl 进程
        // 如果需要在主进程中直接写入文件（如单机非 HA 模式），调用 AppendToFileWal()

        return seq;
    }

    /**
     * 直接写入文件 WAL（用于非 HA 模式或恢复场景）
     *
     * 注意：此方法会阻塞，不应在核心交易链路中调用
     */
    template<typename T>
    uint64_t AppendToFileWal(WalEntryType type, const T& data) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

        if (!file_wal_) {
            return 0;
        }

        const uint64_t seq = file_wal_->Append(type, data);
        if (seq == 0) {
            SPDLOG_ERROR("FileWal append failed");
        }

        // 检查是否需要自动快照
        CheckAutoSnapshot();

        return seq;
    }

    /**
     * 强制刷盘（文件 WAL）
     */
    void Sync() {
        if (file_wal_) {
            file_wal_->Sync();
        }
    }

    // ========== 快照操作 ==========

    /**
     * 保存快照
     *
     * @param orders 订单列表
     * @param trades 成交列表
     * @param positions 持仓列表
     * @return 快照文件路径，失败返回空
     */
    std::string SaveSnapshot(const std::vector<Order>& orders,
                             const std::vector<Trade>& trades,
                             const std::vector<Position>& positions) {
        const uint64_t seq = GetCurrentSeq();

        const SnapshotWriter writer(config_.snapshot_dir);
        const std::string path = writer.Save(seq, orders, trades, positions);

        if (!path.empty()) {
            last_snapshot_seq_ = seq;

            // 删除旧 WAL 文件
            if (file_wal_) {
                file_wal_->TruncateBefore(seq);
            }
        }

        return path;
    }

    // ========== 恢复操作 ==========

    /**
     * 恢复回调类型
     */
    using OrderCallback = std::function<void(const Order&)>;
    using TradeCallback = std::function<void(const Trade&)>;
    using PositionCallback = std::function<void(const Position&)>;

    /**
     * 完整恢复（快照 + WAL）
     *
     * @return 恢复的条目数量
     */
    size_t FullRecover(const OrderCallback& on_order,
                       const TradeCallback& on_trade,
                       const PositionCallback& on_position) {
        size_t total = 0;

        // 1. 加载快照
        std::vector<Order> orders;
        std::vector<Trade> trades;
        std::vector<Position> positions;

        SnapshotReader reader(config_.snapshot_dir);
        const uint64_t snapshot_seq = reader.LoadLatest(orders, trades, positions);

        for (const auto& order : orders) {
            on_order(order);
            total++;
        }
        for (const auto& trade : trades) {
            on_trade(trade);
            total++;
        }
        for (const auto& pos : positions) {
            on_position(pos);
            total++;
        }

        SPDLOG_INFO("Loaded snapshot: {} entries, seq={}", total, snapshot_seq);

        // 2. 回放文件 WAL
        if (config_.file_wal_enabled) {
            const size_t wal_count = ReplayFileWal(
                snapshot_seq, on_order, on_trade, on_position);
            total += wal_count;
            SPDLOG_INFO("Replayed file WAL: {} entries", wal_count);
        }

        // 3. 回放共享内存 WAL（未确认部分）
        if (shm_wal_ && shm_wal_->IsValid()) {
            const size_t shm_count = ReplayShmWal(on_order, on_trade, on_position);
            total += shm_count;
            SPDLOG_INFO("Replayed shm WAL: {} entries", shm_count);
        }

        return total;
    }

    // ========== 状态查询 ==========

    /**
     * 获取当前序号
     */
    uint64_t GetCurrentSeq() const {
        if (file_wal_) {
            return file_wal_->GetCurrentSeq();
        }
        if (shm_wal_ && shm_wal_->IsValid()) {
            return shm_wal_->GetNextSeq() - 1;
        }
        return 0;
    }

    /**
     * 获取统计信息
     */
    WalStats GetStats() const {
        WalStats stats {};

        if (shm_wal_ && shm_wal_->IsValid()) {
            stats.shm_write_pos = shm_wal_->GetWritePos();
            stats.shm_confirmed_pos = shm_wal_->GetConfirmedPos();
            stats.shm_unconfirmed_bytes = shm_wal_->GetUnconfirmedBytes();
        }

        if (file_wal_) {
            stats.file_current_seq = file_wal_->GetCurrentSeq();
            stats.file_total_size_bytes = file_wal_->GetTotalSize();
        }

        stats.last_snapshot_seq = last_snapshot_seq_;

        return stats;
    }

    /**
     * 获取 WAL 文件总大小（MB）
     */
    size_t GetWalFileSizeMB() const {
        if (file_wal_) {
            return file_wal_->GetTotalSize() / (1024 * 1024);
        }
        return 0;
    }

    /**
     * 检查是否需要快照
     */
    bool NeedSnapshot() const {
        if (!config_.auto_snapshot_enabled || !file_wal_) {
            return false;
        }
        return GetWalFileSizeMB() >= config_.auto_snapshot_wal_threshold_mb;
    }

    /**
     * 手动触发快照检查
     *
     * @return 如果触发了快照返回 true
     */
    bool TriggerSnapshotIfNeeded() {
        if (NeedSnapshot() && snapshot_callback_) {
            SPDLOG_INFO("WAL size {}MB >= threshold {}MB, triggering auto snapshot",
                        GetWalFileSizeMB(), config_.auto_snapshot_wal_threshold_mb);
            return snapshot_callback_();
        }
        return false;
    }

    /**
     * 获取共享内存 WAL（用于复制服务）
     */
    ShmWalRing* GetShmWal() const { return shm_wal_.get(); }

    /**
     * 是否启用
     */
    bool IsEnabled() const {
        return (shm_wal_ && shm_wal_->IsValid()) || (file_wal_ != nullptr);
    }

private:
    bool InitShmWal() {
        try {
            shm_wal_ = std::make_unique<ShmWalRing>(config_.shm_wal_name);
            if (!shm_wal_->IsValid()) {
                SPDLOG_ERROR("Failed to initialize ShmWalRing: {}", config_.shm_wal_name);
                shm_wal_.reset();
                return false;
            }
            SPDLOG_INFO("ShmWalRing initialized: {}", config_.shm_wal_name);
            return true;
        } catch (const std::exception& e) {
            SPDLOG_ERROR("ShmWalRing init exception: {}", e.what());
            return false;
        }
    }

    bool InitFileWal() {
        try {
            file_wal_ = std::make_unique<WalFileWriter>(config_.file_wal_dir);
            file_wal_->SetSyncInterval(config_.file_wal_sync_interval);
            if (!file_wal_->Init()) {
                SPDLOG_ERROR("Failed to initialize FileWal: {}", config_.file_wal_dir);
                file_wal_.reset();
                return false;
            }
            SPDLOG_INFO("FileWal initialized: {}", config_.file_wal_dir);
            return true;
        } catch (const std::exception& e) {
            SPDLOG_ERROR("FileWal init exception: {}", e.what());
            return false;
        }
    }

    size_t ReplayFileWal(uint64_t after_seq,
                         const OrderCallback& on_order,
                         const TradeCallback& on_trade,
                         const PositionCallback& on_position) {
        WalFileReader reader(config_.file_wal_dir);
        std::vector<std::pair<WalEntryHeader, std::vector<char>>> entries;
        reader.ReadAfter(after_seq, entries);

        size_t count = 0;
        for (const auto& [header, data] : entries) {
            if (ReplayEntry(header, data, on_order, on_trade, on_position)) {
                count++;
            }
        }
        return count;
    }

    size_t ReplayShmWal(const OrderCallback& on_order,
                        const TradeCallback& on_trade,
                        const PositionCallback& on_position) {
        std::vector<std::pair<WalEntryHeader, std::vector<char>>> entries;
        shm_wal_->ReadUnconfirmed(entries, 100000);

        size_t count = 0;
        for (const auto& [header, data] : entries) {
            if (ReplayEntry(header, data, on_order, on_trade, on_position)) {
                count++;
            }
        }
        return count;
    }

    bool ReplayEntry(const WalEntryHeader& header,
                     const std::vector<char>& data,
                     const OrderCallback& on_order,
                     const TradeCallback& on_trade,
                     const PositionCallback& on_position) {
        switch (header.type) {
            case WalEntryType::kOrder:
                if (data.size() >= sizeof(Order)) {
                    on_order(*reinterpret_cast<const Order*>(data.data()));
                    return true;
                }
                break;
            case WalEntryType::kTrade:
                if (data.size() >= sizeof(Trade)) {
                    on_trade(*reinterpret_cast<const Trade*>(data.data()));
                    return true;
                }
                break;
            case WalEntryType::kPosition:
                if (data.size() >= sizeof(Position)) {
                    on_position(*reinterpret_cast<const Position*>(data.data()));
                    return true;
                }
                break;
            default:
                break;
        }
        return false;
    }

    /**
     * 检查是否需要自动快照
     */
    void CheckAutoSnapshot() {
        // 每 1000 次写入检查一次，避免频繁检查文件大小
        if (++append_count_ % 1000 != 0) {
            return;
        }
        TriggerSnapshotIfNeeded();
    }

    WalConfig config_;
    std::unique_ptr<ShmWalRing> shm_wal_;
    std::unique_ptr<WalFileWriter> file_wal_;
    uint64_t last_snapshot_seq_ {0};
    SnapshotCallback snapshot_callback_;
    uint64_t append_count_ {0};
};

}  // namespace gtrade
