//
// Created by Claude Code on 2026/1/29.
//

#include "pch.h"
#include <gtest/gtest.h>
#include <sstream>
#include "zrtools/fmt_helper.h"

class FmtHelperTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
};

// Test safe_format_value with char type
TEST_F(FmtHelperTest, SafeFormatValueChar) {
    // Normal char
    char c = 'A';
    auto result = zrt::safe_format_value(c);
    EXPECT_EQ(result, "A");

    // Null char should become empty string
    char null_c = '\0';
    auto null_result = zrt::safe_format_value(null_c);
    EXPECT_EQ(null_result, "");
}

// Test safe_format_value with other types (perfect forwarding)
TEST_F(FmtHelperTest, SafeFormatValueOtherTypes) {
    // Integer
    int i = 42;
    auto int_result = zrt::safe_format_value(i);
    EXPECT_EQ(int_result, 42);

    // Double
    double d = 3.14;
    auto double_result = zrt::safe_format_value(d);
    EXPECT_DOUBLE_EQ(double_result, 3.14);

    // String
    std::string s = "hello";
    auto str_result = zrt::safe_format_value(s);
    EXPECT_EQ(str_result, "hello");
}

// Test print_fmt function
TEST_F(FmtHelperTest, PrintFmt) {
    // Capture stdout
    std::streambuf* old_cout = std::cout.rdbuf();
    std::stringstream buffer;
    std::cout.rdbuf(buffer.rdbuf());

    // Print formatted string
    zrt::print_fmt("Hello, {}!", "World");

    // Restore stdout
    std::cout.rdbuf(old_cout);

    EXPECT_EQ(buffer.str(), "Hello, World!\n");
}

// Test print_fmt with numbers
TEST_F(FmtHelperTest, PrintFmtNumbers) {
    std::streambuf* old_cout = std::cout.rdbuf();
    std::stringstream buffer;
    std::cout.rdbuf(buffer.rdbuf());

    zrt::print_fmt("Value: {}, Pi: {:.2f}", 42, 3.14159);

    std::cout.rdbuf(old_cout);

    EXPECT_EQ(buffer.str(), "Value: 42, Pi: 3.14\n");
}

// Test safe_fmt with valid format
TEST_F(FmtHelperTest, SafeFmtValid) {
    std::string result = zrt::safe_fmt("Hello, {}!", "World");
    EXPECT_EQ(result, "Hello, World!");

    result = zrt::safe_fmt("Number: {}, Float: {:.2f}", 42, 3.14159);
    EXPECT_EQ(result, "Number: 42, Float: 3.14");
}

// Test safe_fmt with invalid format (should not crash)
TEST_F(FmtHelperTest, SafeFmtInvalid) {
    // Missing argument - should catch exception and return error message
    std::string result = zrt::safe_fmt("Value: {} {}", 42);

    // Should contain error indication
    EXPECT_NE(result.find("error"), std::string::npos);
}

// Test safe_fmt with empty format
TEST_F(FmtHelperTest, SafeFmtEmpty) {
    std::string result = zrt::safe_fmt("");
    EXPECT_EQ(result, "");
}

// Test safe_fmt with no placeholders
TEST_F(FmtHelperTest, SafeFmtNoPlaceholders) {
    std::string result = zrt::safe_fmt("Just a plain string");
    EXPECT_EQ(result, "Just a plain string");
}

// Test safe_fmt with various types
TEST_F(FmtHelperTest, SafeFmtVariousTypes) {
    // Integer
    EXPECT_EQ(zrt::safe_fmt("{}", 123), "123");

    // Negative integer
    EXPECT_EQ(zrt::safe_fmt("{}", -456), "-456");

    // Boolean
    EXPECT_EQ(zrt::safe_fmt("{}", true), "true");
    EXPECT_EQ(zrt::safe_fmt("{}", false), "false");

    // String
    EXPECT_EQ(zrt::safe_fmt("{}", "test"), "test");

    // Floating point with precision
    EXPECT_EQ(zrt::safe_fmt("{:.3f}", 3.14159), "3.142");
}

// Test safe_fmt with positional arguments
TEST_F(FmtHelperTest, SafeFmtPositional) {
    std::string result = zrt::safe_fmt("{0} {1} {0}", "A", "B");
    EXPECT_EQ(result, "A B A");
}

// Test safe_fmt with width and alignment
TEST_F(FmtHelperTest, SafeFmtAlignment) {
    // Right align
    EXPECT_EQ(zrt::safe_fmt("{:>5}", 42), "   42");

    // Left align
    EXPECT_EQ(zrt::safe_fmt("{:<5}", 42), "42   ");

    // Center align
    EXPECT_EQ(zrt::safe_fmt("{:^5}", 42), " 42  ");
}

// Test safe_fmt with hex/octal/binary
TEST_F(FmtHelperTest, SafeFmtNumberFormats) {
    // Hexadecimal
    EXPECT_EQ(zrt::safe_fmt("{:x}", 255), "ff");
    EXPECT_EQ(zrt::safe_fmt("{:X}", 255), "FF");

    // Octal
    EXPECT_EQ(zrt::safe_fmt("{:o}", 64), "100");

    // Binary
    EXPECT_EQ(zrt::safe_fmt("{:b}", 5), "101");
}

// Test safe_fmt with special characters
TEST_F(FmtHelperTest, SafeFmtSpecialChars) {
    // Escaped braces
    EXPECT_EQ(zrt::safe_fmt("{{}}"), "{}");

    // Mixed
    EXPECT_EQ(zrt::safe_fmt("{{{}}} = {}", "key", "value"), "{key} = value");
}

// Test print_fmt does not crash with various inputs
TEST_F(FmtHelperTest, PrintFmtNoCrash) {
    std::streambuf* old_cout = std::cout.rdbuf();
    std::stringstream buffer;
    std::cout.rdbuf(buffer.rdbuf());

    EXPECT_NO_THROW(zrt::print_fmt("Simple"));
    EXPECT_NO_THROW(zrt::print_fmt("Int: {}", 42));
    EXPECT_NO_THROW(zrt::print_fmt("Float: {:.5f}", 3.14159));
    EXPECT_NO_THROW(zrt::print_fmt("Multiple: {} {} {}", 1, 2, 3));

    std::cout.rdbuf(old_cout);
}
