//
// Created by Claude Code
// 通用共享内存 WAL 环形缓冲区
//
// 设计目标:
// - 支持变长条目
// - 单生产者单消费者无锁访问
// - 共享内存持久化
// - 支持崩溃恢复和主备同步
//

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <sys/mman.h>
#include "spdlog/spdlog.h"
#include "zrtools/zrt_wal_entry.h"
#include "zrtools/zrt_time.h"

namespace zrt {

/**
 * 通用共享内存 WAL 环形缓冲区
 *
 * 特性:
 * - 变长条目存储（每个条目包含 WalEntryHeader + 变长数据）
 * - 单生产者单消费者无锁设计
 * - 三个序号追踪: write_pos, read_pos, confirmed_pos
 * - 支持批量读取未确认条目（用于恢复和同步）
 *
 * 内存布局:
 * +-------------------+
 * | ShmRingHeader     | HEADER_SIZE bytes (固定)
 * +-------------------+
 * | buffer_[]         | BufferSize bytes
 * +-------------------+
 *
 * @tparam TypeEnum 条目类型枚举类型
 * @tparam BufferSize 缓冲区大小（默认 16MB）
 * @tparam MaxEntrySize 最大条目大小（默认 4KB）
 */
template<typename TypeEnum, size_t BufferSize = 16 * 1024 * 1024, size_t MaxEntrySize = 4096>
class ShmRing {
public:
    static constexpr size_t BUFFER_SIZE = BufferSize;
    static constexpr size_t MAX_ENTRY_SIZE = MaxEntrySize;
    static constexpr uint32_t MAGIC_NUMBER = 0x5A525752;  // "ZRWR" (ZRT WAL Ring)
    static constexpr uint32_t VERSION = 1;

    /**
     * 共享内存头部
     */
    struct ShmRingHeader {
        uint32_t magic {MAGIC_NUMBER};
        uint32_t version {VERSION};
        uint64_t buffer_size {BUFFER_SIZE};

        // 序号追踪（64 字节对齐避免 false sharing）
        alignas(64) std::atomic<uint64_t> write_pos {0};     // 写入位置
        alignas(64) std::atomic<uint64_t> read_pos {0};      // 读取位置
        alignas(64) std::atomic<uint64_t> confirmed_pos {0}; // 已确认位置

        // 条目序号
        alignas(64) std::atomic<uint64_t> next_seq {1};      // 下一个序号

        // 统计信息
        uint64_t total_entries {0};
        uint64_t total_bytes {0};
        uint64_t wrap_count {0};  // 回绕次数

        char reserved[24] {};
    };

    // 头部实际大小约 320 字节，使用 512 字节空间
    static constexpr size_t HEADER_SIZE = 512;

    explicit ShmRing(const std::string& shm_name)
        : shm_name_(shm_name)
        , header_(nullptr)
        , buffer_(nullptr) {
        Initialize();
    }

    ~ShmRing() {
        if (header_) {
            munlock(region_.get_address(), region_.get_size());
        }
    }

    // 禁止拷贝
    ShmRing(const ShmRing&) = delete;
    ShmRing& operator=(const ShmRing&) = delete;

    /**
     * 追加 WAL 条目
     *
     * @param type 条目类型
     * @param data 数据指针
     * @param len 数据长度
     * @return 条目序号，0 表示失败
     */
    uint64_t Append(const TypeEnum type, const void* data, const uint16_t len) {
        if (!IsValid()) {
            SPDLOG_ERROR("ShmRing::Append failed: not initialized");
            return 0;
        }

        // 计算条目总大小（8 字节对齐）
        const size_t entry_size = AlignUp(sizeof(WalEntryHeader<TypeEnum>) + len, 8);
        if (entry_size > MAX_ENTRY_SIZE) {
            SPDLOG_ERROR("ShmRing::Append failed: entry too large ({} > {})",
                         entry_size, MAX_ENTRY_SIZE);
            return 0;
        }

        // 获取写入位置
        const uint64_t write_pos = header_->write_pos.load(std::memory_order_relaxed);
        const uint64_t confirmed_pos = header_->confirmed_pos.load(std::memory_order_acquire);

        // 检查空间是否足够
        const uint64_t used = write_pos - confirmed_pos;
        if (used + entry_size > BUFFER_SIZE) {
            SPDLOG_WARN("ShmRing::Append failed: buffer full (used={}, need={})",
                        used, entry_size);
            return 0;
        }

        // 分配序号
        const uint64_t seq = header_->next_seq.fetch_add(1, std::memory_order_relaxed);

        // 获取当前时间戳
        const uint64_t timestamp_ns = zrt::DateTimeUTC().Epoch19();

        // 计算缓冲区中的写入位置
        const size_t buf_offset = write_pos % BUFFER_SIZE;

        // 检查是否需要跨越缓冲区边界
        if (buf_offset + entry_size > BUFFER_SIZE) {
            // 需要回绕，在当前位置写入一个跳过标记
            const size_t remaining = BUFFER_SIZE - buf_offset;
            if (remaining >= sizeof(WalEntryHeader<TypeEnum>)) {
                auto* skip_header = reinterpret_cast<WalEntryHeader<TypeEnum>*>(
                    buffer_ + buf_offset);
                skip_header->seq = 0;  // 特殊序号表示跳过
                skip_header->timestamp_ns = 0;
                skip_header->type = static_cast<TypeEnum>(0);  // kInvalid
                skip_header->data_len = static_cast<uint16_t>(remaining - sizeof(WalEntryHeader<TypeEnum>));
                skip_header->crc32 = 0;
            }

            // 更新写入位置到缓冲区开头
            const uint64_t new_write_pos = write_pos + remaining;
            header_->write_pos.store(new_write_pos, std::memory_order_release);
            header_->wrap_count++;

            // 递归调用，在新位置写入
            return Append(type, data, len);
        }

        // 写入条目
        auto* entry = reinterpret_cast<WalEntry<TypeEnum>*>(buffer_ + buf_offset);
        entry->header.seq = seq;
        entry->header.timestamp_ns = timestamp_ns;
        entry->header.type = type;
        entry->header.data_len = len;

        if (len > 0 && data != nullptr) {
            std::memcpy(entry->data, data, len);
        }

        entry->UpdateCrc();

        // 更新写入位置
        header_->write_pos.store(write_pos + entry_size, std::memory_order_release);
        header_->total_entries++;
        header_->total_bytes += entry_size;

        return seq;
    }

    /**
     * 追加类型化 WAL 条目
     */
    template<typename T>
    uint64_t Append(const TypeEnum type, const T& data) {
        static_assert(std::is_trivially_copyable<T>::value,
                      "T must be trivially copyable");
        return Append(type, &data, sizeof(T));
    }

    /**
     * 读取下一个条目
     *
     * @param entry 输出参数，存储条目头部
     * @param data 输出缓冲区
     * @param max_len 缓冲区最大长度
     * @return true 如果读取成功
     */
    bool ReadNext(WalEntryHeader<TypeEnum>& entry, void* data, const size_t max_len) {
        if (!IsValid()) {
            return false;
        }

        const uint64_t read_pos = header_->read_pos.load(std::memory_order_relaxed);
        const uint64_t write_pos = header_->write_pos.load(std::memory_order_acquire);

        if (read_pos >= write_pos) {
            return false;  // 没有新数据
        }

        // 计算缓冲区中的读取位置
        const size_t buf_offset = read_pos % BUFFER_SIZE;
        const auto* wal_entry = reinterpret_cast<const WalEntry<TypeEnum>*>(buffer_ + buf_offset);

        // 跳过无效条目（回绕填充）
        if (wal_entry->header.seq == 0 ||
            static_cast<uint16_t>(wal_entry->header.type) == 0) {
            const size_t skip_size = AlignUp(
                sizeof(WalEntryHeader<TypeEnum>) + wal_entry->header.data_len, 8);
            header_->read_pos.store(read_pos + skip_size, std::memory_order_release);
            return ReadNext(entry, data, max_len);  // 递归读取下一个
        }

        // 验证 CRC
        if (!wal_entry->ValidateCrc()) {
            SPDLOG_ERROR("ShmRing::ReadNext: CRC mismatch at pos {}", read_pos);
            const size_t skip_size = AlignUp(
                sizeof(WalEntryHeader<TypeEnum>) + wal_entry->header.data_len, 8);
            header_->read_pos.store(read_pos + skip_size, std::memory_order_release);
            return ReadNext(entry, data, max_len);
        }

        // 复制头部
        entry = wal_entry->header;

        // 复制数据
        if (entry.data_len > 0 && data != nullptr) {
            const size_t copy_len = std::min(static_cast<size_t>(entry.data_len), max_len);
            std::memcpy(data, wal_entry->data, copy_len);
        }

        // 更新读取位置
        const size_t entry_size = AlignUp(sizeof(WalEntryHeader<TypeEnum>) + entry.data_len, 8);
        header_->read_pos.store(read_pos + entry_size, std::memory_order_release);

        return true;
    }

    /**
     * 批量读取未确认的条目
     *
     * @param entries 输出向量
     * @param max_count 最大读取数量
     * @return 实际读取的数量
     */
    size_t ReadUnconfirmed(std::vector<std::pair<WalEntryHeader<TypeEnum>, std::vector<char>>>& entries,
                           const size_t max_count) const {
        if (!IsValid()) {
            return 0;
        }

        entries.clear();

        uint64_t pos = header_->confirmed_pos.load(std::memory_order_acquire);
        const uint64_t write_pos = header_->write_pos.load(std::memory_order_acquire);

        while (pos < write_pos && entries.size() < max_count) {
            const size_t buf_offset = pos % BUFFER_SIZE;
            const auto* wal_entry = reinterpret_cast<const WalEntry<TypeEnum>*>(buffer_ + buf_offset);

            const size_t entry_size = AlignUp(
                sizeof(WalEntryHeader<TypeEnum>) + wal_entry->header.data_len, 8);

            // 跳过无效条目
            if (wal_entry->header.seq == 0 ||
                static_cast<uint16_t>(wal_entry->header.type) == 0) {
                pos += entry_size;
                continue;
            }

            // 验证 CRC
            if (!wal_entry->ValidateCrc()) {
                SPDLOG_WARN("ShmRing::ReadUnconfirmed: CRC mismatch at pos {}", pos);
                pos += entry_size;
                continue;
            }

            // 复制条目
            std::vector<char> data(wal_entry->header.data_len);
            if (wal_entry->header.data_len > 0) {
                std::memcpy(data.data(), wal_entry->data, wal_entry->header.data_len);
            }

            entries.emplace_back(wal_entry->header, std::move(data));
            pos += entry_size;
        }

        return entries.size();
    }

    /**
     * 确认已处理的条目
     *
     * @param seq 确认到的序号
     */
    void Confirm(const uint64_t seq) const {
        if (!IsValid()) {
            return;
        }

        uint64_t pos = header_->confirmed_pos.load(std::memory_order_relaxed);
        const uint64_t write_pos = header_->write_pos.load(std::memory_order_acquire);

        while (pos < write_pos) {
            const size_t buf_offset = pos % BUFFER_SIZE;
            const auto* wal_entry = reinterpret_cast<const WalEntry<TypeEnum>*>(buffer_ + buf_offset);

            const size_t entry_size = AlignUp(
                sizeof(WalEntryHeader<TypeEnum>) + wal_entry->header.data_len, 8);

            if (wal_entry->header.seq == seq) {
                header_->confirmed_pos.store(pos + entry_size, std::memory_order_release);
                return;
            }

            if (wal_entry->header.seq > seq) {
                return;
            }

            pos += entry_size;
        }
    }

    /**
     * 批量确认
     *
     * @param count 确认的条目数量
     */
    void ConfirmBatch(const size_t count) const {
        if (!IsValid() || count == 0) {
            return;
        }

        uint64_t pos = header_->confirmed_pos.load(std::memory_order_relaxed);
        const uint64_t write_pos = header_->write_pos.load(std::memory_order_acquire);
        size_t confirmed = 0;

        while (pos < write_pos && confirmed < count) {
            const size_t buf_offset = pos % BUFFER_SIZE;
            const auto* wal_entry = reinterpret_cast<const WalEntry<TypeEnum>*>(buffer_ + buf_offset);

            const size_t entry_size = AlignUp(
                sizeof(WalEntryHeader<TypeEnum>) + wal_entry->header.data_len, 8);

            // 跳过无效条目
            if (wal_entry->header.seq != 0 &&
                static_cast<uint16_t>(wal_entry->header.type) != 0) {
                confirmed++;
            }

            pos += entry_size;
        }

        header_->confirmed_pos.store(pos, std::memory_order_release);
    }

    // 统计信息获取
    uint64_t GetWritePos() const {
        return header_ ? header_->write_pos.load(std::memory_order_acquire) : 0;
    }

    uint64_t GetReadPos() const {
        return header_ ? header_->read_pos.load(std::memory_order_acquire) : 0;
    }

    uint64_t GetConfirmedPos() const {
        return header_ ? header_->confirmed_pos.load(std::memory_order_acquire) : 0;
    }

    uint64_t GetNextSeq() const {
        return header_ ? header_->next_seq.load(std::memory_order_acquire) : 0;
    }

    size_t GetUnconfirmedBytes() const {
        if (!header_) return 0;
        const uint64_t write = header_->write_pos.load(std::memory_order_acquire);
        const uint64_t confirmed = header_->confirmed_pos.load(std::memory_order_acquire);
        return write - confirmed;
    }

    uint64_t GetTotalEntries() const {
        return header_ ? header_->total_entries : 0;
    }

    uint64_t GetTotalBytes() const {
        return header_ ? header_->total_bytes : 0;
    }

    bool IsValid() const {
        return header_ != nullptr && buffer_ != nullptr;
    }

    /**
     * 重置缓冲区
     */
    void Reset() {
        if (header_) {
            header_->magic = MAGIC_NUMBER;
            header_->version = VERSION;
            header_->buffer_size = BUFFER_SIZE;
            header_->write_pos.store(0, std::memory_order_release);
            header_->read_pos.store(0, std::memory_order_release);
            header_->confirmed_pos.store(0, std::memory_order_release);
            header_->next_seq.store(1, std::memory_order_release);
            header_->total_entries = 0;
            header_->total_bytes = 0;
            header_->wrap_count = 0;
            std::memset(buffer_, 0, BUFFER_SIZE);
        }
    }

    const std::string& GetShmName() const { return shm_name_; }

private:
    void Initialize() {
        try {
            namespace bip = boost::interprocess;

            constexpr size_t total_size = HEADER_SIZE + BUFFER_SIZE;
            bool is_new = false;

            try {
                shm_obj_ = bip::shared_memory_object(
                    bip::open_only, shm_name_.c_str(), bip::read_write);
            } catch (const bip::interprocess_exception&) {
                bip::shared_memory_object::remove(shm_name_.c_str());
                shm_obj_ = bip::shared_memory_object(
                    bip::create_only, shm_name_.c_str(), bip::read_write);
                shm_obj_.truncate(total_size);
                is_new = true;
            }

            region_ = bip::mapped_region(shm_obj_, bip::read_write);

            if (mlock(region_.get_address(), region_.get_size()) != 0) {
                SPDLOG_DEBUG("mlock failed for {}: {}", shm_name_, strerror(errno));
            }

            char* base = static_cast<char*>(region_.get_address());
            header_ = reinterpret_cast<ShmRingHeader*>(base);
            buffer_ = base + HEADER_SIZE;

            if (is_new || header_->magic != MAGIC_NUMBER) {
                Reset();
                SPDLOG_INFO("Created new ShmRing: {}", shm_name_);
            } else {
                SPDLOG_INFO("Opened existing ShmRing: {}, write_pos={}, confirmed_pos={}, "
                            "unconfirmed={}",
                            shm_name_,
                            header_->write_pos.load(std::memory_order_relaxed),
                            header_->confirmed_pos.load(std::memory_order_relaxed),
                            GetUnconfirmedBytes());
            }

        } catch (const boost::interprocess::interprocess_exception& e) {
            SPDLOG_ERROR("Failed to initialize ShmRing for {}: {}",
                         shm_name_, e.what());
            header_ = nullptr;
            buffer_ = nullptr;
        }
    }

    std::string shm_name_;
    boost::interprocess::shared_memory_object shm_obj_;
    boost::interprocess::mapped_region region_;
    ShmRingHeader* header_;
    char* buffer_;
};

}  // namespace zrt
