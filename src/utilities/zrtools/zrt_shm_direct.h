//
// Created by dell on 2025/5/22.
// Direct memory mapping - 单线程读写场景的极简实现
// 零开销：无锁、无原子操作、无版本号、无memcpy
//

#pragma once

#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <sys/mman.h>
#include <cstring>
#include "spdlog/spdlog.h"

namespace zrt {

// 极简头部 - 只记录数据大小（用于校验）
struct DirectShmHeader {
    uint32_t magic;       // 魔数，用于检测初始化
    uint32_t data_size;   // 数据大小

    static constexpr uint32_t MAGIC_VALUE = 0x5A525431;  // "ZRT1"

    DirectShmHeader() : magic(MAGIC_VALUE), data_size(0) {}
};

// 单线程直接访问共享内存 - 极致性能
// 适用场景：同一个线程既写又读（如策略线程）
template<typename T>
class DirectShm {
public:
    explicit DirectShm(const std::string& shm_name)
        : shm_name_(shm_name), data_(nullptr) {
        static_assert(std::is_trivially_copyable<T>::value,
                      "T must be trivially copyable for shared memory");
        Initialize();
    }

    ~DirectShm() {
        if (data_) {
            munlock(reinterpret_cast<void*>(header_), sizeof(DirectShmHeader) + sizeof(T));
        }
    }

    // 禁止拷贝
    DirectShm(const DirectShm&) = delete;
    DirectShm& operator=(const DirectShm&) = delete;

    // 允许移动
    DirectShm(DirectShm&& other) noexcept
        : shm_name_(std::move(other.shm_name_))
        , shm_obj_(std::move(other.shm_obj_))
        , region_(std::move(other.region_))
        , header_(other.header_)
        , data_(other.data_) {
        other.header_ = nullptr;
        other.data_ = nullptr;
    }

    DirectShm& operator=(DirectShm&& other) noexcept {
        if (this != &other) {
            shm_name_ = std::move(other.shm_name_);
            shm_obj_ = std::move(other.shm_obj_);
            region_ = std::move(other.region_);
            header_ = other.header_;
            data_ = other.data_;
            other.header_ = nullptr;
            other.data_ = nullptr;
        }
        return *this;
    }

    // 直接获取数据指针 - 零开销访问
    T* operator->() { return data_; }
    const T* operator->() const { return data_; }

    T& operator*() { return *data_; }
    const T& operator*() const { return *data_; }

    T* Get() { return data_; }
    const T* Get() const { return data_; }

    // 便捷方法：读取副本（如果需要）
    T Read() const { return *data_; }

    // 便捷方法：写入副本（如果需要）
    void Write(const T& src) { *data_ = src; }

    // 重置数据为默认构造状态
    void Reset() {
        if (data_) {
            *data_ = T();
        }
    }

    // 检查是否初始化成功
    bool IsValid() const { return data_ != nullptr; }

private:
    void Initialize() {
        try {
            namespace bip = boost::interprocess;

            bool is_new = false;
            try {
                // 尝试打开已有的共享内存
                shm_obj_ = bip::shared_memory_object(bip::open_only, shm_name_.c_str(), bip::read_write);
            } catch (const bip::interprocess_exception&) {
                // 不存在则创建
                bip::shared_memory_object::remove(shm_name_.c_str());
                shm_obj_ = bip::shared_memory_object(bip::create_only, shm_name_.c_str(), bip::read_write);
                shm_obj_.truncate(sizeof(DirectShmHeader) + sizeof(T));
                is_new = true;
            }

            // 映射到内存（只映射一次，永久复用）
            region_ = bip::mapped_region(shm_obj_, bip::read_write);

            // 锁定内存，防止被swap到磁盘
            if (mlock(region_.get_address(), region_.get_size()) != 0) {
                SPDLOG_DEBUG("mlock failed: {}, continue without memory lock", strerror(errno));
            }

            // 初始化指针
            header_ = static_cast<DirectShmHeader*>(region_.get_address());
            data_ = reinterpret_cast<T*>(reinterpret_cast<char*>(header_) + sizeof(DirectShmHeader));

            // 如果是新创建的，初始化头部
            if (is_new || header_->magic != DirectShmHeader::MAGIC_VALUE) {
                new (header_) DirectShmHeader();
                header_->data_size = sizeof(T);
                new (data_) T();  // 默认构造数据
                SPDLOG_DEBUG("Created new shared memory: {}", shm_name_);
            } else {
                SPDLOG_DEBUG("Opened existing shared memory: {}", shm_name_);
            }

        } catch (const boost::interprocess::interprocess_exception& e) {
            SPDLOG_ERROR("Failed to initialize DirectShm for {}: {}", shm_name_, e.what());
            header_ = nullptr;
            data_ = nullptr;
        }
    }

    std::string shm_name_;
    boost::interprocess::shared_memory_object shm_obj_;
    boost::interprocess::mapped_region region_;
    DirectShmHeader* header_;
    T* data_;
};

// 便捷函数 - 获取单例共享内存对象
template<typename T>
DirectShm<T>& GetDirectShm(const std::string& shm_name) {
    static thread_local std::unordered_map<std::string, DirectShm<T>> shm_cache;

    auto it = shm_cache.find(shm_name);
    if (it == shm_cache.end()) {
        it = shm_cache.emplace(std::piecewise_construct,
                               std::forward_as_tuple(shm_name),
                               std::forward_as_tuple(shm_name)).first;
    }

    return it->second;
}

} // namespace zrt
