//
// Created by Claude Code
// 双 Buffer SeqLock 实现 - 用于策略状态检查点
//
// 设计目标：
// - 写端崩溃时读端仍能读到最后完整的数据
// - 单写者多读者无锁访问
// - 适用于策略检查点的持久化
//
// 原理：
// - 使用两个 slot，交替写入
// - 每个 slot 有独立的 seq，奇数表示写入中
// - active_ 指示当前应该读取哪个 slot
// - 写入时先写非活跃 slot，完成后切换 active_
//

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <sys/mman.h>
#include "spdlog/spdlog.h"

namespace zrt {

/**
 * 双 Buffer SeqLock 共享内存检查点
 *
 * 特性:
 * - 双 Buffer 设计：写端崩溃不会损坏读端数据
 * - SeqLock 机制：奇数 seq 表示写入中
 * - 无锁设计：单写者，多读者
 * - 共享内存持久化：进程崩溃后可恢复
 *
 * @tparam State 状态类型（必须是 trivially copyable）
 */
template<typename State>
class ShmCheckpoint {
public:
    // 魔数，用于验证共享内存有效性
    static constexpr uint32_t MAGIC_NUMBER = 0x43484B50;  // "CHKP"
    static constexpr uint32_t VERSION = 1;

    // 每个 slot 的结构（64 字节对齐避免 false sharing）
    struct alignas(64) ShmSlot {
        std::atomic<uint64_t> seq {0};  // 奇数 = 写入中
        State data {};

        // 检查数据是否完整（seq 为偶数）
        bool IsComplete() const {
            return (seq.load(std::memory_order_acquire) & 1) == 0;
        }
    };

    // 共享内存头部
    struct ShmHeader {
        uint32_t magic {MAGIC_NUMBER};
        uint32_t version {VERSION};
        std::atomic<int> active {0};  // 当前活跃的 slot (0 或 1)
        char reserved[52] {};         // 填充到 64 字节
    };

    static_assert(sizeof(ShmHeader) == 64, "ShmHeader must be 64 bytes");

    explicit ShmCheckpoint(const std::string& shm_name)
        : shm_name_(shm_name)
        , header_(nullptr)
        , slots_(nullptr) {
        static_assert(std::is_trivially_copyable<State>::value,
                      "State must be trivially copyable for shared memory");
        static_assert(!std::is_polymorphic<State>::value,
                      "State must not be polymorphic (no virtual functions)");
        Initialize();
    }

    ~ShmCheckpoint() {
        if (header_) {
            munlock(region_.get_address(), region_.get_size());
        }
    }

    // 禁止拷贝
    ShmCheckpoint(const ShmCheckpoint&) = delete;
    ShmCheckpoint& operator=(const ShmCheckpoint&) = delete;

    // 允许移动
    ShmCheckpoint(ShmCheckpoint&& other) noexcept
        : shm_name_(std::move(other.shm_name_))
        , shm_obj_(std::move(other.shm_obj_))
        , region_(std::move(other.region_))
        , header_(other.header_)
        , slots_(other.slots_) {
        other.header_ = nullptr;
        other.slots_ = nullptr;
    }

    ShmCheckpoint& operator=(ShmCheckpoint&& other) noexcept {
        if (this != &other) {
            shm_name_ = std::move(other.shm_name_);
            shm_obj_ = std::move(other.shm_obj_);
            region_ = std::move(other.region_);
            header_ = other.header_;
            slots_ = other.slots_;
            other.header_ = nullptr;
            other.slots_ = nullptr;
        }
        return *this;
    }

    /**
     * 写入状态（生产者调用）
     *
     * 写入协议:
     * 1. 获取非活跃 slot
     * 2. 将 seq 设为奇数（开始写入）
     * 3. 写入数据
     * 4. 将 seq 设为偶数（写入完成）
     * 5. 切换 active_ 到新 slot
     *
     * @param state 要写入的状态
     */
    void Write(const State& state) {
        if (!IsValid()) {
            SPDLOG_ERROR("ShmCheckpoint::Write failed: not initialized");
            return;
        }

        // 获取非活跃 slot
        const int current_active = header_->active.load(std::memory_order_acquire);
        const int write_slot = 1 - current_active;
        ShmSlot& slot = slots_[write_slot];

        // 开始写入（seq 设为奇数）
        const uint64_t old_seq = slot.seq.load(std::memory_order_relaxed);
        slot.seq.store(old_seq + 1, std::memory_order_release);

        // 写入数据
        std::memcpy(&slot.data, &state, sizeof(State));

        // 完成写入（seq 设为偶数）
        slot.seq.store(old_seq + 2, std::memory_order_release);

        // 切换活跃 slot
        header_->active.store(write_slot, std::memory_order_release);
    }

    /**
     * 读取状态（消费者调用）
     *
     * 读取协议:
     * 1. 获取活跃 slot
     * 2. 读取 seq1
     * 3. 如果 seq1 为奇数，尝试另一个 slot
     * 4. 读取数据
     * 5. 读取 seq2
     * 6. 如果 seq1 == seq2，数据有效
     *
     * @param out 输出参数，存储读取的状态
     * @return true 如果读取成功，false 如果没有有效数据
     */
    bool Read(State& out) const {
        if (!IsValid()) {
            return false;
        }

        // 优先读取活跃 slot
        const int active_slot = header_->active.load(std::memory_order_acquire);

        // 尝试读取活跃 slot
        if (TryReadSlot(slots_[active_slot], out)) {
            return true;
        }

        // 如果活跃 slot 正在写入，尝试另一个 slot
        const int other_slot = 1 - active_slot;
        return TryReadSlot(slots_[other_slot], out);
    }

    /**
     * 获取当前版本号（用于监控）
     *
     * @return 当前活跃 slot 的 seq
     */
    uint64_t GetVersion() const {
        if (!IsValid()) {
            return 0;
        }
        const int active_slot = header_->active.load(std::memory_order_acquire);
        return slots_[active_slot].seq.load(std::memory_order_acquire);
    }

    /**
     * 检查是否初始化成功
     */
    bool IsValid() const {
        return header_ != nullptr && slots_ != nullptr;
    }

    /**
     * 重置共享内存
     */
    void Reset() {
        if (IsValid()) {
            header_->magic = MAGIC_NUMBER;
            header_->version = VERSION;
            header_->active.store(0, std::memory_order_release);
            slots_[0].seq.store(0, std::memory_order_release);
            slots_[0].data = State{};
            slots_[1].seq.store(0, std::memory_order_release);
            slots_[1].data = State{};
        }
    }

    /**
     * 获取共享内存名称
     */
    const std::string& GetShmName() const { return shm_name_; }

private:
    void Initialize() {
        try {
            namespace bip = boost::interprocess;

            constexpr size_t total_size = sizeof(ShmHeader) + 2 * sizeof(ShmSlot);
            bool is_new = false;

            try {
                // 尝试打开已有的共享内存
                shm_obj_ = bip::shared_memory_object(
                    bip::open_only, shm_name_.c_str(), bip::read_write);
            } catch (const bip::interprocess_exception&) {
                // 不存在则创建
                bip::shared_memory_object::remove(shm_name_.c_str());
                shm_obj_ = bip::shared_memory_object(
                    bip::create_only, shm_name_.c_str(), bip::read_write);
                shm_obj_.truncate(total_size);
                is_new = true;
            }

            // 映射到内存
            region_ = bip::mapped_region(shm_obj_, bip::read_write);

            // 锁定内存，防止被 swap 到磁盘
            if (mlock(region_.get_address(), region_.get_size()) != 0) {
                SPDLOG_DEBUG("mlock failed for {}: {}, continue without memory lock",
                             shm_name_, strerror(errno));
            }

            // 初始化指针
            char* base = static_cast<char*>(region_.get_address());
            header_ = reinterpret_cast<ShmHeader*>(base);
            slots_ = reinterpret_cast<ShmSlot*>(base + sizeof(ShmHeader));

            // 如果是新创建的或魔数不对，初始化
            if (is_new || header_->magic != MAGIC_NUMBER) {
                Reset();
                SPDLOG_INFO("Created new ShmCheckpoint: {}", shm_name_);
            } else {
                SPDLOG_INFO("Opened existing ShmCheckpoint: {}, active={}, slot0_seq={}, slot1_seq={}",
                            shm_name_,
                            header_->active.load(std::memory_order_relaxed),
                            slots_[0].seq.load(std::memory_order_relaxed),
                            slots_[1].seq.load(std::memory_order_relaxed));
            }

        } catch (const boost::interprocess::interprocess_exception& e) {
            SPDLOG_ERROR("Failed to initialize ShmCheckpoint for {}: {}",
                         shm_name_, e.what());
            header_ = nullptr;
            slots_ = nullptr;
        }
    }

    static bool TryReadSlot(const ShmSlot& slot, State& out) {
        // 最多重试 3 次
        for (int retry = 0; retry < 3; ++retry) {
            // 读取 seq1
            const uint64_t seq1 = slot.seq.load(std::memory_order_acquire);

            // 如果正在写入（奇数），跳过
            if (seq1 & 1) {
                continue;
            }

            // 如果 seq 为 0，说明从未写入
            if (seq1 == 0) {
                return false;
            }

            // 读取数据
            std::memcpy(&out, &slot.data, sizeof(State));

            // 读取 seq2
            const uint64_t seq2 = slot.seq.load(std::memory_order_acquire);

            // 如果 seq 没有变化，数据有效
            if (seq1 == seq2) {
                return true;
            }
        }
        return false;
    }

    std::string shm_name_;
    boost::interprocess::shared_memory_object shm_obj_;
    boost::interprocess::mapped_region region_;
    ShmHeader* header_;
    ShmSlot* slots_;
};

/**
 * 只读版本的 ShmCheckpoint
 *
 * 用于备机接收端，只支持读取操作
 */
template<typename State>
class ShmCheckpointReader {
public:
    using ShmSlot = typename ShmCheckpoint<State>::ShmSlot;
    using ShmHeader = typename ShmCheckpoint<State>::ShmHeader;

    explicit ShmCheckpointReader(const std::string& shm_name)
        : shm_name_(shm_name)
        , header_(nullptr)
        , slots_(nullptr) {
        Initialize();
    }

    ~ShmCheckpointReader() = default;

    // 禁止拷贝
    ShmCheckpointReader(const ShmCheckpointReader&) = delete;
    ShmCheckpointReader& operator=(const ShmCheckpointReader&) = delete;

    /**
     * 读取状态
     */
    bool Read(State& out) const {
        if (!IsValid()) {
            return false;
        }

        const int active_slot = header_->active.load(std::memory_order_acquire);

        if (TryReadSlot(slots_[active_slot], out)) {
            return true;
        }

        const int other_slot = 1 - active_slot;
        return TryReadSlot(slots_[other_slot], out);
    }

    /**
     * 获取当前版本号
     */
    uint64_t GetVersion() const {
        if (!IsValid()) {
            return 0;
        }
        const int active_slot = header_->active.load(std::memory_order_acquire);
        return slots_[active_slot].seq.load(std::memory_order_acquire);
    }

    bool IsValid() const {
        return header_ != nullptr && slots_ != nullptr;
    }

private:
    void Initialize() {
        try {
            namespace bip = boost::interprocess;

            // 只读方式打开
            shm_obj_ = bip::shared_memory_object(
                bip::open_only, shm_name_.c_str(), bip::read_only);

            region_ = bip::mapped_region(shm_obj_, bip::read_only);

            char* base = static_cast<char*>(region_.get_address());
            header_ = reinterpret_cast<const ShmHeader*>(base);
            slots_ = reinterpret_cast<const ShmSlot*>(base + sizeof(ShmHeader));

            if (header_->magic != ShmCheckpoint<State>::MAGIC_NUMBER) {
                SPDLOG_ERROR("Invalid ShmCheckpoint magic for {}", shm_name_);
                header_ = nullptr;
                slots_ = nullptr;
            }

        } catch (const boost::interprocess::interprocess_exception& e) {
            SPDLOG_DEBUG("ShmCheckpointReader: {} not found or not ready: {}",
                         shm_name_, e.what());
            header_ = nullptr;
            slots_ = nullptr;
        }
    }

    bool TryReadSlot(const ShmSlot& slot, State& out) const {
        for (int retry = 0; retry < 3; ++retry) {
            const uint64_t seq1 = slot.seq.load(std::memory_order_acquire);

            if (seq1 & 1) {
                continue;
            }

            if (seq1 == 0) {
                return false;
            }

            std::memcpy(&out, &slot.data, sizeof(State));

            const uint64_t seq2 = slot.seq.load(std::memory_order_acquire);

            if (seq1 == seq2) {
                return true;
            }
        }
        return false;
    }

    std::string shm_name_;
    boost::interprocess::shared_memory_object shm_obj_;
    boost::interprocess::mapped_region region_;
    const ShmHeader* header_;
    const ShmSlot* slots_;
};

}  // namespace zrt
