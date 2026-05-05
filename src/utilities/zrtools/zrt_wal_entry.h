//
// Created by Claude Code
// 通用 WAL (Write-Ahead Log) 条目定义
//
// 用于记录关键操作，支持崩溃恢复和主备同步
// 业务无关的通用实现，通过模板参数指定条目类型枚举
//

#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>
#include "zrt_crc32.h"

namespace zrt {

/**
 * 通用 WAL 条目头部（固定大小：24 字节）
 *
 * @tparam TypeEnum 条目类型枚举类型（必须是 uint16_t 的枚举）
 */
template<typename TypeEnum>
struct WalEntryHeader {
    static_assert(std::is_enum_v<TypeEnum>, "TypeEnum must be an enum type");
    static_assert(sizeof(TypeEnum) == sizeof(uint16_t), "TypeEnum must be uint16_t sized");

    uint64_t seq;           // 序号（单调递增）
    uint64_t timestamp_ns;  // 纳秒时间戳
    TypeEnum type;          // 条目类型
    uint16_t data_len;      // 数据长度（不包含头部）
    uint32_t crc32;         // 数据 CRC32 校验和

    /**
     * 计算 CRC32
     */
    static uint32_t CalcCrc32(const void* data, const size_t len) {
        return Crc32::Calc(data, len);
    }
};

// 确保头部大小固定
template<typename TypeEnum>
constexpr size_t kWalEntryHeaderSize = 24;

/**
 * 通用 WAL 条目（变长）
 *
 * 内存布局:
 * +-------------------+
 * | WalEntryHeader    | 24 bytes
 * +-------------------+
 * | data[]            | data_len bytes
 * +-------------------+
 *
 * @tparam TypeEnum 条目类型枚举类型
 */
template<typename TypeEnum>
struct WalEntry {
    WalEntryHeader<TypeEnum> header;
    char data[0];  // 柔性数组成员

    /**
     * 获取条目总大小
     */
    size_t TotalSize() const {
        return sizeof(WalEntryHeader<TypeEnum>) + header.data_len;
    }

    /**
     * 验证 CRC
     */
    bool ValidateCrc() const {
        if (header.data_len == 0) {
            return header.crc32 == 0;
        }
        return header.crc32 == WalEntryHeader<TypeEnum>::CalcCrc32(data, header.data_len);
    }

    /**
     * 更新 CRC
     */
    void UpdateCrc() {
        if (header.data_len == 0) {
            header.crc32 = 0;
        } else {
            header.crc32 = WalEntryHeader<TypeEnum>::CalcCrc32(data, header.data_len);
        }
    }

    /**
     * 获取数据指针
     */
    const void* GetData() const { return data; }
    void* GetData() { return data; }

    /**
     * 获取数据长度
     */
    uint16_t GetDataLen() const { return header.data_len; }

    /**
     * 模板方法：获取类型化数据
     */
    template<typename T>
    const T* GetDataAs() const {
        static_assert(std::is_trivially_copyable_v<T>,
                      "T must be trivially copyable");
        if (header.data_len < sizeof(T)) {
            return nullptr;
        }
        return reinterpret_cast<const T*>(data);
    }

    template<typename T>
    T* GetDataAs() {
        static_assert(std::is_trivially_copyable_v<T>,
                      "T must be trivially copyable");
        if (header.data_len < sizeof(T)) {
            return nullptr;
        }
        return reinterpret_cast<T*>(data);
    }
};

/**
 * 通用 WAL 条目构建器
 *
 * 用于在固定大小缓冲区中构建 WAL 条目
 *
 * @tparam TypeEnum 条目类型枚举类型
 * @tparam MaxSize 最大条目大小（默认 4KB）
 */
template<typename TypeEnum, size_t MaxSize = 4096>
class WalEntryBuilder {
public:
    static constexpr size_t MAX_ENTRY_SIZE = MaxSize;

    WalEntryBuilder() : size_(0) {
        std::memset(buffer_, 0, sizeof(buffer_));
    }

    /**
     * 构建 WAL 条目
     *
     * @param seq 序号
     * @param timestamp_ns 时间戳
     * @param type 条目类型
     * @param data 数据指针
     * @param data_len 数据长度
     * @return true 如果构建成功
     */
    bool Build(const uint64_t seq, const uint64_t timestamp_ns, const TypeEnum type,
               const void* data, const uint16_t data_len) {
        if (sizeof(WalEntryHeader<TypeEnum>) + data_len > MAX_ENTRY_SIZE) {
            return false;
        }

        auto* entry = reinterpret_cast<WalEntry<TypeEnum>*>(buffer_);
        entry->header.seq = seq;
        entry->header.timestamp_ns = timestamp_ns;
        entry->header.type = type;
        entry->header.data_len = data_len;

        if (data_len > 0 && data != nullptr) {
            std::memcpy(entry->data, data, data_len);
        }

        entry->UpdateCrc();
        size_ = sizeof(WalEntryHeader<TypeEnum>) + data_len;
        return true;
    }

    /**
     * 构建类型化 WAL 条目
     */
    template<typename T>
    bool Build(const uint64_t seq, const uint64_t timestamp_ns, const TypeEnum type,
               const T& data) {
        static_assert(std::is_trivially_copyable<T>::value,
                      "T must be trivially copyable");
        return Build(seq, timestamp_ns, type, &data, sizeof(T));
    }

    /**
     * 获取构建的条目
     */
    const WalEntry<TypeEnum>* GetEntry() const {
        if (size_ == 0) return nullptr;
        return reinterpret_cast<const WalEntry<TypeEnum>*>(buffer_);
    }

    WalEntry<TypeEnum>* GetEntry() {
        if (size_ == 0) return nullptr;
        return reinterpret_cast<WalEntry<TypeEnum>*>(buffer_);
    }

    /**
     * 获取条目大小
     */
    size_t GetSize() const { return size_; }

    /**
     * 获取缓冲区指针
     */
    const char* GetBuffer() const { return buffer_; }
    char* GetBuffer() { return buffer_; }

    /**
     * 重置
     */
    void Reset() {
        size_ = 0;
        std::memset(buffer_, 0, sizeof(buffer_));
    }

private:
    alignas(8) char buffer_[MAX_ENTRY_SIZE];
    size_t size_;
};

/**
 * 对齐到指定字节边界
 */
inline size_t AlignUp(const size_t size, const size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

}  // namespace zrt
