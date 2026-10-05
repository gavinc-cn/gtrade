//
// zrt_wal_entry_test.cpp - WAL 条目单元测试
//

#include "pch.h"
#include "gtest/gtest.h"
#include "zrtools/zrt_wal_entry.h"
#include <cstring>
#include <vector>

// 定义测试用的条目类型枚举
enum class TestEntryType : uint16_t {
    kInvalid = 0,
    kInsert = 1,
    kUpdate = 2,
    kDelete = 3,
};

// 测试数据结构
struct TestData {
    int64_t id;
    double value;
    char name[32];

    bool operator==(const TestData& other) const {
        return id == other.id &&
               value == other.value &&
               std::strcmp(name, other.name) == 0;
    }
};

// ========== WalEntryHeader 测试 ==========

// 测试 WalEntryHeader 大小
TEST(WalEntryHeaderTest, HeaderSize) {
    // 头部应该是 24 字节
    EXPECT_EQ(sizeof(zrt::WalEntryHeader<TestEntryType>), 24);
    EXPECT_EQ(zrt::kWalEntryHeaderSize<TestEntryType>, 24);
}

// 测试 CRC32 计算
TEST(WalEntryHeaderTest, CalcCrc32) {
    const std::string data = "test data";
    const uint32_t crc = zrt::WalEntryHeader<TestEntryType>::CalcCrc32(
        data.data(), data.size());

    EXPECT_NE(crc, 0);

    // 相同数据应该得到相同的 CRC
    const uint32_t crc2 = zrt::WalEntryHeader<TestEntryType>::CalcCrc32(
        data.data(), data.size());
    EXPECT_EQ(crc, crc2);
}

// ========== WalEntry 测试 ==========

// 测试 WalEntry ValidateCrc 和 UpdateCrc
TEST(WalEntryTest, CrcValidation) {
    // 创建一个足够大的缓冲区
    alignas(8) char buffer[128] = {};
    auto* entry = reinterpret_cast<zrt::WalEntry<TestEntryType>*>(buffer);

    // 设置头部
    entry->header.seq = 1;
    entry->header.timestamp_ns = 1234567890;
    entry->header.type = TestEntryType::kInsert;
    entry->header.data_len = 10;

    // 写入数据
    std::memcpy(entry->data, "0123456789", 10);

    // 更新 CRC
    entry->UpdateCrc();

    // 验证应该成功
    EXPECT_TRUE(entry->ValidateCrc());

    // 修改数据后验证应该失败
    // 注：WalEntry::data 是柔性数组成员（char[0]），经 entry->data 取下标会让 GCC 13
    // 在 -O3 下报 -Werror=array-bounds（subscript 0 outside 'char [0]'），故改为按原始
    // 缓冲区 + 头部偏移写（等价于 data[0]）。
    buffer[zrt::kWalEntryHeaderSize<TestEntryType>] = 'X';
    EXPECT_FALSE(entry->ValidateCrc());

    // 重新更新 CRC 后应该成功
    entry->UpdateCrc();
    EXPECT_TRUE(entry->ValidateCrc());
}

// 测试空数据的 CRC 验证
TEST(WalEntryTest, EmptyDataCrcValidation) {
    alignas(8) char buffer[64] = {};
    auto* entry = reinterpret_cast<zrt::WalEntry<TestEntryType>*>(buffer);

    entry->header.seq = 1;
    entry->header.type = TestEntryType::kDelete;
    entry->header.data_len = 0;

    entry->UpdateCrc();
    EXPECT_EQ(entry->header.crc32, 0);
    EXPECT_TRUE(entry->ValidateCrc());
}

// 测试 WalEntry TotalSize
TEST(WalEntryTest, TotalSize) {
    alignas(8) char buffer[128] = {};
    auto* entry = reinterpret_cast<zrt::WalEntry<TestEntryType>*>(buffer);

    entry->header.data_len = 50;

    // 总大小 = 头部大小 + 数据大小
    EXPECT_EQ(entry->TotalSize(), 24 + 50);
}

// 测试 WalEntry GetData 方法
TEST(WalEntryTest, GetData) {
    alignas(8) char buffer[128] = {};
    auto* entry = reinterpret_cast<zrt::WalEntry<TestEntryType>*>(buffer);

    entry->header.data_len = 10;
    std::memcpy(entry->data, "test data!", 10);

    const auto* const_data = entry->GetData();
    EXPECT_EQ(std::memcmp(const_data, "test data!", 10), 0);

    auto* mutable_data = entry->GetData();
    EXPECT_EQ(mutable_data, entry->data);
}

// 测试 WalEntry GetDataLen
TEST(WalEntryTest, GetDataLen) {
    alignas(8) char buffer[64] = {};
    auto* entry = reinterpret_cast<zrt::WalEntry<TestEntryType>*>(buffer);

    entry->header.data_len = 42;
    EXPECT_EQ(entry->GetDataLen(), 42);
}

// 测试 WalEntry GetDataAs
TEST(WalEntryTest, GetDataAs) {
    alignas(8) char buffer[128] = {};
    auto* entry = reinterpret_cast<zrt::WalEntry<TestEntryType>*>(buffer);

    TestData data {};
    data.id = 12345;
    data.value = 3.14159;
    std::strncpy(data.name, "test", sizeof(data.name));

    entry->header.data_len = sizeof(TestData);
    std::memcpy(entry->data, &data, sizeof(TestData));

    const auto* retrieved = entry->GetDataAs<TestData>();
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->id, 12345);
    EXPECT_DOUBLE_EQ(retrieved->value, 3.14159);
    EXPECT_STREQ(retrieved->name, "test");
}

// 测试 GetDataAs 数据长度不足
TEST(WalEntryTest, GetDataAsInsufficientLength) {
    alignas(8) char buffer[64] = {};
    auto* entry = reinterpret_cast<zrt::WalEntry<TestEntryType>*>(buffer);

    entry->header.data_len = sizeof(TestData) - 1;  // 长度不足

    const auto* retrieved = entry->GetDataAs<TestData>();
    EXPECT_EQ(retrieved, nullptr);
}

// ========== WalEntryBuilder 测试 ==========

// 测试 WalEntryBuilder 基本构建
TEST(WalEntryBuilderTest, BasicBuild) {
    zrt::WalEntryBuilder<TestEntryType> builder {};

    const std::string data = "test data";
    EXPECT_TRUE(builder.Build(1, 1000000000, TestEntryType::kInsert,
                              data.data(), static_cast<uint16_t>(data.size())));

    const auto* entry = builder.GetEntry();
    ASSERT_NE(entry, nullptr);

    EXPECT_EQ(entry->header.seq, 1);
    EXPECT_EQ(entry->header.timestamp_ns, 1000000000);
    EXPECT_EQ(entry->header.type, TestEntryType::kInsert);
    EXPECT_EQ(entry->header.data_len, data.size());
    EXPECT_TRUE(entry->ValidateCrc());
}

// 测试 WalEntryBuilder 结构体数据
TEST(WalEntryBuilderTest, StructData) {
    zrt::WalEntryBuilder<TestEntryType> builder {};

    TestData data {};
    data.id = 999;
    data.value = 2.71828;
    std::strncpy(data.name, "builder test", sizeof(data.name));

    EXPECT_TRUE(builder.Build(10, 2000000000, TestEntryType::kUpdate, data));

    const auto* entry = builder.GetEntry();
    ASSERT_NE(entry, nullptr);

    EXPECT_EQ(entry->header.seq, 10);
    EXPECT_EQ(entry->header.data_len, sizeof(TestData));
    EXPECT_TRUE(entry->ValidateCrc());

    const auto* retrieved = entry->GetDataAs<TestData>();
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(*retrieved, data);
}

// 测试 WalEntryBuilder 数据过大
TEST(WalEntryBuilderTest, DataTooLarge) {
    zrt::WalEntryBuilder<TestEntryType, 64> builder {};  // 最大 64 字节

    std::vector<char> large_data(100, 'X');  // 100 字节，超过最大大小

    EXPECT_FALSE(builder.Build(1, 1000, TestEntryType::kInsert,
                               large_data.data(),
                               static_cast<uint16_t>(large_data.size())));

    EXPECT_EQ(builder.GetEntry(), nullptr);
}

// 测试 WalEntryBuilder GetSize
TEST(WalEntryBuilderTest, GetSize) {
    zrt::WalEntryBuilder<TestEntryType> builder {};

    EXPECT_EQ(builder.GetSize(), 0);

    const std::string data = "size test";
    builder.Build(1, 1000, TestEntryType::kInsert,
                  data.data(), static_cast<uint16_t>(data.size()));

    EXPECT_EQ(builder.GetSize(), 24 + data.size());  // 头部 + 数据
}

// 测试 WalEntryBuilder GetBuffer
TEST(WalEntryBuilderTest, GetBuffer) {
    zrt::WalEntryBuilder<TestEntryType> builder {};

    EXPECT_NE(builder.GetBuffer(), nullptr);

    const std::string data = "buffer test";
    builder.Build(1, 1000, TestEntryType::kInsert,
                  data.data(), static_cast<uint16_t>(data.size()));

    const char* buffer = builder.GetBuffer();
    EXPECT_NE(buffer, nullptr);
}

// 测试 WalEntryBuilder Reset
TEST(WalEntryBuilderTest, Reset) {
    zrt::WalEntryBuilder<TestEntryType> builder {};

    const std::string data = "reset test";
    builder.Build(1, 1000, TestEntryType::kInsert,
                  data.data(), static_cast<uint16_t>(data.size()));

    EXPECT_GT(builder.GetSize(), 0);

    builder.Reset();

    EXPECT_EQ(builder.GetSize(), 0);
    EXPECT_EQ(builder.GetEntry(), nullptr);
}

// 测试 WalEntryBuilder 空数据
TEST(WalEntryBuilderTest, EmptyData) {
    zrt::WalEntryBuilder<TestEntryType> builder {};

    EXPECT_TRUE(builder.Build(1, 1000, TestEntryType::kDelete, nullptr, 0));

    const auto* entry = builder.GetEntry();
    ASSERT_NE(entry, nullptr);

    EXPECT_EQ(entry->header.data_len, 0);
    EXPECT_EQ(entry->header.crc32, 0);
    EXPECT_TRUE(entry->ValidateCrc());
}

// ========== AlignUp 测试 ==========

// 测试 AlignUp 函数
TEST(AlignUpTest, BasicAlignment) {
    // 8 字节对齐
    EXPECT_EQ(zrt::AlignUp(1, 8), 8);
    EXPECT_EQ(zrt::AlignUp(7, 8), 8);
    EXPECT_EQ(zrt::AlignUp(8, 8), 8);
    EXPECT_EQ(zrt::AlignUp(9, 8), 16);
    EXPECT_EQ(zrt::AlignUp(15, 8), 16);
    EXPECT_EQ(zrt::AlignUp(16, 8), 16);
    EXPECT_EQ(zrt::AlignUp(17, 8), 24);

    // 4 字节对齐
    EXPECT_EQ(zrt::AlignUp(1, 4), 4);
    EXPECT_EQ(zrt::AlignUp(4, 4), 4);
    EXPECT_EQ(zrt::AlignUp(5, 4), 8);

    // 16 字节对齐
    EXPECT_EQ(zrt::AlignUp(1, 16), 16);
    EXPECT_EQ(zrt::AlignUp(16, 16), 16);
    EXPECT_EQ(zrt::AlignUp(17, 16), 32);
}

// 测试 AlignUp 边界情况
TEST(AlignUpTest, ZeroValue) {
    EXPECT_EQ(zrt::AlignUp(0, 8), 0);
}

// 测试大值对齐
TEST(AlignUpTest, LargeValues) {
    EXPECT_EQ(zrt::AlignUp(1000, 64), 1024);
    EXPECT_EQ(zrt::AlignUp(1024, 64), 1024);
    EXPECT_EQ(zrt::AlignUp(1025, 64), 1088);
}
