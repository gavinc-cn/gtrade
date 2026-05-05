//
// zrt_wal_file_test.cpp - WAL 文件读写器单元测试
//

#include "pch.h"
#include "gtest/gtest.h"
#include "zrtools/zrt_time.h"  // 需要在 zrt_wal_file.h 之前包含
#include "zrtools/zrt_wal_file.h"
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

// 定义测试用的条目类型枚举
enum class TestWalType : uint16_t {
    kInvalid = 0,
    kOrder = 1,
    kTrade = 2,
    kPosition = 3,
};

// 测试数据结构
struct OrderData {
    int64_t order_id;
    double price;
    double qty;
    char symbol[16];
};

class WalFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "/tmp/wal_file_test";

        // 清理并创建测试目录
        fs::remove_all(test_dir_);
        fs::create_directories(test_dir_);
    }

    void TearDown() override {
        // 清理测试目录
        fs::remove_all(test_dir_);
    }

    std::string test_dir_;
};

// ========== WalFileHeader 测试 ==========

// 测试 WalFileHeader 大小
TEST(WalFileHeaderTest, HeaderSize) {
    EXPECT_EQ(sizeof(zrt::WalFileHeader), 64);
}

// ========== WalFileWriter 测试 ==========

// 测试 WalFileWriter 初始化
TEST_F(WalFileTest, WriterInit) {
    zrt::WalFileWriter<TestWalType> writer(test_dir_, "test_wal");

    EXPECT_TRUE(writer.Init());
    EXPECT_EQ(writer.GetCurrentSeq(), 0);
}

// 测试 WalFileWriter 基本写入
TEST_F(WalFileTest, WriterBasicAppend) {
    zrt::WalFileWriter<TestWalType> writer(test_dir_, "test_wal");
    ASSERT_TRUE(writer.Init());

    const std::string data = "test data";
    const uint64_t seq = writer.Append(TestWalType::kOrder,
                                       data.data(),
                                       static_cast<uint16_t>(data.size()));

    EXPECT_EQ(seq, 1);
    EXPECT_EQ(writer.GetCurrentSeq(), 1);
}

// 测试 WalFileWriter 多次写入
TEST_F(WalFileTest, WriterMultipleAppends) {
    zrt::WalFileWriter<TestWalType> writer(test_dir_, "test_wal");
    ASSERT_TRUE(writer.Init());

    for (int i = 1; i <= 100; ++i) {
        const std::string data = "data_" + std::to_string(i);
        const uint64_t seq = writer.Append(TestWalType::kTrade,
                                           data.data(),
                                           static_cast<uint16_t>(data.size()));
        EXPECT_EQ(seq, i);
    }

    EXPECT_EQ(writer.GetCurrentSeq(), 100);
}

// 测试 WalFileWriter 结构体数据写入
TEST_F(WalFileTest, WriterStructAppend) {
    zrt::WalFileWriter<TestWalType> writer(test_dir_, "test_wal");
    ASSERT_TRUE(writer.Init());

    OrderData order {};
    order.order_id = 12345;
    order.price = 100.50;
    order.qty = 10.0;
    std::strncpy(order.symbol, "BTC-USDT", sizeof(order.symbol));

    const uint64_t seq = writer.Append(TestWalType::kOrder, order);
    EXPECT_EQ(seq, 1);
}

// 测试 WalFileWriter Sync
TEST_F(WalFileTest, WriterSync) {
    zrt::WalFileWriter<TestWalType> writer(test_dir_, "test_wal");
    ASSERT_TRUE(writer.Init());

    const std::string data = "sync test";
    writer.Append(TestWalType::kOrder, data.data(),
                  static_cast<uint16_t>(data.size()));

    EXPECT_NO_THROW(writer.Sync());
}

// 测试 WalFileWriter Close
TEST_F(WalFileTest, WriterClose) {
    zrt::WalFileWriter<TestWalType> writer(test_dir_, "test_wal");
    ASSERT_TRUE(writer.Init());

    const std::string data = "close test";
    writer.Append(TestWalType::kOrder, data.data(),
                  static_cast<uint16_t>(data.size()));

    EXPECT_NO_THROW(writer.Close());

    // 验证文件已创建
    bool found_wal = false;
    for (const auto& entry : fs::directory_iterator(test_dir_)) {
        if (entry.path().extension() == ".wal") {
            found_wal = true;
            break;
        }
    }
    EXPECT_TRUE(found_wal);
}

// 测试 WalFileWriter SetSyncInterval
TEST_F(WalFileTest, WriterSyncInterval) {
    zrt::WalFileWriter<TestWalType> writer(test_dir_, "test_wal");
    writer.SetSyncInterval(50);
    ASSERT_TRUE(writer.Init());

    // 写入超过同步间隔数量的条目
    for (int i = 0; i < 60; ++i) {
        const std::string data = "interval_test_" + std::to_string(i);
        writer.Append(TestWalType::kOrder, data.data(),
                      static_cast<uint16_t>(data.size()));
    }

    EXPECT_EQ(writer.GetCurrentSeq(), 60);
}

// 测试 WalFileWriter GetTotalSize
TEST_F(WalFileTest, WriterGetTotalSize) {
    zrt::WalFileWriter<TestWalType> writer(test_dir_, "test_wal");
    ASSERT_TRUE(writer.Init());

    // 写入一些数据
    for (int i = 0; i < 10; ++i) {
        const std::string data = "size_test_" + std::to_string(i);
        writer.Append(TestWalType::kOrder, data.data(),
                      static_cast<uint16_t>(data.size()));
    }
    writer.Sync();

    const size_t total_size = writer.GetTotalSize();
    EXPECT_GT(total_size, 0);
}

// ========== WalFileReader 测试 ==========

// 测试 WalFileReader 基本读取
TEST_F(WalFileTest, ReaderBasicRead) {
    // 先写入数据
    {
        zrt::WalFileWriter<TestWalType> writer(test_dir_, "test_wal");
        ASSERT_TRUE(writer.Init());

        for (int i = 1; i <= 10; ++i) {
            const std::string data = "read_test_" + std::to_string(i);
            writer.Append(TestWalType::kTrade, data.data(),
                          static_cast<uint16_t>(data.size()));
        }
        writer.Close();
    }

    // 读取数据
    zrt::WalFileReader<TestWalType> reader(test_dir_, "test_wal");
    std::vector<std::pair<zrt::WalEntryHeader<TestWalType>, std::vector<char>>> entries {};

    const size_t count = reader.ReadAfter(0, entries);
    EXPECT_EQ(count, 10);
    EXPECT_EQ(entries.size(), 10);

    // 验证序号
    for (size_t i = 0; i < entries.size(); ++i) {
        EXPECT_EQ(entries[i].first.seq, i + 1);
        EXPECT_EQ(entries[i].first.type, TestWalType::kTrade);
    }
}

// 测试 WalFileReader ReadAfter 特定序号
TEST_F(WalFileTest, ReaderReadAfterSpecificSeq) {
    // 先写入数据
    {
        zrt::WalFileWriter<TestWalType> writer(test_dir_, "test_wal");
        ASSERT_TRUE(writer.Init());

        for (int i = 1; i <= 10; ++i) {
            const std::string data = "seq_test_" + std::to_string(i);
            writer.Append(TestWalType::kOrder, data.data(),
                          static_cast<uint16_t>(data.size()));
        }
        writer.Close();
    }

    // 从序号 5 之后开始读取
    zrt::WalFileReader<TestWalType> reader(test_dir_, "test_wal");
    std::vector<std::pair<zrt::WalEntryHeader<TestWalType>, std::vector<char>>> entries {};

    const size_t count = reader.ReadAfter(5, entries);
    EXPECT_EQ(count, 5);  // 应该读取 6, 7, 8, 9, 10

    // 验证序号
    for (size_t i = 0; i < entries.size(); ++i) {
        EXPECT_EQ(entries[i].first.seq, i + 6);
    }
}

// 测试 WalFileReader max_count 限制
TEST_F(WalFileTest, ReaderMaxCount) {
    // 先写入数据
    {
        zrt::WalFileWriter<TestWalType> writer(test_dir_, "test_wal");
        ASSERT_TRUE(writer.Init());

        for (int i = 1; i <= 100; ++i) {
            const std::string data = "max_count_" + std::to_string(i);
            writer.Append(TestWalType::kPosition, data.data(),
                          static_cast<uint16_t>(data.size()));
        }
        writer.Close();
    }

    // 限制读取 20 条
    zrt::WalFileReader<TestWalType> reader(test_dir_, "test_wal");
    std::vector<std::pair<zrt::WalEntryHeader<TestWalType>, std::vector<char>>> entries {};

    const size_t count = reader.ReadAfter(0, entries, 20);
    EXPECT_EQ(count, 20);
}

// 测试 WalFileReader 空目录
TEST_F(WalFileTest, ReaderEmptyDirectory) {
    zrt::WalFileReader<TestWalType> reader(test_dir_, "test_wal");
    std::vector<std::pair<zrt::WalEntryHeader<TestWalType>, std::vector<char>>> entries {};

    const size_t count = reader.ReadAfter(0, entries);
    EXPECT_EQ(count, 0);
}

// 测试 WalFileReader 结构体数据读取
TEST_F(WalFileTest, ReaderStructData) {
    OrderData original {};
    original.order_id = 99999;
    original.price = 50000.0;
    original.qty = 0.5;
    std::strncpy(original.symbol, "ETH-USDT", sizeof(original.symbol));

    // 写入结构体
    {
        zrt::WalFileWriter<TestWalType> writer(test_dir_, "test_wal");
        ASSERT_TRUE(writer.Init());
        writer.Append(TestWalType::kOrder, original);
        writer.Close();
    }

    // 读取并验证
    zrt::WalFileReader<TestWalType> reader(test_dir_, "test_wal");
    std::vector<std::pair<zrt::WalEntryHeader<TestWalType>, std::vector<char>>> entries {};

    reader.ReadAfter(0, entries);
    ASSERT_EQ(entries.size(), 1);

    const auto& entry = entries[0];
    EXPECT_EQ(entry.first.type, TestWalType::kOrder);
    EXPECT_EQ(entry.second.size(), sizeof(OrderData));

    const auto* retrieved = reinterpret_cast<const OrderData*>(entry.second.data());
    EXPECT_EQ(retrieved->order_id, original.order_id);
    EXPECT_DOUBLE_EQ(retrieved->price, original.price);
    EXPECT_DOUBLE_EQ(retrieved->qty, original.qty);
    EXPECT_STREQ(retrieved->symbol, original.symbol);
}

// 测试 Writer 重新打开继续序号
// 注意：prefix 不能包含下划线，因为 ParseSeqFromFilename 使用下划线分割
TEST_F(WalFileTest, WriterContinueSequence) {
    // 第一次写入
    {
        zrt::WalFileWriter<TestWalType> writer(test_dir_, "wal");
        ASSERT_TRUE(writer.Init());

        for (int i = 1; i <= 5; ++i) {
            const std::string data = "first_" + std::to_string(i);
            writer.Append(TestWalType::kOrder, data.data(),
                          static_cast<uint16_t>(data.size()));
        }
        writer.Close();
    }

    // 第二次打开，序号应该继续
    {
        zrt::WalFileWriter<TestWalType> writer(test_dir_, "wal");
        ASSERT_TRUE(writer.Init());

        EXPECT_EQ(writer.GetCurrentSeq(), 5);

        const std::string data = "second_1";
        const uint64_t seq = writer.Append(TestWalType::kOrder, data.data(),
                                           static_cast<uint16_t>(data.size()));
        EXPECT_EQ(seq, 6);
        writer.Close();
    }

    // 验证所有数据都可读取
    zrt::WalFileReader<TestWalType> reader(test_dir_, "wal");
    std::vector<std::pair<zrt::WalEntryHeader<TestWalType>, std::vector<char>>> entries {};

    reader.ReadAfter(0, entries);
    EXPECT_EQ(entries.size(), 6);
}

// 测试 TruncateBefore
TEST_F(WalFileTest, TruncateBefore) {
    // 写入多个 WAL 文件的数据量
    {
        zrt::WalFileWriter<TestWalType> writer(test_dir_, "test_wal");
        ASSERT_TRUE(writer.Init());

        for (int i = 1; i <= 50; ++i) {
            const std::string data = "truncate_" + std::to_string(i);
            writer.Append(TestWalType::kOrder, data.data(),
                          static_cast<uint16_t>(data.size()));
        }
        writer.Close();
    }

    // 注意：TruncateBefore 只删除整个文件序号都小于指定值的文件
    // 在这个测试中，只有一个文件，所以不会删除
    zrt::WalFileWriter<TestWalType> writer(test_dir_, "test_wal");
    const size_t deleted = writer.TruncateBefore(25);

    // 由于只有一个文件，不会删除
    EXPECT_EQ(deleted, 0);
}
