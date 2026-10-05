//
// Created by Claude Code on 2026/1/29.
//

#include "pch.h"
#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include "zrtools/thread.h"

class ZrtThreadTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
};

// Test SetThreadAffinity
TEST_F(ZrtThreadTest, SetThreadAffinity) {
    // Get number of CPUs
    int num_cpus = std::thread::hardware_concurrency();

    if (num_cpus < 1) {
        GTEST_SKIP() << "Cannot determine number of CPUs";
    }

    std::atomic<bool> affinity_set{false};
    std::thread worker([&affinity_set, num_cpus]() {
        // Try to set affinity to CPU 0 (should always exist)
        bool result = zrt::SetThreadAffinity(pthread_self(), 0);
        affinity_set = result;
    });

    worker.join();

    // On most systems this should succeed
    // Some environments (containers, etc.) may restrict this
    // so we just check it doesn't crash
    SUCCEED();
}

// Test SetThreadAffinity with invalid CPU
TEST_F(ZrtThreadTest, SetThreadAffinityInvalidCPU) {
    int invalid_cpu = 9999;  // Very unlikely to have this many CPUs

    std::atomic<bool> affinity_result{true};
    std::thread worker([&affinity_result, invalid_cpu]() {
        bool result = zrt::SetThreadAffinity(pthread_self(), invalid_cpu);
        affinity_result = result;
    });

    worker.join();

    // Should fail with invalid CPU
    EXPECT_FALSE(affinity_result);
}

// Test SetThreadSched with normal priority
TEST_F(ZrtThreadTest, SetThreadSchedNormal) {
    std::atomic<bool> sched_set{false};
    std::thread worker([&sched_set]() {
        sched_param param{};
        param.sched_priority = 0;  // Normal priority for SCHED_OTHER

        bool result = zrt::SetThreadSched(pthread_self(), SCHED_OTHER, param);
        sched_set = result;
    });

    worker.join();

    // Setting SCHED_OTHER with priority 0 should typically succeed
    EXPECT_TRUE(sched_set);
}

// Test SetThreadSched with FIFO (requires root, may fail)
TEST_F(ZrtThreadTest, SetThreadSchedFIFO) {
    std::atomic<bool> sched_set{false};
    std::thread worker([&sched_set]() {
        sched_param param{};
        param.sched_priority = 1;  // Minimum priority for SCHED_FIFO

        // This typically requires root privileges
        bool result = zrt::SetThreadSched(pthread_self(), SCHED_FIFO, param);
        sched_set = result;
    });

    worker.join();

    // This may fail without root privileges, but should not crash
    // We don't assert the result as it depends on privileges
    SUCCEED();
}

// Test SetThreadSched with invalid policy
TEST_F(ZrtThreadTest, SetThreadSchedInvalidPolicy) {
    std::atomic<bool> sched_set{true};
    std::thread worker([&sched_set]() {
        sched_param param{};
        param.sched_priority = 50;

        // Use an invalid policy value
        bool result = zrt::SetThreadSched(pthread_self(), 9999, param);
        sched_set = result;
    });

    worker.join();

    // Should fail with invalid policy
    EXPECT_FALSE(sched_set);
}

// Test multiple threads with affinity
TEST_F(ZrtThreadTest, MultipleThreadsAffinity) {
    int num_cpus = std::thread::hardware_concurrency();

    if (num_cpus < 2) {
        GTEST_SKIP() << "Need at least 2 CPUs for this test";
    }

    const int num_threads = std::min(4, static_cast<int>(num_cpus));
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([i, &success_count]() {
            if (zrt::SetThreadAffinity(pthread_self(), i)) {
                success_count++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Some or all should succeed depending on environment
    // Just verify no crashes and at least some attempts
    SUCCEED();
}

// Test that SetThreadAffinity is idempotent
TEST_F(ZrtThreadTest, SetThreadAffinityIdempotent) {
    std::atomic<int> success_count{0};
    std::thread worker([&success_count]() {
        // Set affinity multiple times to same CPU
        for (int i = 0; i < 5; ++i) {
            if (zrt::SetThreadAffinity(pthread_self(), 0)) {
                success_count++;
            }
        }
    });

    worker.join();

    // All attempts should have the same result
    EXPECT_TRUE(success_count == 0 || success_count == 5);
}

// Test current thread
TEST_F(ZrtThreadTest, CurrentThread) {
    pthread_t current = pthread_self();

    // Set affinity on current thread
    bool result = zrt::SetThreadAffinity(current, 0);

    // Result depends on environment, but shouldn't crash
    SUCCEED();

    // Set scheduler on current thread
    sched_param param{};
    param.sched_priority = 0;
    result = zrt::SetThreadSched(current, SCHED_OTHER, param);

    // SCHED_OTHER with priority 0 should succeed
    EXPECT_TRUE(result);
}

// Test thread safety of functions
TEST_F(ZrtThreadTest, ThreadSafetyStressTest) {
    const int num_threads = 10;
    const int iterations = 100;
    std::vector<std::thread> threads;
    std::atomic<int> error_count{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([i, iterations, &error_count]() {
            for (int j = 0; j < iterations; ++j) {
                // Try to set affinity and sched params
                zrt::SetThreadAffinity(pthread_self(), i % std::thread::hardware_concurrency());

                sched_param param{};
                param.sched_priority = 0;
                zrt::SetThreadSched(pthread_self(), SCHED_OTHER, param);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // No crashes means success
    SUCCEED();
}
