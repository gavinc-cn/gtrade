//
// zrt_crc32_test.cpp - CRC32 计算工具单元测试
//

#include "pch.h"
#include "gtest/gtest.h"
#include "zrtools/zrt_crc32.h"
#include <string>
#include <vector>

// 测试空数据的 CRC32
TEST(Crc32Test, EmptyData) {
    // 空数据的 CRC32 应该是 0
    const uint32_t crc = zrt::Crc32::Calc(nullptr, 0);
    EXPECT_EQ(crc, 0);

    const char empty[] = "";
    const uint32_t crc2 = zrt::Crc32::Calc(empty, 0);
    EXPECT_EQ(crc2, 0);
}

// 测试已知值的 CRC32
TEST(Crc32Test, KnownValues) {
    // "123456789" 的标准 CRC32 (IEEE 802.3 多项式) 应该是 0xCBF43926
    const char test_data[] = "123456789";
    const uint32_t crc = zrt::Crc32::Calc(test_data, 9);
    EXPECT_EQ(crc, 0xCBF43926);
}

// 测试单字节数据
TEST(Crc32Test, SingleByte) {
    const char data = 'A';
    const uint32_t crc = zrt::Crc32::Calc(&data, 1);
    // 单字节 'A' (0x41) 的 CRC32 是已知的
    EXPECT_NE(crc, 0);
}

// 测试字符串数据
TEST(Crc32Test, StringData) {
    const std::string str = "Hello, World!";
    const uint32_t crc = zrt::Crc32::Calc(str.data(), str.size());
    EXPECT_NE(crc, 0);

    // 相同数据应该得到相同的 CRC
    const uint32_t crc2 = zrt::Crc32::Calc(str.data(), str.size());
    EXPECT_EQ(crc, crc2);
}

// 测试不同数据产生不同的 CRC32
TEST(Crc32Test, DifferentDataDifferentCrc) {
    const std::string str1 = "Hello";
    const std::string str2 = "World";

    const uint32_t crc1 = zrt::Crc32::Calc(str1.data(), str1.size());
    const uint32_t crc2 = zrt::Crc32::Calc(str2.data(), str2.size());

    EXPECT_NE(crc1, crc2);
}

// 测试增量计算
TEST(Crc32Test, IncrementalUpdate) {
    const std::string part1 = "Hello, ";
    const std::string part2 = "World!";
    const std::string full = "Hello, World!";

    // 一次性计算整个字符串
    const uint32_t crc_full = zrt::Crc32::Calc(full.data(), full.size());

    // 增量计算
    uint32_t crc_inc = zrt::Crc32::Update(0, part1.data(), part1.size());
    crc_inc = zrt::Crc32::Update(crc_inc, part2.data(), part2.size());

    // 两种方式应该得到相同的结果
    EXPECT_EQ(crc_full, crc_inc);
}

// 测试 Verify 方法
TEST(Crc32Test, VerifyMethod) {
    const std::string data = "Test data for verification";
    const uint32_t expected_crc = zrt::Crc32::Calc(data.data(), data.size());

    // 正确的 CRC 应该验证成功
    EXPECT_TRUE(zrt::Crc32::Verify(data.data(), data.size(), expected_crc));

    // 错误的 CRC 应该验证失败
    EXPECT_FALSE(zrt::Crc32::Verify(data.data(), data.size(), expected_crc + 1));
    EXPECT_FALSE(zrt::Crc32::Verify(data.data(), data.size(), 0));
}

// 测试二进制数据
TEST(Crc32Test, BinaryData) {
    const std::vector<uint8_t> binary_data = {0x00, 0x01, 0x02, 0x03, 0xFF, 0xFE, 0xFD};
    const uint32_t crc = zrt::Crc32::Calc(binary_data.data(), binary_data.size());
    EXPECT_NE(crc, 0);

    // 验证
    EXPECT_TRUE(zrt::Crc32::Verify(binary_data.data(), binary_data.size(), crc));
}

// 测试全零数据
TEST(Crc32Test, AllZeroData) {
    const std::vector<uint8_t> zeros(100, 0x00);
    const uint32_t crc = zrt::Crc32::Calc(zeros.data(), zeros.size());

    // 全零数据也应该有有效的 CRC
    EXPECT_NE(crc, 0);

    // 验证
    EXPECT_TRUE(zrt::Crc32::Verify(zeros.data(), zeros.size(), crc));
}

// 测试全 0xFF 数据
TEST(Crc32Test, AllOnesData) {
    const std::vector<uint8_t> ones(100, 0xFF);
    const uint32_t crc = zrt::Crc32::Calc(ones.data(), ones.size());

    EXPECT_NE(crc, 0);
    EXPECT_TRUE(zrt::Crc32::Verify(ones.data(), ones.size(), crc));
}

// 测试大数据块
TEST(Crc32Test, LargeData) {
    const size_t large_size = 1024 * 1024;  // 1MB
    std::vector<uint8_t> large_data(large_size);

    // 填充测试数据
    for (size_t i = 0; i < large_size; ++i) {
        large_data[i] = static_cast<uint8_t>(i & 0xFF);
    }

    const uint32_t crc = zrt::Crc32::Calc(large_data.data(), large_data.size());
    EXPECT_NE(crc, 0);

    // 验证
    EXPECT_TRUE(zrt::Crc32::Verify(large_data.data(), large_data.size(), crc));
}

// 测试增量计算的多次更新
TEST(Crc32Test, MultipleIncrementalUpdates) {
    const std::vector<std::string> parts = {"Part1", "Part2", "Part3", "Part4"};

    // 拼接完整字符串
    std::string full {};
    for (const auto& part : parts) {
        full += part;
    }

    // 一次性计算
    const uint32_t crc_full = zrt::Crc32::Calc(full.data(), full.size());

    // 增量计算
    uint32_t crc_inc = 0;
    for (const auto& part : parts) {
        crc_inc = zrt::Crc32::Update(crc_inc, part.data(), part.size());
    }

    EXPECT_EQ(crc_full, crc_inc);
}

// 测试单次增量更新等于直接计算
TEST(Crc32Test, SingleUpdateEqualsCalc) {
    const std::string data = "Single update test";

    const uint32_t crc_calc = zrt::Crc32::Calc(data.data(), data.size());
    const uint32_t crc_update = zrt::Crc32::Update(0, data.data(), data.size());

    EXPECT_EQ(crc_calc, crc_update);
}

// 测试结构体数据
struct TestStruct {
    int32_t a;
    double b;
    char c[16];
};

TEST(Crc32Test, StructData) {
    TestStruct data {};
    data.a = 12345;
    data.b = 3.14159;
    std::strncpy(data.c, "test", sizeof(data.c));

    const uint32_t crc = zrt::Crc32::Calc(&data, sizeof(data));
    EXPECT_NE(crc, 0);

    // 验证
    EXPECT_TRUE(zrt::Crc32::Verify(&data, sizeof(data), crc));

    // 修改数据后 CRC 应该不同
    data.a = 54321;
    EXPECT_FALSE(zrt::Crc32::Verify(&data, sizeof(data), crc));
}
