//
// 系统状态快照
//
// 支持保存和恢复 OrderManager 状态
// 配合 WAL 实现完整的崩溃恢复
//

#pragma once

#include <cstdint>
#include <string>
#include <fstream>
#include <vector>
#include <filesystem>
#include "spdlog/spdlog.h"
#include "type_define.h"

namespace gtrade {

/**
 * 快照文件头
 */
struct SnapshotHeader {
    uint32_t magic {0x534E4150};  // "SNAP"
    uint32_t version {1};
    uint64_t wal_seq {0};         // 快照对应的 WAL 序号
    uint64_t create_time_ns {0};  // 创建时间
    uint32_t order_count {0};     // 订单数量
    uint32_t trade_count {0};     // 成交数量
    uint32_t position_count {0};  // 持仓数量
    uint32_t checksum {0};        // 校验和
    char reserved[24] {};
};
static_assert(sizeof(SnapshotHeader) == 64, "SnapshotHeader should be 64 bytes");

/**
 * 快照写入器
 */
class SnapshotWriter {
public:
    explicit SnapshotWriter(const std::string& dir)
        : dir_(dir) {
    }

    /**
     * 保存快照
     *
     * @param wal_seq 当前 WAL 序号
     * @param orders 订单列表
     * @param trades 成交列表
     * @param positions 持仓列表
     * @return 成功返回快照文件路径，失败返回空
     */
    std::string Save(uint64_t wal_seq,
                     const std::vector<Order>& orders,
                     const std::vector<Trade>& trades,
                     const std::vector<Position>& positions) const {
        // 创建目录
        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);
        if (ec) {
            SPDLOG_ERROR("Failed to create snapshot directory: {}", ec.message());
            return "";
        }

        // 生成文件名
        const std::string filename = dir_ + "/snapshot_" +
                                     std::to_string(wal_seq) + ".snap";

        std::ofstream file(filename, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            SPDLOG_ERROR("Failed to create snapshot file: {}", filename);
            return "";
        }

        // 准备头部
        SnapshotHeader header {};
        header.wal_seq = wal_seq;
        header.create_time_ns = zrt::DateTimeUTC().Epoch19();
        header.order_count = static_cast<uint32_t>(orders.size());
        header.trade_count = static_cast<uint32_t>(trades.size());
        header.position_count = static_cast<uint32_t>(positions.size());

        // 写入头部（先占位，后面更新校验和）
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));

        uint32_t checksum = 0;

        // 写入订单
        for (const auto& order : orders) {
            file.write(reinterpret_cast<const char*>(&order), sizeof(order));
            checksum ^= CalcChecksum(&order, sizeof(order));
        }

        // 写入成交
        for (const auto& trade : trades) {
            file.write(reinterpret_cast<const char*>(&trade), sizeof(trade));
            checksum ^= CalcChecksum(&trade, sizeof(trade));
        }

        // 写入持仓
        for (const auto& pos : positions) {
            file.write(reinterpret_cast<const char*>(&pos), sizeof(pos));
            checksum ^= CalcChecksum(&pos, sizeof(pos));
        }

        // 更新校验和
        header.checksum = checksum;
        file.seekp(0);
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));

        file.close();

        SPDLOG_INFO("Snapshot saved: {}, wal_seq={}, orders={}, trades={}, positions={}",
                    filename, wal_seq, orders.size(), trades.size(), positions.size());

        // 清理旧快照（保留最近 N 个）
        CleanOldSnapshots(3);

        return filename;
    }

    /**
     * 清理旧快照
     */
    void CleanOldSnapshots(const size_t keep_count) const {
        std::vector<std::string> snapshots;
        for (const auto& entry : std::filesystem::directory_iterator(dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".snap") {
                snapshots.push_back(entry.path().string());
            }
        }

        if (snapshots.size() <= keep_count) return;

        // 按文件名排序（包含序号，所以按字典序即可）
        std::sort(snapshots.begin(), snapshots.end());

        // 删除旧的
        for (size_t i = 0; i < snapshots.size() - keep_count; i++) {
            std::error_code ec;
            std::filesystem::remove(snapshots[i], ec);
            if (!ec) {
                SPDLOG_INFO("Deleted old snapshot: {}", snapshots[i]);
            }
        }
    }

private:
    static uint32_t CalcChecksum(const void* data, size_t len) {
        uint32_t sum = 0;
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; i++) {
            sum = (sum << 1) | (sum >> 31);
            sum ^= p[i];
        }
        return sum;
    }

    std::string dir_;
};

/**
 * 快照读取器
 */
class SnapshotReader {
public:
    explicit SnapshotReader(const std::string& dir)
        : dir_(dir) {
    }

    /**
     * 加载最新快照
     *
     * @param orders 输出订单列表
     * @param trades 输出成交列表
     * @param positions 输出持仓列表
     * @return 快照的 WAL 序号，失败返回 0
     */
    uint64_t LoadLatest(std::vector<Order>& orders,
                        std::vector<Trade>& trades,
                        std::vector<Position>& positions) {
        // 找最新的快照文件
        std::string latest_file;
        uint64_t latest_seq = 0;

        for (const auto& entry : std::filesystem::directory_iterator(dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".snap") {
                const uint64_t seq = ParseSeqFromFilename(entry.path().filename().string());
                if (seq > latest_seq) {
                    latest_seq = seq;
                    latest_file = entry.path().string();
                }
            }
        }

        if (latest_file.empty()) {
            SPDLOG_INFO("No snapshot found in {}", dir_);
            return 0;
        }

        return Load(latest_file, orders, trades, positions);
    }

    /**
     * 加载指定快照文件
     */
    uint64_t Load(const std::string& filepath,
                  std::vector<Order>& orders,
                  std::vector<Trade>& trades,
                  std::vector<Position>& positions) {
        orders.clear();
        trades.clear();
        positions.clear();

        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            SPDLOG_ERROR("Failed to open snapshot: {}", filepath);
            return 0;
        }

        // 读取头部
        SnapshotHeader header {};
        file.read(reinterpret_cast<char*>(&header), sizeof(header));

        if (header.magic != 0x534E4150) {
            SPDLOG_ERROR("Invalid snapshot magic: {}", filepath);
            return 0;
        }

        // 读取订单
        orders.resize(header.order_count);
        for (auto& order : orders) {
            file.read(reinterpret_cast<char*>(&order), sizeof(order));
        }

        // 读取成交
        trades.resize(header.trade_count);
        for (auto& trade : trades) {
            file.read(reinterpret_cast<char*>(&trade), sizeof(trade));
        }

        // 读取持仓
        positions.resize(header.position_count);
        for (auto& pos : positions) {
            file.read(reinterpret_cast<char*>(&pos), sizeof(pos));
        }

        SPDLOG_INFO("Snapshot loaded: {}, wal_seq={}, orders={}, trades={}, positions={}",
                    filepath, header.wal_seq, orders.size(), trades.size(), positions.size());

        return header.wal_seq;
    }

private:
    uint64_t ParseSeqFromFilename(const std::string& filename) const {
        // 解析 snapshot_seq.snap 格式
        const size_t underscore = filename.find('_');
        const size_t dot = filename.find('.');
        if (underscore != std::string::npos && dot != std::string::npos) {
            try {
                return std::stoull(filename.substr(underscore + 1, dot - underscore - 1));
            } catch (...) {}
        }
        return 0;
    }

    std::string dir_;
};

}  // namespace gtrade
