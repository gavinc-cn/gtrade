//
// SPSC (Single Producer Single Consumer) Lock-Free Ring Buffer
// 单生产者单消费者无锁环形缓冲区
//
// 特性:
// - Lock-free: 读写线程无需加锁
// - 编译期策略: 通过模板参数指定策略，零运行时开销
// - 持久化支持: 可直接映射到共享内存
// - 序号机制: 支持write_seq和confirmed_seq追踪
//

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <chrono>
#include <thread>

namespace zrt {

// 队列满时的策略
enum class FullPolicy {
    ReturnFalse, // 返回失败（推荐：交易系统中避免阻塞）
    Overwrite,   // 覆盖旧数据（危险！仅用于确定可以丢失旧数据的场景）
    Wait,        // 自旋等待直到有空间（带超时）
};

// 队列空时的策略
enum class EmptyPolicy {
    ReturnFalse,  // 返回失败（推荐）
    Wait,         // 自旋等待直到有数据（带超时）
};

/**
 * SPSC环形缓冲区
 *
 * @tparam T 数据类型（必须是trivially copyable以支持共享内存）
 * @tparam Capacity 缓冲区容量
 * @tparam FPolicy 队列满时策略（编译期决定）
 * @tparam EPolicy 队列空时策略（编译期决定）
 *
 * 设计要点:
 * 1. 使用三个原子序号: write_seq (生产者), read_seq, confirmed_seq (消费者)
 * 2. 无锁设计: 生产者只写write_seq，消费者只写read_seq和confirmed_seq
 * 3. 缓存行对齐: 避免false sharing
 * 4. 编译期策略: 避免运行时分支判断
 */
template<typename T,
         size_t Capacity,
         FullPolicy FPolicy = FullPolicy::ReturnFalse,
         EmptyPolicy EPolicy = EmptyPolicy::ReturnFalse,
         int SPIN_COUNT = 100
    >
class SPSCRingBuffer {
public:
    // 魔数，用于验证缓冲区有效性
    static constexpr uint32_t MAGIC_NUMBER = 0x53505343; // "SPSC"
    static constexpr uint32_t VERSION = 1;

    // 构造函数
    // wait_timeout_us: 等待超时时间（微秒），0表示无限等待（仅当策略为Wait时有效）
    SPSCRingBuffer(int64_t wait_timeout_us = 0)
        : magic(MAGIC_NUMBER)
        , version(VERSION)
        , wait_timeout_us_(wait_timeout_us)
        , write_seq_(0)
        , read_seq_(0)
        , confirmed_seq_(0)
    {
        // 编译期检查：必须是trivially copyable以支持共享内存
        static_assert(std::is_trivially_copyable<T>::value,
                     "T must be trivially copyable for shared memory support");

        // 编译期检查：不能有虚函数（虚函数表指针在不同进程中会不同）
        static_assert(!std::is_polymorphic<T>::value,
                     "T must not be polymorphic (no virtual functions)");

        // 容量必须大于0
        static_assert(Capacity > 0, "Capacity must be greater than 0");

        std::memset(buffer_, 0, sizeof(buffer_));
    }

    // 验证缓冲区有效性
    bool IsValid() const {
        return magic == MAGIC_NUMBER && version == VERSION;
    }

    // 重置缓冲区
    void Reset() {
        magic = MAGIC_NUMBER;
        version = VERSION;
        write_seq_.store(0, std::memory_order_release);
        read_seq_.store(0, std::memory_order_release);
        confirmed_seq_.store(0, std::memory_order_release);
        std::memset(buffer_, 0, sizeof(buffer_));
    }

    /**
     * 写入数据（生产者调用）
     *
     * @param item 要写入的数据
     * @return true if 写入成功, false if 队列满且策略为ReturnFalse
     */
    bool Write(const T& item) {
        const uint64_t current_write = write_seq_.load(std::memory_order_relaxed);
        const uint64_t current_confirmed = confirmed_seq_.load(std::memory_order_acquire);

        // 检查是否队列满
        if (current_write - current_confirmed >= Capacity) {
            return HandleFullQueue(item, current_write, current_confirmed);
        }

        // 写入数据
        size_t index = current_write % Capacity;
        buffer_[index] = item;

        // 更新写序号（release语义确保数据写入对读线程可见）
        write_seq_.store(current_write + 1, std::memory_order_release);
        return true;
    }

    /**
     * 读取数据（消费者调用）
     *
     * @param item 输出参数，存储读取的数据
     * @return true if 读取成功, false if 队列空且策略为ReturnFalse或超时
     */
    bool Read(T& item) {
        const uint64_t current_read = read_seq_.load(std::memory_order_relaxed);
        uint64_t current_write = write_seq_.load(std::memory_order_acquire);

        // 检查是否队列空
        if (current_read >= current_write) {
            if constexpr (EPolicy == EmptyPolicy::ReturnFalse) {
                return false;  // 直接返回失败
            } else if constexpr (EPolicy == EmptyPolicy::Wait) {
                // 等待模式：自旋等待直到有数据或超时
                const auto start = std::chrono::steady_clock::now();

                while (true) {
                    // 重新检查是否有数据
                    current_write = write_seq_.load(std::memory_order_acquire);
                    if (current_read < current_write) {
                        break;  // 有数据了，跳出循环继续读取
                    }

                    // 检查超时（如果设置了超时）
                    if (wait_timeout_us_ > 0) {
                        auto elapsed = std::chrono::steady_clock::now() - start;
                        if (std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() >= wait_timeout_us_) {
                            return false;  // 超时
                        }
                    }

                    // 短暂自旋后yield
                    static thread_local int spin_counter = 0;
                    if (++spin_counter >= SPIN_COUNT) {
                        std::this_thread::yield();
                        spin_counter = 0;
                    }
                }
            }
        }

        // 读取数据
        size_t index = current_read % Capacity;
        item = buffer_[index];

        // 更新读序号
        read_seq_.store(current_read + 1, std::memory_order_release);
        return true;
    }

    /**
     * 批量读取未确认的数据（用于恢复）
     *
     * @param output 输出数组
     * @param max_count 最大读取数量
     * @return 实际读取的数量
     */
    size_t ReadUnconfirmed(T* output, const size_t max_count) const {
        const uint64_t current_confirmed = confirmed_seq_.load(std::memory_order_acquire);
        const uint64_t current_write = write_seq_.load(std::memory_order_acquire);

        const size_t unconfirmed = current_write - current_confirmed;

        // 检查数据是否被覆盖
        if (unconfirmed > Capacity) {
            return 0; // 数据已被覆盖，无法恢复
        }

        const size_t count = std::min(unconfirmed, max_count);

        for (size_t i = 0; i < count; i++) {
            size_t index = (current_confirmed + i) % Capacity;
            output[i] = buffer_[index];
        }

        return count;
    }

    /**
     * 确认数据已处理（消费者调用）
     *
     * @param seq 确认到的序号
     */
    void ConfirmRead(const uint64_t seq) {
        const uint64_t current_confirmed = confirmed_seq_.load(std::memory_order_relaxed);
        if (seq > current_confirmed) {
            confirmed_seq_.store(seq, std::memory_order_release);
        }
    }

    /**
     * 批量确认
     *
     * @param count 确认的数量
     */
    void ConfirmBatch(const size_t count) {
        const uint64_t current_confirmed = confirmed_seq_.load(std::memory_order_relaxed);
        const uint64_t current_write = write_seq_.load(std::memory_order_acquire);

        uint64_t new_confirmed = current_confirmed + count;
        if (new_confirmed > current_write) {
            new_confirmed = current_write;
        }

        if (new_confirmed > current_confirmed) {
            confirmed_seq_.store(new_confirmed, std::memory_order_release);
        }
    }

    // 获取统计信息
    uint64_t GetWriteSeq() const {
        return write_seq_.load(std::memory_order_acquire);
    }

    uint64_t GetReadSeq() const {
        return read_seq_.load(std::memory_order_acquire);
    }

    uint64_t GetConfirmedSeq() const {
        return confirmed_seq_.load(std::memory_order_acquire);
    }

    size_t GetUnconfirmedCount() const {
        const uint64_t write = write_seq_.load(std::memory_order_acquire);
        const uint64_t confirmed = confirmed_seq_.load(std::memory_order_acquire);
        return write - confirmed;
    }

    bool IsFull() const {
        const uint64_t write = write_seq_.load(std::memory_order_relaxed);
        const uint64_t confirmed = confirmed_seq_.load(std::memory_order_acquire);
        return (write - confirmed) >= Capacity;
    }

    bool IsEmpty() const {
        const uint64_t write = write_seq_.load(std::memory_order_acquire);
        const uint64_t read = read_seq_.load(std::memory_order_relaxed);
        return read >= write;
    }

    static size_t GetCapacity() { return Capacity; }

public:
    // 元数据（用于共享内存验证）
    uint32_t magic;
    uint32_t version;

private:
    // 队列满时的处理（编译期特化）
    bool HandleFullQueue(const T& item, uint64_t current_write, uint64_t current_confirmed) {
        if constexpr (FPolicy == FullPolicy::ReturnFalse) {
            return false;  // 直接返回失败
        } else if constexpr (FPolicy == FullPolicy::Overwrite) {
            // 覆盖模式：直接写入（会覆盖未确认的数据）
            size_t index = current_write % Capacity;
            buffer_[index] = item;
            write_seq_.store(current_write + 1, std::memory_order_release);
            return true;
        } else if constexpr (FPolicy == FullPolicy::Wait) {
            // 等待模式：自旋等待直到有空间或超时
            const auto start = std::chrono::steady_clock::now();

            while (true) {
                // 重新检查是否有空间
                const uint64_t current_confirmed_new = confirmed_seq_.load(std::memory_order_acquire);
                if (current_write - current_confirmed_new < Capacity) {
                    // 有空间了，写入数据
                    size_t index = current_write % Capacity;
                    buffer_[index] = item;
                    write_seq_.store(current_write + 1, std::memory_order_release);
                    return true;
                }

                // 检查超时（如果设置了超时）
                if (wait_timeout_us_ > 0) {
                    const auto elapsed = std::chrono::steady_clock::now() - start;
                    if (std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() >= wait_timeout_us_) {
                        return false;  // 超时
                    }
                }

                // 短暂自旋后yield
                static thread_local int spin_counter = 0;
                if (++spin_counter >= SPIN_COUNT) {
                    std::this_thread::yield();
                    spin_counter = 0;
                }
            }
        }
        return false;
    }

    // 等待超时时间（微秒），仅当策略为Wait时有效
    int64_t wait_timeout_us_ {};

    // 缓存行对齐，避免false sharing
    alignas(64) std::atomic<uint64_t> write_seq_ {};      // 生产者写入序号
    alignas(64) std::atomic<uint64_t> read_seq_ {};       // 消费者读取序号
    alignas(64) std::atomic<uint64_t> confirmed_seq_ {};  // 消费者确认序号

    // 数据缓冲区
    T buffer_[Capacity] {};
};

} // namespace gtrade
