//
// zrt_shm_checkpoint_test.cpp - 共享内存检查点单元测试
//

#include "pch.h"
#include "gtest/gtest.h"
#include "zrtools/zrt_shm_checkpoint.h"
#include <thread>
#include <atomic>
#include <chrono>

// 测试状态结构（使用命名空间避免与 ha_test.cpp 中的 TestState 冲突）
namespace zrt_checkpoint_test {
struct CheckpointTestState {
    int64_t position;
    double price;
    int32_t status;
    char symbol[32];

    bool operator==(const CheckpointTestState& other) const {
        return position == other.position &&
               price == other.price &&
               status == other.status &&
               std::strcmp(symbol, other.symbol) == 0;
    }
};
}  // namespace zrt_checkpoint_test

using TestCheckpointState = zrt_checkpoint_test::CheckpointTestState;

class ZrtShmCheckpointTest : public ::testing::Test {
protected:
    void SetUp() override {
        shm_name_ = "test_checkpoint_" + std::to_string(getpid());
        // 清理可能残留的共享内存
        boost::interprocess::shared_memory_object::remove(shm_name_.c_str());
    }

    void TearDown() override {
        // 清理共享内存
        boost::interprocess::shared_memory_object::remove(shm_name_.c_str());
    }

    std::string shm_name_;
};

// 测试 ShmCheckpoint 创建和初始化
TEST_F(ZrtShmCheckpointTest, Creation) {
    zrt::ShmCheckpoint<TestCheckpointState> checkpoint(shm_name_);

    EXPECT_TRUE(checkpoint.IsValid());
    EXPECT_EQ(checkpoint.GetShmName(), shm_name_);
}

// 测试 ShmCheckpoint 基本写入和读取
TEST_F(ZrtShmCheckpointTest, BasicWriteRead) {
    zrt::ShmCheckpoint<TestCheckpointState> checkpoint(shm_name_);
    ASSERT_TRUE(checkpoint.IsValid());

    TestCheckpointState write_state {};
    write_state.position = 100;
    write_state.price = 50000.0;
    write_state.status = 1;
    std::strncpy(write_state.symbol, "BTC-USDT", sizeof(write_state.symbol));

    // 写入
    checkpoint.Write(write_state);

    // 读取
    TestCheckpointState read_state {};
    EXPECT_TRUE(checkpoint.Read(read_state));

    EXPECT_EQ(read_state, write_state);
}

// 测试 ShmCheckpoint 多次写入
TEST_F(ZrtShmCheckpointTest, MultipleWrites) {
    zrt::ShmCheckpoint<TestCheckpointState> checkpoint(shm_name_);
    ASSERT_TRUE(checkpoint.IsValid());

    for (int i = 0; i < 100; ++i) {
        TestCheckpointState state {};
        state.position = i * 10;
        state.price = 100.0 + i;
        state.status = i % 5;
        std::snprintf(state.symbol, sizeof(state.symbol), "SYM_%d", i);

        checkpoint.Write(state);

        TestCheckpointState read_state {};
        EXPECT_TRUE(checkpoint.Read(read_state));
        EXPECT_EQ(read_state, state);
    }
}

// 测试 ShmCheckpoint GetVersion
TEST_F(ZrtShmCheckpointTest, GetVersion) {
    zrt::ShmCheckpoint<TestCheckpointState> checkpoint(shm_name_);
    ASSERT_TRUE(checkpoint.IsValid());

    const uint64_t initial_version = checkpoint.GetVersion();

    TestCheckpointState state {};
    state.position = 1;
    checkpoint.Write(state);

    // 版本号应该增加
    EXPECT_GT(checkpoint.GetVersion(), initial_version);
}

// 测试 ShmCheckpoint Reset
TEST_F(ZrtShmCheckpointTest, Reset) {
    zrt::ShmCheckpoint<TestCheckpointState> checkpoint(shm_name_);
    ASSERT_TRUE(checkpoint.IsValid());

    TestCheckpointState state {};
    state.position = 999;
    checkpoint.Write(state);

    // 重置
    checkpoint.Reset();

    // 读取应该返回默认值或失败
    TestCheckpointState read_state {};
    // 重置后可能读取失败或返回默认值
    checkpoint.Read(read_state);
    // 重置后 version 应该回到 0
    EXPECT_EQ(checkpoint.GetVersion(), 0);
}

// 测试 ShmCheckpoint 持久化
TEST_F(ZrtShmCheckpointTest, Persistence) {
    TestCheckpointState original_state {};
    original_state.position = 12345;
    original_state.price = 3.14159;
    original_state.status = 7;
    std::strncpy(original_state.symbol, "PERSIST", sizeof(original_state.symbol));

    // 第一次写入
    {
        zrt::ShmCheckpoint<TestCheckpointState> checkpoint(shm_name_);
        ASSERT_TRUE(checkpoint.IsValid());
        checkpoint.Write(original_state);
    }

    // 重新打开并读取
    {
        zrt::ShmCheckpoint<TestCheckpointState> checkpoint(shm_name_);
        ASSERT_TRUE(checkpoint.IsValid());

        TestCheckpointState read_state {};
        EXPECT_TRUE(checkpoint.Read(read_state));
        EXPECT_EQ(read_state, original_state);
    }
}

// 测试 ShmCheckpoint 移动语义
TEST_F(ZrtShmCheckpointTest, MoveSemantics) {
    zrt::ShmCheckpoint<TestCheckpointState> checkpoint1(shm_name_);
    ASSERT_TRUE(checkpoint1.IsValid());

    TestCheckpointState state {};
    state.position = 100;
    checkpoint1.Write(state);

    // 移动构造
    zrt::ShmCheckpoint<TestCheckpointState> checkpoint2 = std::move(checkpoint1);
    EXPECT_TRUE(checkpoint2.IsValid());

    // 原对象应该无效
    EXPECT_FALSE(checkpoint1.IsValid());

    // 新对象应该能读取数据
    TestCheckpointState read_state {};
    EXPECT_TRUE(checkpoint2.Read(read_state));
    EXPECT_EQ(read_state.position, 100);
}

// 测试 ShmCheckpoint 并发读写
TEST_F(ZrtShmCheckpointTest, ConcurrentReadWrite) {
    zrt::ShmCheckpoint<TestCheckpointState> checkpoint(shm_name_);
    ASSERT_TRUE(checkpoint.IsValid());

    std::atomic<bool> running{true};
    std::atomic<int> write_count{0};
    std::atomic<int> read_count{0};

    // 写入线程
    std::thread writer([&]() {
        TestCheckpointState state {};
        int counter = 0;
        while (running) {
            state.position = ++counter;
            state.price = counter * 1.5;
            state.status = counter % 10;
            checkpoint.Write(state);
            write_count++;
        }
    });

    // 读取线程
    std::vector<std::thread> readers {};
    for (int i = 0; i < 3; ++i) {
        readers.emplace_back([&]() {
            TestCheckpointState state {};
            while (running) {
                if (checkpoint.Read(state)) {
                    read_count++;
                    // 验证数据一致性
                    EXPECT_GE(state.position, 0);
                }
            }
        });
    }

    // 运行一段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    running = false;

    writer.join();
    for (auto& r : readers) {
        r.join();
    }

    EXPECT_GT(write_count.load(), 0);
    EXPECT_GT(read_count.load(), 0);
}

// ========== ShmCheckpointReader 测试 ==========

// 测试 ShmCheckpointReader 基本功能
TEST_F(ZrtShmCheckpointTest, ReaderBasicRead) {
    TestCheckpointState write_state {};
    write_state.position = 555;
    write_state.price = 2.718;
    write_state.status = 3;
    std::strncpy(write_state.symbol, "READER", sizeof(write_state.symbol));

    // 先用 ShmCheckpoint 写入
    {
        zrt::ShmCheckpoint<TestCheckpointState> checkpoint(shm_name_);
        ASSERT_TRUE(checkpoint.IsValid());
        checkpoint.Write(write_state);
    }

    // 用 ShmCheckpointReader 读取
    {
        zrt::ShmCheckpointReader<TestCheckpointState> reader(shm_name_);
        ASSERT_TRUE(reader.IsValid());

        TestCheckpointState read_state {};
        EXPECT_TRUE(reader.Read(read_state));
        EXPECT_EQ(read_state, write_state);
    }
}

// 测试 ShmCheckpointReader GetVersion
TEST_F(ZrtShmCheckpointTest, ReaderGetVersion) {
    // 写入数据
    {
        zrt::ShmCheckpoint<TestCheckpointState> checkpoint(shm_name_);
        ASSERT_TRUE(checkpoint.IsValid());

        TestCheckpointState state {};
        state.position = 1;
        checkpoint.Write(state);
    }

    // 读取版本
    {
        zrt::ShmCheckpointReader<TestCheckpointState> reader(shm_name_);
        ASSERT_TRUE(reader.IsValid());

        const uint64_t version = reader.GetVersion();
        EXPECT_GT(version, 0);
    }
}

// 测试 ShmCheckpointReader 共享内存不存在
TEST_F(ZrtShmCheckpointTest, ReaderNonExistent) {
    zrt::ShmCheckpointReader<TestCheckpointState> reader("non_existent_shm");

    EXPECT_FALSE(reader.IsValid());

    TestCheckpointState state {};
    EXPECT_FALSE(reader.Read(state));
}

// 测试大数据结构
struct LargeState {
    int64_t values[100];
    double prices[50];
    char buffer[256];
};

TEST_F(ZrtShmCheckpointTest, LargeStateStructure) {
    const std::string large_shm = shm_name_ + "_large";

    LargeState write_state {};
    for (int i = 0; i < 100; ++i) {
        write_state.values[i] = i * 1000;
    }
    for (int i = 0; i < 50; ++i) {
        write_state.prices[i] = i * 1.5;
    }
    std::strncpy(write_state.buffer, "large state test", sizeof(write_state.buffer));

    // 写入
    {
        zrt::ShmCheckpoint<LargeState> checkpoint(large_shm);
        ASSERT_TRUE(checkpoint.IsValid());
        checkpoint.Write(write_state);
    }

    // 读取
    {
        zrt::ShmCheckpoint<LargeState> checkpoint(large_shm);
        ASSERT_TRUE(checkpoint.IsValid());

        LargeState read_state {};
        EXPECT_TRUE(checkpoint.Read(read_state));

        for (int i = 0; i < 100; ++i) {
            EXPECT_EQ(read_state.values[i], write_state.values[i]);
        }
        for (int i = 0; i < 50; ++i) {
            EXPECT_DOUBLE_EQ(read_state.prices[i], write_state.prices[i]);
        }
        EXPECT_STREQ(read_state.buffer, write_state.buffer);
    }

    boost::interprocess::shared_memory_object::remove(large_shm.c_str());
}
