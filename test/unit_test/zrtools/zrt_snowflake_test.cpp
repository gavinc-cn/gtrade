//
// Created by Claude Code on 2026/1/29.
//

#include "pch.h"
#include <gtest/gtest.h>
#include <thread>
#include <set>
#include <vector>
#include <atomic>
#include "zrtools/zrt_snowflake.h"

// Test fixture for Snowflake tests
class SnowflakeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Suppress log output during tests
    }
};

// Basic ID generation test
TEST_F(SnowflakeTest, BasicIdGeneration) {
    zrt::SnowflakeStandardV2 sf(1);

    uint64_t id = sf.nextId();
    EXPECT_GT(id, 0);

    // Generate multiple IDs and verify they are unique
    std::set<uint64_t> ids;
    for (int i = 0; i < 1000; ++i) {
        uint64_t new_id = sf.nextId();
        EXPECT_TRUE(ids.insert(new_id).second) << "Duplicate ID generated: " << new_id;
    }
}

// Test ID parsing
TEST_F(SnowflakeTest, IdParsing) {
    zrt::SnowflakeStandardV2 sf(42);

    uint64_t id = sf.nextId();
    auto parsed = sf.parseId(id);

    EXPECT_EQ(parsed.server_id, 42);
    EXPECT_LT(parsed.sequence, zrt::SnowflakeStandardV2::MAX_SEQUENCE + 1);

    // Timestamp should be reasonable (within recent time)
    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    EXPECT_LE(parsed.timestamp_ms, now_ms + 1000);
    EXPECT_GE(parsed.timestamp_ms, now_ms - 1000);
}

// Test monotonically increasing IDs within same millisecond
TEST_F(SnowflakeTest, MonotonicallyIncreasing) {
    zrt::SnowflakeStandardV2 sf(1);

    uint64_t prev_id = sf.nextId();
    for (int i = 0; i < 1000; ++i) {
        uint64_t curr_id = sf.nextId();
        EXPECT_GT(curr_id, prev_id) << "IDs are not monotonically increasing";
        prev_id = curr_id;
    }
}

// Test different server IDs
TEST_F(SnowflakeTest, DifferentServerIds) {
    zrt::SnowflakeStandardV2 sf1(1);
    zrt::SnowflakeStandardV2 sf2(2);

    uint64_t id1 = sf1.nextId();
    uint64_t id2 = sf2.nextId();

    auto parsed1 = sf1.parseId(id1);
    auto parsed2 = sf2.parseId(id2);

    EXPECT_EQ(parsed1.server_id, 1);
    EXPECT_EQ(parsed2.server_id, 2);
    EXPECT_NE(id1, id2);
}

// Test max server ID validation
TEST_F(SnowflakeTest, MaxServerIdValidation) {
    // Valid server ID
    EXPECT_NO_THROW(zrt::SnowflakeStandardV2(255));

    // Invalid server ID (exceeds 8 bits = 255)
    EXPECT_THROW(zrt::SnowflakeStandardV2(256), std::invalid_argument);
}

// Test sequence overflow within same millisecond
TEST_F(SnowflakeTest, SequenceOverflow) {
    zrt::SnowflakeStandardV2 sf(1);

    // Generate enough IDs to potentially overflow sequence
    const int num_ids = 20000;
    std::set<uint64_t> ids;

    for (int i = 0; i < num_ids; ++i) {
        uint64_t id = sf.nextId();
        EXPECT_TRUE(ids.insert(id).second) << "Duplicate ID at iteration " << i;
    }

    EXPECT_EQ(ids.size(), num_ids);
}

// Test different configurations
TEST_F(SnowflakeTest, DifferentConfigurations) {
    // Standard configuration
    {
        zrt::SnowflakeStandardV2 sf(1);
        EXPECT_EQ(sf.getServerId(), 1);
        EXPECT_GT(sf.getMaxYears(), 60);
    }

    // Strict configuration
    {
        zrt::SnowflakeStrictV2 sf(1);
        EXPECT_EQ(sf.getServerId(), 1);
    }

    // Tolerant configuration
    {
        zrt::SnowflakeTolerantV2 sf(1);
        EXPECT_EQ(sf.getServerId(), 1);
    }

    // Large scale configuration (10 bits for server ID)
    {
        zrt::SnowflakeLargeV2 sf(1023);  // Max server ID for 10 bits
        EXPECT_EQ(sf.getServerId(), 1023);
    }

    // High throughput configuration
    {
        zrt::SnowflakeHighThroughputV2 sf(1);
        EXPECT_EQ(sf.getServerId(), 1);
    }
}

// Test custom epoch
TEST_F(SnowflakeTest, CustomEpoch) {
    uint64_t custom_epoch = 1700000000000ULL;  // Custom epoch
    zrt::SnowflakeStandardV2 sf(1, custom_epoch);

    EXPECT_EQ(sf.getEpoch(), custom_epoch);

    uint64_t id = sf.nextId();
    auto parsed = sf.parseId(id);

    EXPECT_GE(parsed.timestamp_ms, custom_epoch);
}

// Test concurrent ID generation (thread safety)
TEST_F(SnowflakeTest, ConcurrentIdGeneration) {
    zrt::SnowflakeStandardV2 sf(1);

    const int num_threads = 4;
    const int ids_per_thread = 1000;
    std::vector<std::thread> threads;
    std::vector<std::set<uint64_t>> thread_ids(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&sf, &thread_ids, t, ids_per_thread]() {
            for (int i = 0; i < ids_per_thread; ++i) {
                uint64_t id = sf.nextId();
                thread_ids[t].insert(id);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Verify all IDs are unique across threads
    std::set<uint64_t> all_ids;
    for (const auto& ids : thread_ids) {
        for (uint64_t id : ids) {
            EXPECT_TRUE(all_ids.insert(id).second) << "Duplicate ID found: " << id;
        }
    }

    EXPECT_EQ(all_ids.size(), num_threads * ids_per_thread);
}

// Test statistics functions
TEST_F(SnowflakeTest, Statistics) {
    zrt::SnowflakeStandardV2 sf(1);

    sf.resetStats();

    // Generate some IDs
    for (int i = 0; i < 100; ++i) {
        sf.nextId();
    }

    // Statistics should not crash
    EXPECT_NO_THROW(sf.printStats());
    EXPECT_NO_THROW(sf.printConfig());
}

// Test getMaxYears calculation
TEST_F(SnowflakeTest, MaxYearsCalculation) {
    zrt::SnowflakeStandardV2 sf(1);

    // Standard config has 41 bits for timestamp
    // 2^41 ms = about 69 years
    double max_years = sf.getMaxYears();
    EXPECT_GE(max_years, 69);
    EXPECT_LE(max_years, 70);
}

// Test bit layout consistency
TEST_F(SnowflakeTest, BitLayoutConsistency) {
    // Verify total bits = 63 (plus 1 sign bit = 64)
    EXPECT_EQ(zrt::SnowflakeStandardV2::SERVER_ID_BITS +
              zrt::SnowflakeStandardV2::SEQUENCE_BITS +
              zrt::SnowflakeStandardV2::TIMESTAMP_BITS, 63);

    EXPECT_EQ(zrt::SnowflakeLargeV2::SERVER_ID_BITS +
              zrt::SnowflakeLargeV2::SEQUENCE_BITS +
              zrt::SnowflakeLargeV2::TIMESTAMP_BITS, 63);
}
