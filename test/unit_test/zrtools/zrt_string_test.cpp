//
// Created by AI Assistant on 2025-07-09.
//

#include "pch.h"
#include "gtest/gtest.h"
#include "zrtools/zrt_string.h"
#include <climits>
#include <cfloat>

// 测试类型转换功能
TEST(ZrtStringTest, ConvertBasicTypes) {
    // 字符串到整数
    EXPECT_EQ(zrt::convert<int>("123"), 123);
    EXPECT_EQ(zrt::convert<int>("-456"), -456);
    EXPECT_EQ(zrt::convert<int>("0"), 0);
    
    // 字符串到浮点数
    EXPECT_DOUBLE_EQ(zrt::convert<double>("3.14159"), 3.14159);
    EXPECT_DOUBLE_EQ(zrt::convert<double>("-2.71828"), -2.71828);
    
    // 整数到字符串
    EXPECT_EQ(zrt::convert<std::string>(42), "42");
    EXPECT_EQ(zrt::convert<std::string>(-100), "-100");
}

// 测试转换错误处理
TEST(ZrtStringTest, ConvertErrorHandling) {
    // 无效字符串转换应返回默认值
    EXPECT_EQ(zrt::convert<int>("abc"), 0);
    EXPECT_EQ(zrt::convert<int>("abc", 999), 999);
    
    // 空字符串转换
    EXPECT_EQ(zrt::convert<int>(""), 0);
    EXPECT_DOUBLE_EQ(zrt::convert<double>(""), 0.0);
    
    // 溢出处理
    EXPECT_EQ(zrt::convert<int>("999999999999999999"), 0);
}

// 测试边界值转换
TEST(ZrtStringTest, ConvertBoundaryValues) {
    // 整数边界值
    EXPECT_EQ(zrt::convert<int>(std::to_string(INT_MAX)), INT_MAX);
    EXPECT_EQ(zrt::convert<int>(std::to_string(INT_MIN)), INT_MIN);
    
    // 浮点数边界值
    EXPECT_DOUBLE_EQ(zrt::convert<double>("1.7976931348623157e+308"), DBL_MAX);
    EXPECT_DOUBLE_EQ(zrt::convert<double>("2.2250738585072014e-308"), DBL_MIN);
}

// 测试convert2str功能
TEST(ZrtStringTest, Convert2Str) {
    // 基本类型转字符串
    EXPECT_EQ(zrt::convert2str(42), "42");
    EXPECT_EQ(zrt::convert2str(-100), "-100");
    EXPECT_EQ(zrt::convert2str(0), "0");
    
    // 浮点数转字符串带精度
    EXPECT_EQ(zrt::convert2str(3.14159, 2), "3.14");
    EXPECT_EQ(zrt::convert2str(3.14159, 4), "3.1416");
    EXPECT_EQ(zrt::convert2str(-3.14159, 3), "-3.142");
    
    // 处理-0.0的情况
    EXPECT_EQ(zrt::convert2str(-0.0, 2), "0.00");
}

// 测试字符串验证功能
TEST(ZrtStringTest, StringValidation) {
    // 可转换为整数测试
    EXPECT_TRUE(zrt::convertible2int("123"));
    EXPECT_TRUE(zrt::convertible2int("-456"));
    EXPECT_TRUE(zrt::convertible2int("0"));

    // 注意：std::stoi 支持部分转换，"12.34" -> 12, "123abc" -> 123
    EXPECT_FALSE(zrt::convertible2int("abc"));
    EXPECT_TRUE(zrt::convertible2int("12.34"));   // std::stoi 返回 12
    EXPECT_FALSE(zrt::convertible2int(""));
    EXPECT_TRUE(zrt::convertible2int("123abc"));  // std::stoi 返回 123
    
    // 批量验证
    std::vector<std::string> valid_ints = {"1", "22", "333", "-4444"};
    EXPECT_TRUE(zrt::all_convertible2int(valid_ints));
    
    std::vector<std::string> mixed = {"1", "22", "abc", "333"};
    EXPECT_FALSE(zrt::all_convertible2int(mixed));
    
    std::vector<std::string> empty_vec;
    EXPECT_TRUE(zrt::all_convertible2int(empty_vec));
}

// 测试字符串大小写转换
TEST(ZrtStringTest, CaseConversion) {
    // 转小写
    EXPECT_EQ(zrt::to_lower("HELLO"), "hello");
    EXPECT_EQ(zrt::to_lower("Hello World"), "hello world");
    EXPECT_EQ(zrt::to_lower("123ABC"), "123abc");
    EXPECT_EQ(zrt::to_lower(""), "");
    
    // 转大写
    EXPECT_EQ(zrt::to_upper("hello"), "HELLO");
    EXPECT_EQ(zrt::to_upper("Hello World"), "HELLO WORLD");
    EXPECT_EQ(zrt::to_upper("123abc"), "123ABC");
    EXPECT_EQ(zrt::to_upper(""), "");
}

// 测试字符串包含功能
TEST(ZrtStringTest, StringContains) {
    std::string text = "Hello World Test";
    
    EXPECT_TRUE(zrt::contain(text, "Hello"));
    EXPECT_TRUE(zrt::contain(text, "World"));
    EXPECT_TRUE(zrt::contain(text, "Test"));
    EXPECT_TRUE(zrt::contain(text, ""));
    
    EXPECT_FALSE(zrt::contain(text, "hello"));  // 区分大小写
    EXPECT_FALSE(zrt::contain(text, "xyz"));
    EXPECT_FALSE(zrt::contain(text, "Testing"));
    
    // 边界情况
    EXPECT_TRUE(zrt::contain("", ""));
    EXPECT_FALSE(zrt::contain("", "abc"));
    EXPECT_TRUE(zrt::contain("abc", ""));
}

// 测试特殊字符处理
TEST(ZrtStringTest, SpecialCharacters) {
    // 包含特殊字符的转换
    EXPECT_EQ(zrt::convert<std::string>(42), "42");
    
    // 大小写转换中的特殊字符
    EXPECT_EQ(zrt::to_lower("Hello\nWorld\t!"), "hello\nworld\t!");
    EXPECT_EQ(zrt::to_upper("Hello\nWorld\t!"), "HELLO\nWORLD\t!");
    
    // 包含中文字符
    std::string chinese = "你好世界";
    EXPECT_EQ(zrt::to_lower(chinese), chinese);  // 中文不变
    EXPECT_EQ(zrt::to_upper(chinese), chinese);  // 中文不变
}

// 测试精度转换
TEST(ZrtStringTest, PrecisionConversion) {
    // 不同精度的浮点数转换
    EXPECT_EQ(zrt::convert2str(1.0/3.0, 0), "0");
    EXPECT_EQ(zrt::convert2str(1.0/3.0, 1), "0.3");
    EXPECT_EQ(zrt::convert2str(1.0/3.0, 5), "0.33333");
    
    // 大数的精度
    EXPECT_EQ(zrt::convert2str(1234567.89, 2), "1234567.89");
    EXPECT_EQ(zrt::convert2str(1234567.89, 0), "1234568");
    
    // 小数的精度
    EXPECT_EQ(zrt::convert2str(0.00123, 5), "0.00123");
    EXPECT_EQ(zrt::convert2str(0.00123, 2), "0.00");
}

// 测试性能和大量数据
TEST(ZrtStringTest, PerformanceAndLargeData) {
    // 大量字符串转换
    for (int i = 0; i < 1000; ++i) {
        std::string str = std::to_string(i);
        EXPECT_EQ(zrt::convert<int>(str), i);
    }
    
    // 长字符串处理
    std::string long_str(10000, 'a');
    EXPECT_EQ(zrt::to_upper(long_str), std::string(10000, 'A'));
    
    // 大量包含检查
    std::string text = "This is a long text for testing contains functionality";
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(zrt::contain(text, "long"));
        EXPECT_FALSE(zrt::contain(text, "missing"));
    }
}