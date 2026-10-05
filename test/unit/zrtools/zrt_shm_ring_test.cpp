//
// zrt_shm_ring_test.cpp - 共享内存 WAL 环形缓冲区单元测试
//

#include "pch.h"
#include "gtest/gtest.h"
#include "zrtools/zrt_shm_ring.h"
#include <thread>
#include <chrono>

// 测试用的条目类型枚举
enum class TestRingType : uint16_t {
    kInvalid = 0,
    kMessage = 1,
    kEvent = 2,
    kData = 3,
};

// 测试数据结构
struct TestMessage {
    int64_t id;
    double value;
    char content[64];
};

// 使用较小的缓冲区进行测试
using TestShmRing = zrt::ShmRing<TestRingType, 4096, 512>;

class ShmRingTest : public ::testing::Test {
protected:
    void SetUp() override {
        shm_name_ = "test_ring_" + std::to_string(getpid());
        // 清理可能残留的共享内存
        boost::interprocess::shared_memory_object::remove(shm_name_.c_str());
    }

    void TearDown() override {
        // 清理共享内存
        boost::interprocess::shared_memory_object::remove(shm_name_.c_str());
    }

    std::string shm_name_;
};

// 测试 ShmRing 创建和初始化
TEST_F(ShmRingTest, Creation) {
    TestShmRing ring(shm_name_);

    EXPECT_TRUE(ring.IsValid());
    EXPECT_EQ(ring.GetShmName(), shm_name_);
    EXPECT_EQ(ring.GetWritePos(), 0);
    EXPECT_EQ(ring.GetReadPos(), 0);
    EXPECT_EQ(ring.GetConfirmedPos(), 0);
    EXPECT_EQ(ring.GetNextSeq(), 1);
}

// 测试 ShmRing 基本写入
TEST_F(ShmRingTest, BasicAppend) {
    TestShmRing ring(shm_name_);
    ASSERT_TRUE(ring.IsValid());

    const std::string data = "test message";
    const uint64_t seq = ring.Append(TestRingType::kMessage,
                                     data.data(),
                                     static_cast<uint16_t>(data.size()));

    EXPECT_EQ(seq, 1);
    EXPECT_GT(ring.GetWritePos(), 0);
    EXPECT_EQ(ring.GetNextSeq(), 2);
    EXPECT_EQ(ring.GetTotalEntries(), 1);
}

// 测试 ShmRing 多次写入
TEST_F(ShmRingTest, MultipleAppends) {
    TestShmRing ring(shm_name_);
    ASSERT_TRUE(ring.IsValid());

    for (int i = 1; i <= 10; ++i) {
        const std::string data = "message_" + std::to_string(i);
        const uint64_t seq = ring.Append(TestRingType::kMessage,
                                         data.data(),
                                         static_cast<uint16_t>(data.size()));
        EXPECT_EQ(seq, i);
    }

    EXPECT_EQ(ring.GetTotalEntries(), 10);
    EXPECT_EQ(ring.GetNextSeq(), 11);
}

// 测试 ShmRing 结构体数据写入
TEST_F(ShmRingTest, StructAppend) {
    TestShmRing ring(shm_name_);
    ASSERT_TRUE(ring.IsValid());

    TestMessage msg {};
    msg.id = 12345;
    msg.value = 3.14159;
    std::strncpy(msg.content, "Hello Ring!", sizeof(msg.content));

    const uint64_t seq = ring.Append(TestRingType::kData, msg);
    EXPECT_EQ(seq, 1);
}

// 测试 ShmRing ReadNext
TEST_F(ShmRingTest, ReadNext) {
    TestShmRing ring(shm_name_);
    ASSERT_TRUE(ring.IsValid());

    // 写入数据
    const std::string data = "read test data";
    ring.Append(TestRingType::kMessage, data.data(),
                static_cast<uint16_t>(data.size()));

    // 读取
    zrt::WalEntryHeader<TestRingType> header {};
    char buffer[256] = {};

    EXPECT_TRUE(ring.ReadNext(header, buffer, sizeof(buffer)));

    EXPECT_EQ(header.seq, 1);
    EXPECT_EQ(header.type, TestRingType::kMessage);
    EXPECT_EQ(header.data_len, data.size());
    EXPECT_EQ(std::string(buffer, header.data_len), data);
}

// 测试 ShmRing 多次读取
TEST_F(ShmRingTest, MultipleReadNext) {
    TestShmRing ring(shm_name_);
    ASSERT_TRUE(ring.IsValid());

    // 写入多条数据
    for (int i = 1; i <= 5; ++i) {
        const std::string data = "msg_" + std::to_string(i);
        ring.Append(TestRingType::kMessage, data.data(),
                    static_cast<uint16_t>(data.size()));
    }

    // 逐条读取
    for (int i = 1; i <= 5; ++i) {
        zrt::WalEntryHeader<TestRingType> header {};
        char buffer[256] = {};

        EXPECT_TRUE(ring.ReadNext(header, buffer, sizeof(buffer)));
        EXPECT_EQ(header.seq, i);

        const std::string expected = "msg_" + std::to_string(i);
        EXPECT_EQ(std::string(buffer, header.data_len), expected);
    }

    // 没有更多数据
    zrt::WalEntryHeader<TestRingType> header {};
    char buffer[256] = {};
    EXPECT_FALSE(ring.ReadNext(header, buffer, sizeof(buffer)));
}

// 测试 ShmRing ReadUnconfirmed
TEST_F(ShmRingTest, ReadUnconfirmed) {
    TestShmRing ring(shm_name_);
    ASSERT_TRUE(ring.IsValid());

    // 写入数据
    for (int i = 1; i <= 10; ++i) {
        const std::string data = "unconf_" + std::to_string(i);
        ring.Append(TestRingType::kEvent, data.data(),
                    static_cast<uint16_t>(data.size()));
    }

    // 读取未确认的条目
    std::vector<std::pair<zrt::WalEntryHeader<TestRingType>, std::vector<char>>> entries {};
    const size_t count = ring.ReadUnconfirmed(entries, 100);

    EXPECT_EQ(count, 10);
    EXPECT_EQ(entries.size(), 10);

    // 验证序号
    for (size_t i = 0; i < entries.size(); ++i) {
        EXPECT_EQ(entries[i].first.seq, i + 1);
    }
}

// 测试 ShmRing Confirm
TEST_F(ShmRingTest, Confirm) {
    TestShmRing ring(shm_name_);
    ASSERT_TRUE(ring.IsValid());

    // 写入数据
    for (int i = 1; i <= 5; ++i) {
        const std::string data = "confirm_" + std::to_string(i);
        ring.Append(TestRingType::kMessage, data.data(),
                    static_cast<uint16_t>(data.size()));
    }

    const uint64_t initial_confirmed = ring.GetConfirmedPos();

    // 确认序号 3
    ring.Confirm(3);

    // 确认位置应该更新
    EXPECT_GT(ring.GetConfirmedPos(), initial_confirmed);

    // 读取未确认的条目应该只有 4, 5
    std::vector<std::pair<zrt::WalEntryHeader<TestRingType>, std::vector<char>>> entries {};
    ring.ReadUnconfirmed(entries, 100);

    EXPECT_EQ(entries.size(), 2);
    EXPECT_EQ(entries[0].first.seq, 4);
    EXPECT_EQ(entries[1].first.seq, 5);
}

// 测试 ShmRing ConfirmBatch
TEST_F(ShmRingTest, ConfirmBatch) {
    TestShmRing ring(shm_name_);
    ASSERT_TRUE(ring.IsValid());

    // 写入数据
    for (int i = 1; i <= 10; ++i) {
        const std::string data = "batch_" + std::to_string(i);
        ring.Append(TestRingType::kData, data.data(),
                    static_cast<uint16_t>(data.size()));
    }

    // 批量确认前 5 条
    ring.ConfirmBatch(5);

    // 读取未确认的条目应该只有 5 条
    std::vector<std::pair<zrt::WalEntryHeader<TestRingType>, std::vector<char>>> entries {};
    ring.ReadUnconfirmed(entries, 100);

    EXPECT_EQ(entries.size(), 5);
    EXPECT_EQ(entries[0].first.seq, 6);
}

// 测试 ShmRing Reset
TEST_F(ShmRingTest, Reset) {
    TestShmRing ring(shm_name_);
    ASSERT_TRUE(ring.IsValid());

    // 写入一些数据
    for (int i = 0; i < 5; ++i) {
        const std::string data = "reset_" + std::to_string(i);
        ring.Append(TestRingType::kMessage, data.data(),
                    static_cast<uint16_t>(data.size()));
    }

    EXPECT_GT(ring.GetWritePos(), 0);
    EXPECT_GT(ring.GetTotalEntries(), 0);

    // 重置
    ring.Reset();

    EXPECT_EQ(ring.GetWritePos(), 0);
    EXPECT_EQ(ring.GetReadPos(), 0);
    EXPECT_EQ(ring.GetConfirmedPos(), 0);
    EXPECT_EQ(ring.GetNextSeq(), 1);
    EXPECT_EQ(ring.GetTotalEntries(), 0);
}

// 测试 ShmRing 统计信息
TEST_F(ShmRingTest, Statistics) {
    TestShmRing ring(shm_name_);
    ASSERT_TRUE(ring.IsValid());

    const std::string data = "stat test";
    ring.Append(TestRingType::kMessage, data.data(),
                static_cast<uint16_t>(data.size()));

    EXPECT_EQ(ring.GetTotalEntries(), 1);
    EXPECT_GT(ring.GetTotalBytes(), 0);
    EXPECT_GT(ring.GetUnconfirmedBytes(), 0);
}

// 测试 ShmRing 持久化
TEST_F(ShmRingTest, Persistence) {
    // 写入数据
    {
        TestShmRing ring(shm_name_);
        ASSERT_TRUE(ring.IsValid());

        for (int i = 1; i <= 3; ++i) {
            const std::string data = "persist_" + std::to_string(i);
            ring.Append(TestRingType::kEvent, data.data(),
                        static_cast<uint16_t>(data.size()));
        }
    }

    // 重新打开
    {
        TestShmRing ring(shm_name_);
        ASSERT_TRUE(ring.IsValid());

        // 写入位置应该保持
        EXPECT_GT(ring.GetWritePos(), 0);
        EXPECT_EQ(ring.GetNextSeq(), 4);

        // 应该能读取未确认的数据
        std::vector<std::pair<zrt::WalEntryHeader<TestRingType>, std::vector<char>>> entries {};
        ring.ReadUnconfirmed(entries, 100);

        EXPECT_EQ(entries.size(), 3);
    }
}

// 测试 ShmRing 数据过大
TEST_F(ShmRingTest, DataTooLarge) {
    TestShmRing ring(shm_name_);
    ASSERT_TRUE(ring.IsValid());

    // 创建超过最大条目大小的数据
    std::vector<char> large_data(TestShmRing::MAX_ENTRY_SIZE + 100, 'X');

    const uint64_t seq = ring.Append(TestRingType::kData,
                                     large_data.data(),
                                     static_cast<uint16_t>(large_data.size()));

    // 应该返回 0 表示失败
    EXPECT_EQ(seq, 0);
}

// 测试并发读写
TEST_F(ShmRingTest, ConcurrentReadWrite) {
    TestShmRing ring(shm_name_);
    ASSERT_TRUE(ring.IsValid());

    std::atomic<bool> running{true};
    std::atomic<int> write_count{0};
    std::atomic<int> read_count{0};

    // 写入线程
    std::thread writer([&]() {
        int counter = 0;
        while (running && counter < 100) {
            const std::string data = "concurrent_" + std::to_string(counter++);
            if (ring.Append(TestRingType::kMessage, data.data(),
                            static_cast<uint16_t>(data.size())) > 0) {
                write_count++;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    // 读取线程
    std::thread reader([&]() {
        while (running || ring.GetReadPos() < ring.GetWritePos()) {
            zrt::WalEntryHeader<TestRingType> header {};
            char buffer[256] = {};

            if (ring.ReadNext(header, buffer, sizeof(buffer))) {
                read_count++;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });

    // 运行一段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    running = false;

    writer.join();
    reader.join();

    EXPECT_GT(write_count.load(), 0);
    // 读取数量可能小于等于写入数量
    EXPECT_LE(read_count.load(), write_count.load());
}

// 测试空读取
TEST_F(ShmRingTest, EmptyRead) {
    TestShmRing ring(shm_name_);
    ASSERT_TRUE(ring.IsValid());

    zrt::WalEntryHeader<TestRingType> header {};
    char buffer[256] = {};

    // 空缓冲区读取应该返回 false
    EXPECT_FALSE(ring.ReadNext(header, buffer, sizeof(buffer)));
}

// 测试 GetUnconfirmedBytes
TEST_F(ShmRingTest, GetUnconfirmedBytes) {
    TestShmRing ring(shm_name_);
    ASSERT_TRUE(ring.IsValid());

    EXPECT_EQ(ring.GetUnconfirmedBytes(), 0);

    const std::string data = "unconfirmed bytes test";
    ring.Append(TestRingType::kMessage, data.data(),
                static_cast<uint16_t>(data.size()));

    EXPECT_GT(ring.GetUnconfirmedBytes(), 0);

    // 确认后应该减少
    ring.ConfirmBatch(1);
    EXPECT_EQ(ring.GetUnconfirmedBytes(), 0);
}
