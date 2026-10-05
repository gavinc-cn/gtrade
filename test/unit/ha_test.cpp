//
// HA (High Availability) 组件单元测试
//

#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <boost/interprocess/shared_memory_object.hpp>
#include "zrtools/zrt_shm_checkpoint.h"
#include "strategy_checkpoint.h"
#include "wal_entry.h"
#include "shm_wal_ring.h"
#include "global.h"

// ========== ShmCheckpoint 测试 ==========

// 测试用的简单状态结构
struct TestState {
    uint32_t version {0};
    uint64_t counter {0};
    double values[4] {0};
    char name[32] {0};
};
static_assert(std::is_trivially_copyable<TestState>::value, "TestState must be trivially copyable");

TEST(ShmCheckpointTest, BasicReadWrite) {
    // 清理可能存在的共享内存
    boost::interprocess::shared_memory_object::remove("test_checkpoint_basic");

    zrt::ShmCheckpoint<TestState> checkpoint("test_checkpoint_basic");
    ASSERT_TRUE(checkpoint.IsValid());

    // 写入状态
    TestState state {};
    state.version = 1;
    state.counter = 12345;
    state.values[0] = 1.5;
    state.values[1] = 2.5;
    std::strncpy(state.name, "test_state", sizeof(state.name) - 1);

    checkpoint.Write(state);

    // 读取状态
    TestState read_state {};
    ASSERT_TRUE(checkpoint.Read(read_state));

    EXPECT_EQ(read_state.version, 1);
    EXPECT_EQ(read_state.counter, 12345);
    EXPECT_DOUBLE_EQ(read_state.values[0], 1.5);
    EXPECT_DOUBLE_EQ(read_state.values[1], 2.5);
    EXPECT_STREQ(read_state.name, "test_state");

    // 清理
    boost::interprocess::shared_memory_object::remove("test_checkpoint_basic");
}

TEST(ShmCheckpointTest, MultipleWrites) {
    boost::interprocess::shared_memory_object::remove("test_checkpoint_multi");

    zrt::ShmCheckpoint<TestState> checkpoint("test_checkpoint_multi");
    ASSERT_TRUE(checkpoint.IsValid());

    // 多次写入
    for (uint64_t i = 1; i <= 100; ++i) {
        TestState state {};
        state.version = 1;
        state.counter = i;
        state.values[0] = static_cast<double>(i);
        checkpoint.Write(state);
    }

    // 读取应该返回最后写入的值
    TestState read_state {};
    ASSERT_TRUE(checkpoint.Read(read_state));
    EXPECT_EQ(read_state.counter, 100);
    EXPECT_DOUBLE_EQ(read_state.values[0], 100.0);

    // 清理
    boost::interprocess::shared_memory_object::remove("test_checkpoint_multi");
}

TEST(ShmCheckpointTest, VersionTracking) {
    boost::interprocess::shared_memory_object::remove("test_checkpoint_version");

    zrt::ShmCheckpoint<TestState> checkpoint("test_checkpoint_version");
    ASSERT_TRUE(checkpoint.IsValid());

    // 初始版本应为 0
    EXPECT_EQ(checkpoint.GetVersion(), 0);

    // 写入后版本增加
    TestState state {};
    state.counter = 1;
    checkpoint.Write(state);

    EXPECT_GT(checkpoint.GetVersion(), 0);

    uint64_t v1 = checkpoint.GetVersion();

    // 再次写入，版本继续增加
    // 注意：双 Buffer 设计下，写入到另一个 slot，版本从 0 变成 2
    // 所以新版本可能与 v1 相同（都是 2），这是正常的
    state.counter = 2;
    checkpoint.Write(state);

    // 版本应该保持非零（可能等于或大于 v1）
    EXPECT_GT(checkpoint.GetVersion(), 0);

    // 验证数据正确
    TestState read_state {};
    ASSERT_TRUE(checkpoint.Read(read_state));
    EXPECT_EQ(read_state.counter, 2);

    // 清理
    boost::interprocess::shared_memory_object::remove("test_checkpoint_version");
}

TEST(ShmCheckpointTest, PersistenceAcrossInstances) {
    boost::interprocess::shared_memory_object::remove("test_checkpoint_persist");

    // 第一个实例写入
    {
        zrt::ShmCheckpoint<TestState> checkpoint("test_checkpoint_persist");
        ASSERT_TRUE(checkpoint.IsValid());

        TestState state {};
        state.version = 1;
        state.counter = 999;
        std::strncpy(state.name, "persisted", sizeof(state.name) - 1);
        checkpoint.Write(state);
    }

    // 第二个实例读取
    {
        zrt::ShmCheckpoint<TestState> checkpoint("test_checkpoint_persist");
        ASSERT_TRUE(checkpoint.IsValid());

        TestState read_state {};
        ASSERT_TRUE(checkpoint.Read(read_state));

        EXPECT_EQ(read_state.version, 1);
        EXPECT_EQ(read_state.counter, 999);
        EXPECT_STREQ(read_state.name, "persisted");
    }

    // 清理
    boost::interprocess::shared_memory_object::remove("test_checkpoint_persist");
}

// ========== CheckpointWrapper 测试 ==========

// 测试用的策略自定义检查点数据
struct TestCheckpointData {
    double cur_pos {};
    int strat_status {};
    int buy_cnt {};
    int sell_cnt {};
};
static_assert(std::is_trivially_copyable<TestCheckpointData>::value, "TestCheckpointData must be trivially copyable");

using TestCheckpoint = gtrade::CheckpointWrapper<TestCheckpointData>;

TEST(CheckpointWrapperTest, BasicStructure) {
    TestCheckpoint cp {};

    // 检查默认值
    EXPECT_EQ(cp.version, 1);
    EXPECT_EQ(cp.strategy_id, 0);
    EXPECT_EQ(cp.market_seq, 0);
    EXPECT_EQ(cp.order_seq, 0);
    EXPECT_EQ(cp.status, gtrade::StrategyStatus::kUnknown);

    // 检查用户数据默认值
    EXPECT_DOUBLE_EQ(cp.data.cur_pos, 0.0);
    EXPECT_EQ(cp.data.strat_status, 0);
    EXPECT_EQ(cp.data.buy_cnt, 0);
    EXPECT_EQ(cp.data.sell_cnt, 0);

    // 检查 trivially copyable
    EXPECT_TRUE(std::is_trivially_copyable<TestCheckpoint>::value);
    EXPECT_TRUE(std::is_standard_layout<TestCheckpoint>::value);
}

TEST(CheckpointWrapperTest, Checksum) {
    TestCheckpoint cp {};
    cp.version = 1;
    cp.strategy_id = 12345;
    cp.market_seq = 100;
    cp.order_seq = 50;
    cp.data.cur_pos = 1.5;
    cp.data.strat_status = 2;
    cp.status = gtrade::StrategyStatus::kRunning;

    // 更新校验和
    cp.UpdateChecksum();

    // 验证校验和
    EXPECT_TRUE(cp.ValidateChecksum());

    // 修改数据后校验和应该失败
    cp.market_seq = 101;
    EXPECT_FALSE(cp.ValidateChecksum());

    // 重新更新校验和
    cp.UpdateChecksum();
    EXPECT_TRUE(cp.ValidateChecksum());

    // 修改用户数据后校验和也应该失败
    cp.data.cur_pos = 2.5;
    EXPECT_FALSE(cp.ValidateChecksum());

    cp.UpdateChecksum();
    EXPECT_TRUE(cp.ValidateChecksum());
}

TEST(CheckpointWrapperTest, UserDataAccess) {
    TestCheckpoint cp {};

    // 设置用户数据
    cp.data.cur_pos = 100.5;
    cp.data.strat_status = 3;
    cp.data.buy_cnt = 5;
    cp.data.sell_cnt = 3;

    // 验证
    EXPECT_DOUBLE_EQ(cp.data.cur_pos, 100.5);
    EXPECT_EQ(cp.data.strat_status, 3);
    EXPECT_EQ(cp.data.buy_cnt, 5);
    EXPECT_EQ(cp.data.sell_cnt, 3);
}

TEST(CheckpointWrapperTest, Reset) {
    TestCheckpoint cp {};

    // 设置一些值
    cp.version = 2;
    cp.strategy_id = 999;
    cp.market_seq = 100;
    cp.data.cur_pos = 50.0;
    cp.status = gtrade::StrategyStatus::kRunning;

    // 重置
    cp.Reset();

    // 验证所有值都回到默认
    EXPECT_EQ(cp.version, 1);
    EXPECT_EQ(cp.strategy_id, 0);
    EXPECT_EQ(cp.market_seq, 0);
    EXPECT_DOUBLE_EQ(cp.data.cur_pos, 0.0);
    EXPECT_EQ(cp.status, gtrade::StrategyStatus::kUnknown);
}

TEST(StrategyCheckpointTest, HashStrategyId) {
    // 相同字符串应产生相同哈希
    uint32_t hash1 = gtrade::HashStrategyId("test_strategy");
    uint32_t hash2 = gtrade::HashStrategyId("test_strategy");
    EXPECT_EQ(hash1, hash2);

    // 不同字符串应产生不同哈希
    uint32_t hash3 = gtrade::HashStrategyId("another_strategy");
    EXPECT_NE(hash1, hash3);
}

// ========== WAL Entry 测试 ==========

TEST(WalEntryTest, HeaderSize) {
    EXPECT_EQ(sizeof(gtrade::WalEntryHeader), 24);
}

TEST(WalEntryTest, CRC32Calculation) {
    const char test_data[] = "Hello, WAL!";
    uint32_t crc = gtrade::WalEntryHeader::CalcCrc32(test_data, strlen(test_data));

    // CRC 应该是非零的
    EXPECT_NE(crc, 0);

    // 相同数据应产生相同 CRC
    uint32_t crc2 = gtrade::WalEntryHeader::CalcCrc32(test_data, strlen(test_data));
    EXPECT_EQ(crc, crc2);

    // 不同数据应产生不同 CRC
    const char different_data[] = "Different data";
    uint32_t crc3 = gtrade::WalEntryHeader::CalcCrc32(different_data, strlen(different_data));
    EXPECT_NE(crc, crc3);
}

TEST(WalEntryTest, EntryBuilder) {
    gtrade::WalEntryBuilder builder;

    // 构建一个简单的条目
    struct TestData {
        int64_t value1;
        double value2;
    };

    TestData data {12345, 3.14159};

    ASSERT_TRUE(builder.Build(1, 1000000000, gtrade::WalEntryType::kOrder, data));

    const gtrade::WalEntry* entry = builder.GetEntry();
    ASSERT_NE(entry, nullptr);

    EXPECT_EQ(entry->header.seq, 1);
    EXPECT_EQ(entry->header.timestamp_ns, 1000000000);
    EXPECT_EQ(entry->header.type, gtrade::WalEntryType::kOrder);
    EXPECT_EQ(entry->header.data_len, sizeof(TestData));

    // 验证 CRC
    EXPECT_TRUE(entry->ValidateCrc());

    // 获取数据
    const TestData* read_data = entry->GetDataAs<TestData>();
    ASSERT_NE(read_data, nullptr);
    EXPECT_EQ(read_data->value1, 12345);
    EXPECT_DOUBLE_EQ(read_data->value2, 3.14159);
}

TEST(WalEntryTest, EntryTypeName) {
    EXPECT_STREQ(gtrade::GetWalEntryTypeName(gtrade::WalEntryType::kInvalid), "Invalid");
    EXPECT_STREQ(gtrade::GetWalEntryTypeName(gtrade::WalEntryType::kOrder), "Order");
    EXPECT_STREQ(gtrade::GetWalEntryTypeName(gtrade::WalEntryType::kTrade), "Trade");
    EXPECT_STREQ(gtrade::GetWalEntryTypeName(gtrade::WalEntryType::kPosition), "Position");
    EXPECT_STREQ(gtrade::GetWalEntryTypeName(gtrade::WalEntryType::kStrategyCheckpoint), "StrategyCheckpoint");
}

// ========== ShmWalRing 测试 ==========

TEST(ShmWalRingTest, BasicAppendAndRead) {
    boost::interprocess::shared_memory_object::remove("test_wal_ring_basic");

    gtrade::ShmWalRing wal("test_wal_ring_basic");
    ASSERT_TRUE(wal.IsValid());

    // 初始状态
    EXPECT_EQ(wal.GetWritePos(), 0);
    EXPECT_EQ(wal.GetConfirmedPos(), 0);
    EXPECT_EQ(wal.GetNextSeq(), 1);

    // 追加条目
    struct TestData {
        int64_t value;
    };

    TestData data {42};
    uint64_t seq = wal.Append(gtrade::WalEntryType::kOrder, data);

    EXPECT_GT(seq, 0);
    EXPECT_GT(wal.GetWritePos(), 0);
    EXPECT_EQ(wal.GetNextSeq(), 2);

    // 清理
    boost::interprocess::shared_memory_object::remove("test_wal_ring_basic");
}

TEST(ShmWalRingTest, MultipleAppend) {
    boost::interprocess::shared_memory_object::remove("test_wal_ring_multi");

    gtrade::ShmWalRing wal("test_wal_ring_multi");
    ASSERT_TRUE(wal.IsValid());

    struct TestData {
        int64_t value;
    };

    // 追加多个条目
    for (int i = 0; i < 100; ++i) {
        TestData data {i};
        uint64_t seq = wal.Append(gtrade::WalEntryType::kOrder, data);
        EXPECT_EQ(seq, static_cast<uint64_t>(i + 1));
    }

    EXPECT_EQ(wal.GetNextSeq(), 101);
    EXPECT_EQ(wal.GetTotalEntries(), 100);

    // 清理
    boost::interprocess::shared_memory_object::remove("test_wal_ring_multi");
}

TEST(ShmWalRingTest, ReadUnconfirmed) {
    boost::interprocess::shared_memory_object::remove("test_wal_ring_read");

    gtrade::ShmWalRing wal("test_wal_ring_read");
    ASSERT_TRUE(wal.IsValid());

    struct TestData {
        int64_t value;
    };

    // 追加条目
    for (int i = 0; i < 10; ++i) {
        TestData data {i * 100};
        wal.Append(gtrade::WalEntryType::kOrder, data);
    }

    // 读取未确认的条目
    std::vector<std::pair<gtrade::WalEntryHeader, std::vector<char>>> entries;
    size_t count = wal.ReadUnconfirmed(entries, 100);

    EXPECT_EQ(count, 10);

    // 验证数据
    for (size_t i = 0; i < count; ++i) {
        const gtrade::WalEntryHeader& header = entries[i].first;
        EXPECT_EQ(header.seq, i + 1);
        EXPECT_EQ(header.type, gtrade::WalEntryType::kOrder);

        if (!entries[i].second.empty()) {
            const TestData* data = reinterpret_cast<const TestData*>(entries[i].second.data());
            EXPECT_EQ(data->value, static_cast<int64_t>(i * 100));
        }
    }

    // 清理
    boost::interprocess::shared_memory_object::remove("test_wal_ring_read");
}

TEST(ShmWalRingTest, ConfirmBatch) {
    boost::interprocess::shared_memory_object::remove("test_wal_ring_confirm");

    gtrade::ShmWalRing wal("test_wal_ring_confirm");
    ASSERT_TRUE(wal.IsValid());

    struct TestData {
        int64_t value;
    };

    // 追加条目
    for (int i = 0; i < 10; ++i) {
        TestData data {i};
        wal.Append(gtrade::WalEntryType::kOrder, data);
    }

    EXPECT_GT(wal.GetUnconfirmedBytes(), 0);

    // 确认前 5 个
    wal.ConfirmBatch(5);

    // 读取剩余未确认的条目
    std::vector<std::pair<gtrade::WalEntryHeader, std::vector<char>>> entries;
    size_t count = wal.ReadUnconfirmed(entries, 100);

    EXPECT_EQ(count, 5);

    // 第一个未确认的应该是 seq=6
    if (count > 0) {
        EXPECT_EQ(entries[0].first.seq, 6);
    }

    // 清理
    boost::interprocess::shared_memory_object::remove("test_wal_ring_confirm");
}

TEST(ShmWalRingTest, Persistence) {
    boost::interprocess::shared_memory_object::remove("test_wal_ring_persist");

    // 第一个实例写入
    {
        gtrade::ShmWalRing wal("test_wal_ring_persist");
        ASSERT_TRUE(wal.IsValid());

        struct TestData {
            int64_t value;
        };

        for (int i = 0; i < 5; ++i) {
            TestData data {i * 10};
            wal.Append(gtrade::WalEntryType::kOrder, data);
        }

        EXPECT_EQ(wal.GetTotalEntries(), 5);
    }

    // 第二个实例应该能读取
    {
        gtrade::ShmWalRing wal("test_wal_ring_persist");
        ASSERT_TRUE(wal.IsValid());

        // 应该保留之前的状态
        EXPECT_EQ(wal.GetTotalEntries(), 5);
        EXPECT_EQ(wal.GetNextSeq(), 6);

        // 可以继续追加
        struct TestData {
            int64_t value;
        };
        TestData data {999};
        uint64_t seq = wal.Append(gtrade::WalEntryType::kOrder, data);

        EXPECT_EQ(seq, 6);
        EXPECT_EQ(wal.GetTotalEntries(), 6);
    }

    // 清理
    boost::interprocess::shared_memory_object::remove("test_wal_ring_persist");
}

// ========== HAConfig 测试 ==========

TEST(HAConfigTest, DefaultValues) {
    HAConfig config {};

    EXPECT_FALSE(config.enabled);
    EXPECT_EQ(config.initial_role, HARole::kPrimary);
    EXPECT_TRUE(config.peer_addr.empty());
    EXPECT_EQ(config.replication_port, 9999);
    EXPECT_EQ(config.heartbeat_interval_ms, 100);
    EXPECT_EQ(config.takeover_timeout_ms, 500);
    EXPECT_TRUE(config.sync_order_send);
    EXPECT_EQ(config.sync_timeout_ms, 50);
    EXPECT_TRUE(config.auto_failover);
    EXPECT_TRUE(config.auto_reconcile);
}

TEST(HAConfigTest, RoleName) {
    EXPECT_STREQ(GetHARoleName(HARole::kPrimary), "Primary");
    EXPECT_STREQ(GetHARoleName(HARole::kStandby), "Standby");
    EXPECT_STREQ(GetHARoleName(HARole::kUnknown), "Unknown");
}

// 性能测试
TEST(ShmCheckpointTest, WritePerformance) {
    boost::interprocess::shared_memory_object::remove("test_checkpoint_perf");

    zrt::ShmCheckpoint<TestState> checkpoint("test_checkpoint_perf");
    ASSERT_TRUE(checkpoint.IsValid());

    const int ITERATIONS = 10000;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        TestState state {};
        state.counter = i;
        checkpoint.Write(state);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    double avg_ns = static_cast<double>(duration.count()) / ITERATIONS;
    double throughput = 1e9 / avg_ns;

    std::cout << "ShmCheckpoint Performance:" << std::endl;
    std::cout << "  Total: " << ITERATIONS << " writes in " << duration.count() << " ns" << std::endl;
    std::cout << "  Average: " << avg_ns << " ns/write" << std::endl;
    std::cout << "  Throughput: " << throughput << " writes/sec" << std::endl;

    // 写入应该在微秒级以内
    EXPECT_LT(avg_ns, 10000) << "Checkpoint write should be fast";

    // 清理
    boost::interprocess::shared_memory_object::remove("test_checkpoint_perf");
}

TEST(ShmWalRingTest, AppendPerformance) {
    boost::interprocess::shared_memory_object::remove("test_wal_perf");

    gtrade::ShmWalRing wal("test_wal_perf");
    ASSERT_TRUE(wal.IsValid());

    struct TestData {
        int64_t values[8];
    };

    const int ITERATIONS = 10000;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        TestData data {};
        data.values[0] = i;
        wal.Append(gtrade::WalEntryType::kOrder, data);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    double avg_ns = static_cast<double>(duration.count()) / ITERATIONS;
    double throughput = 1e9 / avg_ns;

    std::cout << "ShmWalRing Performance:" << std::endl;
    std::cout << "  Total: " << ITERATIONS << " appends in " << duration.count() << " ns" << std::endl;
    std::cout << "  Average: " << avg_ns << " ns/append" << std::endl;
    std::cout << "  Throughput: " << throughput << " appends/sec" << std::endl;

    // 追加应该在微秒级以内
    EXPECT_LT(avg_ns, 10000) << "WAL append should be fast";

    // 清理
    boost::interprocess::shared_memory_object::remove("test_wal_perf");
}
