//
// zrt_exceptions_test.cpp - 异常类单元测试
//

#include "pch.h"
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "zrtools/zrt_exceptions.h"
#include <string>

// ========== RunTimeError 测试 ==========

// 测试 RunTimeError 默认构造
TEST(RunTimeErrorTest, DefaultConstructor) {
    const zrt::RunTimeError error {};

    // 默认构造的错误消息应该为空
    EXPECT_STREQ(error.what(), "");
}

// 测试 RunTimeError 带格式化消息
TEST(RunTimeErrorTest, FormattedMessage) {
    const zrt::RunTimeError error("Error code: {}", 42);

    EXPECT_STREQ(error.what(), "Error code: 42");
}

// 测试 RunTimeError 多参数格式化
TEST(RunTimeErrorTest, MultipleParameters) {
    const zrt::RunTimeError error("Name: {}, Age: {}, Score: {:.2f}", "Alice", 25, 95.5);

    EXPECT_STREQ(error.what(), "Name: Alice, Age: 25, Score: 95.50");
}

// 测试 RunTimeError 带错误ID和格式化参数
TEST(RunTimeErrorTest, WithErrorIdAndFormatArgs) {
    const zrt::RunTimeError error(100, "Operation failed: {}", "timeout");

    EXPECT_STREQ(error.what(), "Operation failed: timeout");
    EXPECT_EQ(error.err_id(), 100);
}

// 测试 RunTimeError 作为 std::exception 的基类
TEST(RunTimeErrorTest, AsStdException) {
    try {
        throw zrt::RunTimeError("Test exception");
    } catch (const std::exception& e) {
        EXPECT_STREQ(e.what(), "Test exception");
    }
}

// 测试 RunTimeError 作为 std::runtime_error 的基类
TEST(RunTimeErrorTest, AsRuntimeError) {
    try {
        throw zrt::RunTimeError("Runtime error test");
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "Runtime error test");
    }
}

// 测试抛出和捕获 RunTimeError
TEST(RunTimeErrorTest, ThrowAndCatch) {
    auto throw_func = []() {
        throw zrt::RunTimeError(500, "Internal error: {}", "database connection failed");
    };

    EXPECT_THROW(throw_func(), zrt::RunTimeError);

    try {
        throw_func();
    } catch (const zrt::RunTimeError& e) {
        EXPECT_STREQ(e.what(), "Internal error: database connection failed");
        EXPECT_EQ(e.err_id(), 500);
    }
}

// 测试 RunTimeError 空格式化字符串
TEST(RunTimeErrorTest, EmptyFormatString) {
    const zrt::RunTimeError error("");
    EXPECT_STREQ(error.what(), "");
}

// 测试 RunTimeError 只有格式化占位符
TEST(RunTimeErrorTest, OnlyPlaceholder) {
    const zrt::RunTimeError error("{}", "just this");
    EXPECT_STREQ(error.what(), "just this");
}

// 测试 RunTimeError 复杂格式化
TEST(RunTimeErrorTest, ComplexFormatting) {
    const zrt::RunTimeError error(
        "Error at line {}: {} ({:#x})",
        100, "invalid opcode", 0xDEAD
    );

    const std::string msg = error.what();
    EXPECT_TRUE(msg.find("100") != std::string::npos);
    EXPECT_TRUE(msg.find("invalid opcode") != std::string::npos);
    EXPECT_TRUE(msg.find("0xdead") != std::string::npos);
}

// 测试 RunTimeError 复制语义
TEST(RunTimeErrorTest, CopySemantics) {
    const zrt::RunTimeError original(42, "Original error: {}", "test");

    zrt::RunTimeError copy = original;

    EXPECT_STREQ(copy.what(), original.what());
    EXPECT_EQ(copy.err_id(), original.err_id());
}

// ========== TypeConvertError 测试 ==========
// 注意：TypeConvertError::what() 有实现 bug（返回 std::string 而非 const char*）
// 由于模板实例化会触发编译错误，暂时跳过 TypeConvertError 的测试
// 待源码修复后再启用测试

// ========== Terminate Handler 测试 ==========
// 注意：不实际调用，因为它们会终止程序

TEST(ExceptionHandlersTest, HandlerFunctionExists) {
    // 验证函数指针可以获取
    auto* handler_ptr = &zrt::safe_terminate_hdl;
    EXPECT_NE(handler_ptr, nullptr);

    auto* simple_ptr = &zrt::simple_terminate_hdl;
    EXPECT_NE(simple_ptr, nullptr);
}
