//
// Created by Claude Code
// 通用 WAL 文件持久化
//
// 将 WAL 写入文件，支持崩溃恢复
//

#pragma once

#include <cstdint>
#include <string>
#include <fstream>
#include <mutex>
#include <atomic>
#include <vector>
#include <filesystem>
#include <functional>
#include "spdlog/spdlog.h"
#include "zrt_wal_entry.h"

namespace zrt {

/**
 * WAL 文件头
 */
struct WalFileHeader {
    uint32_t magic {0x5A524657};  // "ZRFW" (ZRT File WAL)
    uint32_t version {1};
    uint64_t start_seq {0};       // 文件中第一条记录的序号
    uint64_t entry_count {0};     // 条目数量
    uint64_t create_time_ns {0};  // 创建时间
    char reserved[32] {};
};
static_assert(sizeof(WalFileHeader) == 64, "WalFileHeader should be 64 bytes");

/**
 * 通用 WAL 文件写入器
 *
 * @tparam TypeEnum 条目类型枚举类型
 */
template<typename TypeEnum>
class WalFileWriter {
public:
    /**
     * 时间戳获取函数类型
     * 默认使用 GetNanoTimestamp()
     */
    using TimestampFunc = std::function<uint64_t()>;

    explicit WalFileWriter(const std::string& dir,
                           const std::string& prefix = "wal",
                           TimestampFunc timestamp_func = nullptr)
        : dir_(dir)
        , prefix_(prefix)
        , current_seq_(0)
        , entry_count_(0)
        , timestamp_func_(timestamp_func ? timestamp_func : []{return zrt::DateTimeUTC().Epoch19();})
    {
    }

    ~WalFileWriter() {
        Close();
    }

    /**
     * 初始化，扫描现有文件确定起始序号
     */
    bool Init() {
        // 创建目录
        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);
        if (ec) {
            SPDLOG_ERROR("Failed to create WAL directory {}: {}", dir_, ec.message());
            return false;
        }

        // 扫描现有 WAL 文件，找到最大序号
        uint64_t max_seq = 0;
        for (const auto& entry : std::filesystem::directory_iterator(dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".wal") {
                const uint64_t seq = ParseSeqFromFilename(entry.path().filename().string());
                if (seq > max_seq) {
                    max_seq = seq;
                    max_seq = GetLastSeqInFile(entry.path().string());
                }
            }
        }
        current_seq_ = max_seq;

        return CreateNewFile();
    }

    /**
     * 追加写入条目
     */
    uint64_t Append(const TypeEnum type, const void* data, const uint16_t len) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!file_.is_open()) {
            SPDLOG_ERROR("WAL file not open");
            return 0;
        }

        const uint64_t seq = ++current_seq_;
        const uint64_t timestamp_ns = timestamp_func_();

        // 构造条目头
        WalEntryHeader<TypeEnum> header {};
        header.seq = seq;
        header.timestamp_ns = timestamp_ns;
        header.type = type;
        header.data_len = len;
        header.crc32 = WalEntryHeader<TypeEnum>::CalcCrc32(data, len);

        // 写入头部
        file_.write(reinterpret_cast<const char*>(&header), sizeof(header));

        // 写入数据
        if (len > 0 && data != nullptr) {
            file_.write(static_cast<const char*>(data), len);
        }

        entry_count_++;

        // 可选：每 N 条 flush
        if (entry_count_ % sync_interval_ == 0) {
            file_.flush();
        }

        return seq;
    }

    /**
     * 模板版本的追加写入
     */
    template<typename T>
    uint64_t Append(const TypeEnum type, const T& data) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        return Append(type, &data, static_cast<uint16_t>(sizeof(T)));
    }

    /**
     * 强制刷盘
     */
    void Sync() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.flush();
        }
    }

    /**
     * 关闭当前文件
     */
    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            UpdateFileHeader();
            file_.close();
        }
    }

    /**
     * 获取当前序号
     */
    uint64_t GetCurrentSeq() const { return current_seq_; }

    /**
     * 设置同步间隔
     */
    void SetSyncInterval(const size_t interval) { sync_interval_ = interval; }

    /**
     * 获取所有 WAL 文件的总大小（字节）
     */
    size_t GetTotalSize() const {
        size_t total = 0;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir_, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".wal") {
                total += entry.file_size(ec);
            }
        }
        return total;
    }

    /**
     * 删除指定序号之前的 WAL 文件
     */
    size_t TruncateBefore(const uint64_t seq) const {
        size_t deleted = 0;
        for (const auto& entry : std::filesystem::directory_iterator(dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".wal") {
                const uint64_t file_end_seq = GetLastSeqInFile(entry.path().string());

                // 如果整个文件的序号都小于指定序号，删除它
                if (file_end_seq < seq && entry.path() != current_file_) {
                    std::error_code ec;
                    std::filesystem::remove(entry.path(), ec);
                    if (!ec) {
                        deleted++;
                        SPDLOG_INFO("Deleted old WAL file: {}", entry.path().string());
                    }
                }
            }
        }
        return deleted;
    }

private:
    bool CreateNewFile() {
        current_file_ = dir_ + "/" + prefix_ + "_" +
                        std::to_string(current_seq_ + 1) + ".wal";

        file_.open(current_file_, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!file_.is_open()) {
            SPDLOG_ERROR("Failed to create WAL file: {}", current_file_);
            return false;
        }

        // 写入文件头
        WalFileHeader header {};
        header.start_seq = current_seq_ + 1;
        header.create_time_ns = timestamp_func_();
        file_.write(reinterpret_cast<const char*>(&header), sizeof(header));

        entry_count_ = 0;
        SPDLOG_INFO("Created WAL file: {}", current_file_);
        return true;
    }

    void UpdateFileHeader() {
        if (!file_.is_open()) return;

        file_.seekp(0);

        WalFileHeader header {};
        header.start_seq = current_seq_ - entry_count_ + 1;
        header.entry_count = entry_count_;
        header.create_time_ns = timestamp_func_();

        file_.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file_.seekp(0, std::ios::end);
    }

    static uint64_t ParseSeqFromFilename(const std::string& filename) {
        const size_t underscore = filename.find('_');
        const size_t dot = filename.find('.');
        if (underscore != std::string::npos && dot != std::string::npos) {
            try {
                return std::stoull(filename.substr(underscore + 1, dot - underscore - 1));
            } catch (...) {}
        }
        return 0;
    }

    uint64_t GetLastSeqInFile(const std::string& filepath) const {
        std::ifstream in(filepath, std::ios::binary);
        if (!in.is_open()) return 0;

        WalFileHeader header {};
        in.read(reinterpret_cast<char*>(&header), sizeof(header));

        // 兼容旧版本的 magic（gtrade 使用 0x57414C46 "WALF"）
        if (header.magic != 0x5A524657 && header.magic != 0x57414C46) return 0;

        uint64_t last_seq = header.start_seq;
        while (in.good()) {
            WalEntryHeader<TypeEnum> entry_header {};
            in.read(reinterpret_cast<char*>(&entry_header), sizeof(entry_header));
            if (!in.good() || entry_header.seq == 0) break;

            last_seq = entry_header.seq;
            in.seekg(entry_header.data_len, std::ios::cur);
        }

        return last_seq;
    }

    std::string dir_;
    std::string prefix_;
    std::string current_file_;
    std::ofstream file_;
    std::mutex mutex_;
    uint64_t current_seq_;
    uint64_t entry_count_;
    size_t sync_interval_ {100};
    TimestampFunc timestamp_func_;
};

/**
 * 通用 WAL 文件读取器
 *
 * @tparam TypeEnum 条目类型枚举类型
 */
template<typename TypeEnum>
class WalFileReader {
public:
    explicit WalFileReader(const std::string& dir, const std::string& prefix = "wal")
        : dir_(dir)
        , prefix_(prefix) {
    }

    /**
     * 读取指定序号之后的所有条目
     */
    size_t ReadAfter(const uint64_t after_seq,
                     std::vector<std::pair<WalEntryHeader<TypeEnum>, std::vector<char>>>& entries,
                     const size_t max_count = 0) {
        entries.clear();

        // 收集所有 WAL 文件并排序
        std::vector<std::string> files;
        for (const auto& entry : std::filesystem::directory_iterator(dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".wal") {
                files.push_back(entry.path().string());
            }
        }
        std::sort(files.begin(), files.end());

        // 依次读取
        for (const auto& filepath : files) {
            std::ifstream in(filepath, std::ios::binary);
            if (!in.is_open()) continue;

            // 读取文件头
            WalFileHeader file_header {};
            in.read(reinterpret_cast<char*>(&file_header), sizeof(file_header));
            // 兼容旧版本的 magic
            if (file_header.magic != 0x5A524657 && file_header.magic != 0x57414C46) continue;

            // 读取条目
            while (in.good()) {
                WalEntryHeader<TypeEnum> entry_header {};
                in.read(reinterpret_cast<char*>(&entry_header), sizeof(entry_header));
                if (!in.good() || entry_header.seq == 0) break;

                std::vector<char> data(entry_header.data_len);
                if (entry_header.data_len > 0) {
                    in.read(data.data(), entry_header.data_len);
                }

                if (entry_header.seq > after_seq) {
                    entries.emplace_back(entry_header, std::move(data));

                    if (max_count > 0 && entries.size() >= max_count) {
                        return entries.size();
                    }
                }
            }
        }

        return entries.size();
    }

private:
    std::string dir_;
    std::string prefix_;
};

}  // namespace zrt
