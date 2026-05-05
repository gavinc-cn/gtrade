//
// Created by AI Assistant on 2025-07-09.
//

#include "pch.h"
#include "gtest/gtest.h"
#include "zrtools/zrt_assert.h"
#include "zrtools/zrt_exceptions.h"
#include <sstream>

// 测试基本断言宏
TEST(ZrtAssertTest, BasicAssertions) {
    // 成功的断言不应该抛出异常
    EXPECT_NO_THROW(g_assert(true, "This should not throw"));
    EXPECT_NO_THROW(g_assert(1 == 1, "1 equals 1"));
    EXPECT_NO_THROW(g_assert(5 > 3, "5 is greater than 3"));
    
    // 失败的断言应该抛出异常
    EXPECT_THROW(g_assert(false, "This should throw"), zrt::RunTimeError);
    EXPECT_THROW(g_assert(1 == 2, "1 does not equal 2"), zrt::RunTimeError);
    EXPECT_THROW(g_assert(3 > 5, "3 is not greater than 5"), zrt::RunTimeError);
}

// 测试等值断言
TEST(ZrtAssertTest, EqualityAssertions) {
    // 成功的等值断言
    EXPECT_NO_THROW(assert_eq(42, 42));
    EXPECT_NO_THROW(assert_eq(0, 0));
    EXPECT_NO_THROW(assert_eq(-5, -5));
    EXPECT_NO_THROW(assert_eq(3.14, 3.14));
    
    // 失败的等值断言
    EXPECT_THROW(assert_eq(1, 2), zrt::RunTimeError);
    EXPECT_THROW(assert_eq(0, 1), zrt::RunTimeError);
    EXPECT_THROW(assert_eq(3.14, 2.71), zrt::RunTimeError);
    
    // 测试不同类型的比较
    EXPECT_NO_THROW(assert_eq(10, 10.0));
    EXPECT_THROW(assert_eq(10, 10.1), zrt::RunTimeError);
}

// 测试字符串等值断言
TEST(ZrtAssertTest, StringEqualityAssertions) {
    // 成功的字符串等值断言
    EXPECT_NO_THROW(assert_str_eq("hello", "hello"));
    EXPECT_NO_THROW(assert_str_eq("", ""));
    EXPECT_NO_THROW(assert_str_eq("test", std::string("test")));
    
    // 失败的字符串等值断言
    EXPECT_THROW(assert_str_eq("hello", "world"), zrt::RunTimeError);
    EXPECT_THROW(assert_str_eq("", "not empty"), zrt::RunTimeError);
    EXPECT_THROW(assert_str_eq("case", "CASE"), zrt::RunTimeError);
    
    // 测试空字符串
    const char* null_ptr = nullptr;
    // 注意：这里需要小心处理nullptr，可能需要特殊处理
    EXPECT_NO_THROW(assert_str_eq("", ""));
}

// 测试大于断言
TEST(ZrtAssertTest, GreaterThanAssertions) {
    // 成功的大于断言
    EXPECT_NO_THROW(assert_gt(5, 3));
    EXPECT_NO_THROW(assert_gt(0, -1));
    EXPECT_NO_THROW(assert_gt(3.14, 2.71));
    EXPECT_NO_THROW(assert_gt(100, 99));
    
    // 失败的大于断言
    EXPECT_THROW(assert_gt(3, 5), zrt::RunTimeError);
    EXPECT_THROW(assert_gt(0, 0), zrt::RunTimeError);  // 等于不满足大于
    EXPECT_THROW(assert_gt(-1, 0), zrt::RunTimeError);
    EXPECT_THROW(assert_gt(2.71, 3.14), zrt::RunTimeError);
}

// 测试小于断言
TEST(ZrtAssertTest, LessThanAssertions) {
    // 成功的小于断言
    EXPECT_NO_THROW(assert_lt(3, 5));
    EXPECT_NO_THROW(assert_lt(-1, 0));
    EXPECT_NO_THROW(assert_lt(2.71, 3.14));
    EXPECT_NO_THROW(assert_lt(99, 100));
    
    // 失败的小于断言
    EXPECT_THROW(assert_lt(5, 3), zrt::RunTimeError);
    EXPECT_THROW(assert_lt(0, 0), zrt::RunTimeError);  // 等于不满足小于
    EXPECT_THROW(assert_lt(0, -1), zrt::RunTimeError);
    EXPECT_THROW(assert_lt(3.14, 2.71), zrt::RunTimeError);
}

// 测试大于等于断言
TEST(ZrtAssertTest, GreaterEqualAssertions) {
    // 成功的大于等于断言
    EXPECT_NO_THROW(assert_ge(5, 3));
    EXPECT_NO_THROW(assert_ge(5, 5));  // 等于满足大于等于
    EXPECT_NO_THROW(assert_ge(0, 0));
    EXPECT_NO_THROW(assert_ge(3.14, 3.14));
    EXPECT_NO_THROW(assert_ge(100, 99));
    
    // 失败的大于等于断言
    EXPECT_THROW(assert_ge(3, 5), zrt::RunTimeError);
    EXPECT_THROW(assert_ge(-1, 0), zrt::RunTimeError);
    EXPECT_THROW(assert_ge(2.71, 3.14), zrt::RunTimeError);
}

// 测试复杂表达式断言
TEST(ZrtAssertTest, ComplexExpressionAssertions) {
    int a = 10, b = 20, c = 30;
    
    // 成功的复杂表达式断言
    EXPECT_NO_THROW(g_assert(a + b == c, "a + b should equal c"));
    EXPECT_NO_THROW(g_assert(a < b && b < c, "a < b < c"));
    EXPECT_NO_THROW(g_assert(c - b == a, "c - b should equal a"));
    
    // 失败的复杂表达式断言
    EXPECT_THROW(g_assert(a + b > c, "a + b should not be greater than c"), zrt::RunTimeError);
    EXPECT_THROW(g_assert(a > b || b > c, "Neither a > b nor b > c"), zrt::RunTimeError);
}

// 测试异常消息内容
TEST(ZrtAssertTest, ExceptionMessages) {
    try {
        g_assert(false, "Custom error message");
        FAIL() << "Expected exception was not thrown";
    } catch (const zrt::RunTimeError& e) {
        std::string message = e.what();
        EXPECT_TRUE(message.find("false") != std::string::npos);
        EXPECT_TRUE(message.find("Custom error message") != std::string::npos);
    }
    
    try {
        assert_eq(1, 2);
        FAIL() << "Expected exception was not thrown";
    } catch (const zrt::RunTimeError& e) {
        std::string message = e.what();
        EXPECT_TRUE(message.find("1 == 2") != std::string::npos);
    }
}

// 测试边界条件
TEST(ZrtAssertTest, BoundaryConditions) {
    // 测试极值
    EXPECT_NO_THROW(assert_eq(INT_MAX, INT_MAX));
    EXPECT_NO_THROW(assert_eq(INT_MIN, INT_MIN));
    EXPECT_THROW(assert_eq(INT_MAX, INT_MIN), zrt::RunTimeError);
    
    // 测试浮点数边界
    EXPECT_NO_THROW(assert_eq(0.0, 0.0));
    EXPECT_NO_THROW(assert_eq(-0.0, 0.0));
    
    // 测试很接近但不相等的浮点数
    EXPECT_THROW(assert_eq(1.0, 1.0000001), zrt::RunTimeError);
}

// 测试不同数据类型
TEST(ZrtAssertTest, DifferentDataTypes) {
    // 整数类型
    short s = 100;
    int i = 100;
    long l = 100;
    EXPECT_NO_THROW(assert_eq(s, i));
    EXPECT_NO_THROW(assert_eq(i, l));
    
    // 浮点类型（使用精确可表示的值，避免 float/double 精度差异）
    float f = 2.5f;
    double d = 2.5;
    EXPECT_NO_THROW(assert_eq(f, d));
    
    // 字符类型
    char c1 = 'A';
    char c2 = 'A';
    EXPECT_NO_THROW(assert_eq(c1, c2));
    
    // 布尔类型
    bool b1 = true;
    bool b2 = true;
    EXPECT_NO_THROW(assert_eq(b1, b2));
    EXPECT_THROW(assert_eq(true, false), zrt::RunTimeError);
}

// 测试性能（确保断言不会显著影响性能）
TEST(ZrtAssertTest, Performance) {
    // 成功的断言应该很快
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 10000; ++i) {
        g_assert(i >= 0, "i should be non-negative");
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // 断言10000次不应该超过1毫秒（这是一个合理的预期）
    EXPECT_LT(duration.count(), 1000);
}

// 测试嵌套断言
TEST(ZrtAssertTest, NestedAssertions) {
    // 多层嵌套的断言
    EXPECT_NO_THROW({
        g_assert(true, "Level 1");
        g_assert(1 == 1, "Level 2");
        assert_eq(2, 2);
        assert_gt(3, 2);
        assert_lt(1, 2);
        assert_ge(2, 2);
    });
    
    // 其中一个失败的嵌套断言
    EXPECT_THROW({
        g_assert(true, "This passes");
        assert_eq(1, 1);  // This passes
        assert_eq(1, 2);  // This fails
        g_assert(false, "This won't be reached");
    }, zrt::RunTimeError);
}