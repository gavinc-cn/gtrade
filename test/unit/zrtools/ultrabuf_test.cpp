//
// Created by AI Assistant on 2025-07-09.
//

#include "pch.h"
#include "gtest/gtest.h"
#include "zrtools/ultrabuf.h"
#include <cstring>
#include <vector>

class UltraBufTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 每个测试用例开始前的设置
    }
    
    void TearDown() override {
        // 每个测试用例结束后的清理
    }
};

// 测试UltraBuf基本功能
TEST_F(UltraBufTest, BasicOperations) {
    zrt::UltraBuf buffer;
    
    // 初始状态
    EXPECT_TRUE(buffer.is_empty());
    EXPECT_EQ(buffer.get_size(), 0);
    EXPECT_GT(buffer.get_capacity(), 0);
    
    // 写入数据
    std::string test_data = "Hello, World!";
    int64_t written = buffer.push(test_data.c_str(), test_data.size());
    EXPECT_EQ(written, test_data.size());
    EXPECT_FALSE(buffer.is_empty());
    EXPECT_EQ(buffer.get_size(), test_data.size());
    
    // 读取数据
    char read_buffer[100];
    int64_t read_size = buffer.read(read_buffer, test_data.size());
    EXPECT_EQ(read_size, test_data.size());
    EXPECT_EQ(std::string(read_buffer, read_size), test_data);
    
    // 数据仍在缓冲区中
    EXPECT_FALSE(buffer.is_empty());
    EXPECT_EQ(buffer.get_size(), test_data.size());
}

// 测试push和pull操作
TEST_F(UltraBufTest, PushPullOperations) {
    zrt::UltraBuf buffer;
    
    // 写入数据
    std::string test_data = "Test data for push/pull";
    int64_t written = buffer.push(test_data.c_str(), test_data.size());
    EXPECT_EQ(written, test_data.size());
    
    // 拉取数据（会移除数据）
    char pull_buffer[100];
    int64_t pulled = buffer.pull(pull_buffer, test_data.size());
    EXPECT_EQ(pulled, test_data.size());
    EXPECT_EQ(std::string(pull_buffer, pulled), test_data);
    
    // 数据应该被移除
    EXPECT_TRUE(buffer.is_empty());
    EXPECT_EQ(buffer.get_size(), 0);
}

// 测试部分读取和拉取
TEST_F(UltraBufTest, PartialReadPull) {
    zrt::UltraBuf buffer;
    
    std::string test_data = "1234567890";
    buffer.push(test_data.c_str(), test_data.size());
    
    // 部分读取
    char read_buffer[5];
    int64_t read_size = buffer.read(read_buffer, 5);
    EXPECT_EQ(read_size, 5);
    EXPECT_EQ(std::string(read_buffer, read_size), "12345");
    
    // 数据仍在缓冲区中
    EXPECT_EQ(buffer.get_size(), 10);
    
    // 部分拉取
    char pull_buffer[3];
    int64_t pulled = buffer.pull(pull_buffer, 3);
    EXPECT_EQ(pulled, 3);
    EXPECT_EQ(std::string(pull_buffer, pulled), "123");
    
    // 剩余数据
    EXPECT_EQ(buffer.get_size(), 7);
    
    // 读取剩余数据
    char remaining_buffer[10];
    int64_t remaining = buffer.read(remaining_buffer, 10);
    EXPECT_EQ(remaining, 7);
    EXPECT_EQ(std::string(remaining_buffer, remaining), "4567890");
}

// 测试大数据处理
TEST_F(UltraBufTest, LargeDataHandling) {
    zrt::UltraBuf buffer;
    
    // 创建大于单个块大小的数据
    const size_t large_size = BLOCK_BUFFER_SIZE + 1000;
    std::vector<char> large_data(large_size);
    
    // 填充测试数据
    for (size_t i = 0; i < large_size; ++i) {
        large_data[i] = 'A' + (i % 26);
    }
    
    // 写入大数据
    int64_t written = buffer.push(large_data.data(), large_size);
    EXPECT_EQ(written, large_size);
    EXPECT_EQ(buffer.get_size(), large_size);
    EXPECT_GT(buffer.cnt_block_used(), 1);
    
    // 读取大数据
    std::vector<char> read_data(large_size);
    int64_t read_size = buffer.read(read_data.data(), large_size);
    EXPECT_EQ(read_size, large_size);
    EXPECT_EQ(read_data, large_data);
}

// 测试多次写入和读取
TEST_F(UltraBufTest, MultipleOperations) {
    zrt::UltraBuf buffer;
    
    std::vector<std::string> test_strings = {
        "First", "Second", "Third", "Fourth", "Fifth"
    };
    
    size_t total_size = 0;
    
    // 多次写入
    for (const auto& str : test_strings) {
        int64_t written = buffer.push(str.c_str(), str.size());
        EXPECT_EQ(written, str.size());
        total_size += str.size();
    }
    
    EXPECT_EQ(buffer.get_size(), total_size);
    
    // 逐个读取
    for (const auto& str : test_strings) {
        char read_buffer[100];
        int64_t read_size = buffer.pull(read_buffer, str.size());
        EXPECT_EQ(read_size, str.size());
        EXPECT_EQ(std::string(read_buffer, read_size), str);
        total_size -= str.size();
    }
    
    EXPECT_TRUE(buffer.is_empty());
    EXPECT_EQ(buffer.get_size(), 0);
}

// 测试缓冲区容量限制
TEST_F(UltraBufTest, CapacityLimits) {
    // 创建有限制的缓冲区
    zrt::UltraBuf buffer(2);  // 最多2个块

    // 构造函数会预分配一个块
    EXPECT_EQ(buffer.cnt_block_total(), 1);
    EXPECT_EQ(buffer.cnt_block_used(), 1);

    // 写入数据填满一个块（push会预分配下一个块）
    const std::vector<char> data(BLOCK_BUFFER_SIZE, 'X');
    int64_t written = buffer.push(data.data(), data.size());
    EXPECT_EQ(written, data.size());
    // 写入一个完整块后，会预分配下一个块，所以有2个块
    EXPECT_EQ(buffer.cnt_block_used(), 2);

    // 尝试再写入一个完整块应该失败（已达到最大块数且当前块会溢出）
    written = buffer.push(data.data(), data.size());
    EXPECT_EQ(written, EAGAIN);  // 超出容量限制

    // 当前块仍有空间，所以is_full(1)应该返回false
    // is_full检查的是：是否达到最大块数 AND 当前块是否会溢出
    EXPECT_FALSE(buffer.is_full(1));
    EXPECT_TRUE(buffer.is_full(BLOCK_BUFFER_SIZE));
    EXPECT_FALSE(buffer.is_full(BLOCK_BUFFER_SIZE - 1));
}

// 测试边界条件
TEST_F(UltraBufTest, BoundaryConditions) {
    zrt::UltraBuf buffer;
    
    // 写入0字节
    int64_t written = buffer.push(nullptr, 0);
    EXPECT_EQ(written, 0);
    EXPECT_TRUE(buffer.is_empty());
    
    // 读取0字节
    char dummy;
    int64_t read_size = buffer.read(&dummy, 0);
    EXPECT_EQ(read_size, 0);
    
    // 从空缓冲区读取
    read_size = buffer.read(&dummy, 1);
    EXPECT_EQ(read_size, 0);
    
    // 从空缓冲区拉取
    int64_t pulled = buffer.pull(&dummy, 1);
    EXPECT_EQ(pulled, 0);
}

// 测试shrink_to_fit功能
TEST_F(UltraBufTest, ShrinkToFit) {
    zrt::UltraBuf buffer;

    // 写入大量数据（跨越多个块）
    std::vector<char> data(BLOCK_BUFFER_SIZE * 3, 'Y');
    buffer.push(data.data(), data.size());

    const size_t blocks_total_before = buffer.cnt_block_total();
    EXPECT_GT(blocks_total_before, 1);

    // 拉取数据，使部分块变为空块（移入m_empty_head列表）
    std::vector<char> read_data(BLOCK_BUFFER_SIZE * 2);
    buffer.pull(read_data.data(), read_data.size());

    // pull后，部分块被移入空块列表
    size_t blocks_total_after_pull = buffer.cnt_block_total();
    size_t blocks_used_after_pull = buffer.cnt_block_used();
    size_t empty_blocks = blocks_total_after_pull - blocks_used_after_pull;
    EXPECT_GT(empty_blocks, 0);  // 应该有空块

    // 压缩缓冲区（释放空块列表中的块）
    buffer.shrink_to_fit();

    // 检查总块数是否减少（空块被释放）
    EXPECT_LT(buffer.cnt_block_total(), blocks_total_before);
    EXPECT_LT(buffer.cnt_block_total(), blocks_total_after_pull);
    // 使用中的块数不变
    EXPECT_EQ(buffer.cnt_block_used(), blocks_used_after_pull);
}

// 测试copy_content功能
TEST_F(UltraBufTest, CopyContent) {
    zrt::UltraBuf buffer;
    
    std::string test_data = "Copy content test";
    buffer.push(test_data.c_str(), test_data.size());
    
    // 复制内容
    std::string copied = buffer.copy_content();
    EXPECT_EQ(copied, test_data);
    
    // 原缓冲区内容应该保持不变
    EXPECT_EQ(buffer.get_size(), test_data.size());
    EXPECT_FALSE(buffer.is_empty());
}

// 测试print功能（主要检查不崩溃）
TEST_F(UltraBufTest, PrintFunction) {
    zrt::UltraBuf buffer;
    
    // 空缓冲区打印
    EXPECT_NO_THROW(buffer.print());
    
    // 有数据的缓冲区打印
    std::string test_data = "Print test";
    buffer.push(test_data.c_str(), test_data.size());
    EXPECT_NO_THROW(buffer.print());
}

// 测试错误处理
TEST_F(UltraBufTest, ErrorHandling) {
    zrt::UltraBuf buffer;
    
    // 尝试从空缓冲区读取大量数据
    char large_buffer[1000];
    int64_t read_size = buffer.read(large_buffer, 1000);
    EXPECT_EQ(read_size, 0);
    
    // 尝试从空缓冲区拉取数据
    int64_t pulled = buffer.pull(large_buffer, 1000);
    EXPECT_EQ(pulled, 0);
    
    // 写入数据后尝试读取更多数据
    std::string small_data = "small";
    buffer.push(small_data.c_str(), small_data.size());
    
    read_size = buffer.read(large_buffer, 1000);
    EXPECT_EQ(read_size, small_data.size());
}

// 测试性能和稳定性
TEST_F(UltraBufTest, StabilityTest) {
    zrt::UltraBuf buffer;
    
    // 反复写入和读取
    for (int i = 0; i < 100; ++i) {
        std::string data = "Data " + std::to_string(i);
        buffer.push(data.c_str(), data.size());
        
        char read_buffer[100];
        int64_t read_size = buffer.pull(read_buffer, data.size());
        EXPECT_EQ(read_size, data.size());
        EXPECT_EQ(std::string(read_buffer, read_size), data);
    }
    
    EXPECT_TRUE(buffer.is_empty());
}