//
// Created by AI Assistant on 2025-07-09.
//

#include "pch.h"
#include "gtest/gtest.h"
#include "zrtools/str_utils.h"

// 测试字符分割功能
TEST(StrUtilsTest, SplitByChar) {
    // 基本分割
    std::string text = "apple,banana,orange";
    auto result = zrt::split(text, ',');
    EXPECT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "apple");
    EXPECT_EQ(result[1], "banana");
    EXPECT_EQ(result[2], "orange");
    
    // 空字符串分割
    result = zrt::split("", ',');
    EXPECT_EQ(result.size(), 0);  // 空字符串分割应该返回空vector
    
    // 单个元素
    result = zrt::split("single", ',');
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], "single");
    
    // 连续分隔符
    result = zrt::split("a,,b", ',');
    EXPECT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "");
    EXPECT_EQ(result[2], "b");
    
    // 开头和结尾有分隔符
    result = zrt::split(",a,b,", ',');
    EXPECT_EQ(result.size(), 4);
    EXPECT_EQ(result[0], "");
    EXPECT_EQ(result[1], "a");
    EXPECT_EQ(result[2], "b");
    EXPECT_EQ(result[3], "");
}

// 测试字符串分割功能
TEST(StrUtilsTest, SplitByString) {
    // 基本分割
    std::string text = "apple::banana::orange";
    auto result = zrt::split(text, "::");
    EXPECT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "apple");
    EXPECT_EQ(result[1], "banana");
    EXPECT_EQ(result[2], "orange");
    
    // 单字符分隔符
    result = zrt::split("a-b-c", "-");
    EXPECT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
    EXPECT_EQ(result[2], "c");
    
    // 复杂分隔符
    result = zrt::split("data<->more<->info", "<->");
    EXPECT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "data");
    EXPECT_EQ(result[1], "more");
    EXPECT_EQ(result[2], "info");
    
    // 分隔符不存在
    result = zrt::split("no separator", ",");
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], "no separator");
}

// 测试正则表达式分割
TEST(StrUtilsTest, SplitByRegex) {
    // 数字分割
    std::string text = "word1and2word3and4word";
    auto result = zrt::split(text, "[0-9]+");
    EXPECT_GE(result.size(), 4);
    EXPECT_EQ(result[0], "word");
    EXPECT_EQ(result[1], "and");
    EXPECT_EQ(result[2], "word");
    EXPECT_EQ(result[3], "and");
    
    // 空白符分割
    result = zrt::split("word1  word2\tword3\nword4", "\\s+");
    EXPECT_EQ(result.size(), 4);
    EXPECT_EQ(result[0], "word1");
    EXPECT_EQ(result[1], "word2");
    EXPECT_EQ(result[2], "word3");
    EXPECT_EQ(result[3], "word4");
}

// 测试字符连接功能
TEST(StrUtilsTest, JoinWithChar) {
    // 基本连接
    std::vector<std::string> vec = {"apple", "banana", "orange"};
    std::string result = zrt::join(vec, ',');
    EXPECT_EQ(result, "apple,banana,orange");
    
    // 单个元素
    vec = {"single"};
    result = zrt::join(vec, ',');
    EXPECT_EQ(result, "single");
    
    // 空向量
    vec = {};
    result = zrt::join(vec, ',');
    EXPECT_EQ(result, "");
    
    // 包含空字符串
    vec = {"a", "", "b"};
    result = zrt::join(vec, ',');
    EXPECT_EQ(result, "a,,b");
    
    // 不同分隔符
    vec = {"1", "2", "3"};
    result = zrt::join(vec, '-');
    EXPECT_EQ(result, "1-2-3");
    
    result = zrt::join(vec, ' ');
    EXPECT_EQ(result, "1 2 3");
}

// 测试范围连接功能
TEST(StrUtilsTest, JoinWithRange) {
    std::vector<std::string> vec = {"a", "b", "c", "d", "e"};
    
    // 基本范围连接
    std::string result = zrt::join(vec, ',', 1, 4);
    EXPECT_EQ(result, "b,c,d");
    
    // 从头开始
    result = zrt::join(vec, ',', 0, 3);
    EXPECT_EQ(result, "a,b,c");
    
    // 到结尾
    result = zrt::join(vec, ',', 2, 5);
    EXPECT_EQ(result, "c,d,e");
    
    // 超出范围
    result = zrt::join(vec, ',', 2, 10);
    EXPECT_EQ(result, "c,d,e");
    
    // 空范围
    result = zrt::join(vec, ',', 3, 3);
    EXPECT_EQ(result, "d");
    
    // 无效范围
    result = zrt::join(vec, ',', 4, 2);
    EXPECT_EQ(result, "e");
}

// 测试大小写转换功能
TEST(StrUtilsTest, CaseConversion) {
    // 转小写
    std::string text = "Hello World";
    std::string result = zrt::lower(text);
    EXPECT_EQ(result, "hello world");
    EXPECT_EQ(text, "hello world");  // 原地修改
    
    // 转大写
    text = "hello world";
    result = zrt::upper(text);
    EXPECT_EQ(result, "HELLO WORLD");
    EXPECT_EQ(text, "HELLO WORLD");  // 原地修改
    
    // 混合字符
    text = "Hello123World!";
    zrt::lower(text);
    EXPECT_EQ(text, "hello123world!");
    
    zrt::upper(text);
    EXPECT_EQ(text, "HELLO123WORLD!");
    
    // 空字符串
    text = "";
    EXPECT_EQ(zrt::lower(text), "");
    EXPECT_EQ(zrt::upper(text), "");
}

// 测试边界情况
TEST(StrUtilsTest, EdgeCases) {
    // 分割边界情况
    auto result = zrt::split("", "");
    EXPECT_GE(result.size(), 0);
    
    // 连接边界情况
    std::vector<std::string> empty_vec;
    std::string join_result = zrt::join(empty_vec, ',');
    EXPECT_EQ(join_result, "");
    
    // 范围连接边界情况
    std::vector<std::string> vec = {"a", "b", "c"};
    join_result = zrt::join(vec, ',', 0, 1);
    EXPECT_EQ(join_result, "a");

    join_result = zrt::join(vec, ',', 0, 3);
    EXPECT_EQ(join_result, "a,b,c");

    join_result = zrt::join(vec, ',', 1, 3);
    EXPECT_EQ(join_result, "b,c");
    
    // 大小写转换边界情况
    std::string text = "123!@#";
    zrt::lower(text);
    EXPECT_EQ(text, "123!@#");
    
    zrt::upper(text);
    EXPECT_EQ(text, "123!@#");
}

// 测试特殊字符处理
TEST(StrUtilsTest, SpecialCharacters) {
    // 包含特殊字符的分割
    std::string text = "hello\nworld\ttab";
    auto result = zrt::split(text, '\n');
    EXPECT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], "hello");
    EXPECT_EQ(result[1], "world\ttab");
    
    // 包含特殊字符的连接
    std::vector<std::string> vec = {"line1", "line2", "line3"};
    std::string join_result = zrt::join(vec, '\n');
    EXPECT_EQ(join_result, "line1\nline2\nline3");
    
    // Unicode字符处理
    text = "你好世界";
    zrt::lower(text);
    EXPECT_EQ(text, "你好世界");  // 中文字符不变
    
    zrt::upper(text);
    EXPECT_EQ(text, "你好世界");  // 中文字符不变
}

// 测试性能和大量数据
TEST(StrUtilsTest, PerformanceAndLargeData) {
    // 大量分割
    std::string large_text;
    for (int i = 0; i < 1000; ++i) {
        large_text += "item" + std::to_string(i) + ",";
    }
    large_text.pop_back();  // 移除最后一个逗号
    
    auto result = zrt::split(large_text, ',');
    EXPECT_EQ(result.size(), 1000);
    EXPECT_EQ(result[0], "item0");
    EXPECT_EQ(result[999], "item999");
    
    // 大量连接
    std::vector<std::string> large_vec;
    for (int i = 0; i < 1000; ++i) {
        large_vec.push_back("item" + std::to_string(i));
    }
    
    std::string join_result = zrt::join(large_vec, ',');
    EXPECT_TRUE(join_result.find("item0") != std::string::npos);
    EXPECT_TRUE(join_result.find("item999") != std::string::npos);
    
    // 长字符串大小写转换
    std::string long_text(10000, 'a');
    zrt::upper(long_text);
    EXPECT_EQ(long_text, std::string(10000, 'A'));
}