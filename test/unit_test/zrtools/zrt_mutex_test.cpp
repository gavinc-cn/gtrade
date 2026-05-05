//
// zrt_mutex_test.cpp - null_mutex 单元测试
//

#include "pch.h"
#include "gtest/gtest.h"
#include "zrtools/mutex.h"
#include <mutex>
#include <thread>
#include <vector>

// 测试 null_mutex 基本功能
TEST(NullMutexTest, BasicOperations) {
    zrt::null_mutex mtx {};

    // lock 和 unlock 应该不做任何事情
    EXPECT_NO_THROW(mtx.lock());
    EXPECT_NO_THROW(mtx.unlock());

    // try_lock 应该总是返回 true
    EXPECT_TRUE(mtx.try_lock());
    mtx.unlock();
}

// 测试 null_mutex 可以重复 lock
TEST(NullMutexTest, ReentrantLock) {
    zrt::null_mutex mtx {};

    // null_mutex 应该支持重入（因为它什么都不做）
    mtx.lock();
    mtx.lock();
    mtx.lock();

    mtx.unlock();
    mtx.unlock();
    mtx.unlock();
}

// 测试 null_mutex 与 std::lock_guard 配合使用
TEST(NullMutexTest, WithLockGuard) {
    zrt::null_mutex mtx {};

    {
        std::lock_guard<zrt::null_mutex> guard(mtx);
        // 在锁保护范围内执行操作
        int x = 42;
        EXPECT_EQ(x, 42);
    }
    // lock_guard 析构，自动释放锁
}

// 测试 null_mutex 与 std::unique_lock 配合使用
TEST(NullMutexTest, WithUniqueLock) {
    zrt::null_mutex mtx {};

    {
        std::unique_lock<zrt::null_mutex> lock(mtx);
        EXPECT_TRUE(lock.owns_lock());

        lock.unlock();
        EXPECT_FALSE(lock.owns_lock());

        lock.lock();
        EXPECT_TRUE(lock.owns_lock());
    }
}

// 测试 null_mutex 在多线程环境中的行为
TEST(NullMutexTest, MultiThreaded) {
    zrt::null_mutex mtx {};
    int counter = 0;
    constexpr int num_threads = 10;
    constexpr int ops_per_thread = 1000;

    std::vector<std::thread> threads {};
    threads.reserve(num_threads);

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&mtx, &counter]() {
            for (int j = 0; j < ops_per_thread; ++j) {
                std::lock_guard<zrt::null_mutex> guard(mtx);
                ++counter;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 注意：由于 null_mutex 不提供真正的同步，
    // counter 可能不等于 num_threads * ops_per_thread
    // 这里只验证没有崩溃
    EXPECT_GT(counter, 0);
}

// 测试 null_mutex 的 try_lock 总是成功
TEST(NullMutexTest, TryLockAlwaysSucceeds) {
    zrt::null_mutex mtx {};

    // 即使已经 lock，try_lock 也应该成功
    mtx.lock();
    EXPECT_TRUE(mtx.try_lock());
    EXPECT_TRUE(mtx.try_lock());
    EXPECT_TRUE(mtx.try_lock());

    mtx.unlock();
    mtx.unlock();
    mtx.unlock();
    mtx.unlock();
}

// 测试 null_mutex 作为模板参数
template<typename Mutex>
class Counter {
public:
    void Increment() {
        std::lock_guard<Mutex> guard(mtx_);
        ++value_;
    }

    int GetValue() const {
        return value_;
    }

private:
    Mutex mtx_ {};
    int value_ = 0;
};

TEST(NullMutexTest, AsTemplateParameter) {
    Counter<zrt::null_mutex> counter {};

    for (int i = 0; i < 100; ++i) {
        counter.Increment();
    }

    EXPECT_EQ(counter.GetValue(), 100);
}
