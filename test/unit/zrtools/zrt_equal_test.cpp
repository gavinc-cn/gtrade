//
// Created by Claude Code on 2026/1/29.
//

#include "pch.h"
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <string>
#include "zrtools/zrt_equal.h"

class ZrtEqualTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
};

// Test equal for same type integers
TEST_F(ZrtEqualTest, IntegerEquality) {
    EXPECT_TRUE(zrt::equal(42, 42));
    EXPECT_FALSE(zrt::equal(42, 43));
    EXPECT_TRUE(zrt::equal(0, 0));
    EXPECT_TRUE(zrt::equal(-1, -1));
    EXPECT_FALSE(zrt::equal(-1, 1));
}

// Test equal for different integer types
TEST_F(ZrtEqualTest, MixedIntegerTypes) {
    EXPECT_TRUE(zrt::equal(42, 42L));
    EXPECT_TRUE(zrt::equal(42U, 42));
    EXPECT_TRUE(zrt::equal(static_cast<short>(42), 42));
    EXPECT_FALSE(zrt::equal(42, 43L));
}

// Test equal for double values
TEST_F(ZrtEqualTest, DoubleEquality) {
    EXPECT_TRUE(zrt::equal(3.14, 3.14));
    EXPECT_TRUE(zrt::equal(0.0, 0.0));
    EXPECT_TRUE(zrt::equal(-0.0, 0.0));  // Negative zero equals zero

    // Close values within epsilon
    EXPECT_TRUE(zrt::equal(1.0, 1.0 + 1e-9));
    EXPECT_TRUE(zrt::equal(1.0, 1.0 - 1e-9));

    // Values outside epsilon
    EXPECT_FALSE(zrt::equal(1.0, 1.0001));
    EXPECT_FALSE(zrt::equal(1.0, 2.0));
}

// Test equal for float values
TEST_F(ZrtEqualTest, FloatEquality) {
    EXPECT_TRUE(zrt::equal(3.14f, 3.14f));
    EXPECT_TRUE(zrt::equal(0.0f, 0.0f));
    EXPECT_TRUE(zrt::equal(-0.0f, 0.0f));

    // Close values within epsilon
    EXPECT_TRUE(zrt::equal(1.0f, 1.0f + 1e-7f));

    // Values outside epsilon
    EXPECT_FALSE(zrt::equal(1.0f, 1.001f));
}

// Test equal with NaN
TEST_F(ZrtEqualTest, NaNEquality) {
    double nan1 = std::nan("");
    double nan2 = std::nan("");

    // NaN equals NaN in zrt::equal
    EXPECT_TRUE(zrt::equal(nan1, nan2));

    // NaN does not equal a number
    EXPECT_FALSE(zrt::equal(nan1, 0.0));
    EXPECT_FALSE(zrt::equal(0.0, nan2));

    // Same for float
    float nan_f = std::nanf("");
    EXPECT_TRUE(zrt::equal(nan_f, nan_f));
    EXPECT_FALSE(zrt::equal(nan_f, 0.0f));
}

// Test equal with infinity
TEST_F(ZrtEqualTest, InfinityEquality) {
    double inf = std::numeric_limits<double>::infinity();
    double neg_inf = -std::numeric_limits<double>::infinity();

    EXPECT_TRUE(zrt::equal(inf, inf));
    EXPECT_TRUE(zrt::equal(neg_inf, neg_inf));
    EXPECT_FALSE(zrt::equal(inf, neg_inf));
    EXPECT_FALSE(zrt::equal(inf, 0.0));
}

// Test equal for char arrays (C-strings)
TEST_F(ZrtEqualTest, CharArrayEquality) {
    EXPECT_TRUE(zrt::equal("hello", "hello"));
    EXPECT_FALSE(zrt::equal("hello", "world"));
    EXPECT_TRUE(zrt::equal("", ""));
    EXPECT_FALSE(zrt::equal("hello", "hello!"));
}

// Test equal for char and char array
TEST_F(ZrtEqualTest, CharAndCharArray) {
    EXPECT_TRUE(zrt::equal('a', "a"));
    EXPECT_TRUE(zrt::equal("a", 'a'));
    EXPECT_FALSE(zrt::equal('a', "b"));
    EXPECT_FALSE(zrt::equal("ab", 'a'));  // Different lengths
}

// Test equal for string and char array
TEST_F(ZrtEqualTest, StringAndCharArray) {
    std::string s = "hello";
    EXPECT_TRUE(zrt::equal(s, "hello"));
    EXPECT_TRUE(zrt::equal("hello", s));
    EXPECT_FALSE(zrt::equal(s, "world"));
}

// Test equal for string and char
TEST_F(ZrtEqualTest, StringAndChar) {
    std::string s1 = "a";
    EXPECT_TRUE(zrt::equal(s1, 'a'));
    EXPECT_TRUE(zrt::equal('a', s1));

    std::string s2 = "ab";
    EXPECT_FALSE(zrt::equal(s2, 'a'));  // Different lengths
}

// Test equal for char array and integer
TEST_F(ZrtEqualTest, CharArrayAndInteger) {
    EXPECT_TRUE(zrt::equal("42", 42));
    EXPECT_TRUE(zrt::equal(42, "42"));
    EXPECT_FALSE(zrt::equal("42", 43));
    EXPECT_TRUE(zrt::equal("-1", -1));
    EXPECT_FALSE(zrt::equal("abc", 123));
}

// Test equal for string and integer
TEST_F(ZrtEqualTest, StringAndInteger) {
    std::string s = "42";
    EXPECT_TRUE(zrt::equal(s, 42));
    EXPECT_TRUE(zrt::equal(42, s));
    EXPECT_FALSE(zrt::equal(s, 43));
}

// Test equal for char and integer
TEST_F(ZrtEqualTest, CharAndInteger) {
    // '1' equals 1 via string comparison
    EXPECT_TRUE(zrt::equal('1', 1));
    EXPECT_TRUE(zrt::equal(1, '1'));

    // '9' equals 9
    EXPECT_TRUE(zrt::equal('9', 9));
    EXPECT_FALSE(zrt::equal('a', 1));  // 'a' != "1"
}

// Test equal for enum types
TEST_F(ZrtEqualTest, EnumEquality) {
    enum class Color { Red = 1, Green = 2, Blue = 3 };

    EXPECT_TRUE(zrt::equal(1, Color::Red));
    EXPECT_TRUE(zrt::equal(Color::Green, 2));
    EXPECT_FALSE(zrt::equal(1, Color::Blue));

    // Char and enum
    EXPECT_TRUE(zrt::equal('\x01', Color::Red));
    EXPECT_TRUE(zrt::equal(Color::Red, '\x01'));
}

// Test equal with custom epsilon
TEST_F(ZrtEqualTest, CustomEpsilon) {
    // Using larger epsilon
    EXPECT_TRUE(zrt::equal(1.0, 1.001, 0.01));
    EXPECT_FALSE(zrt::equal(1.0, 1.001, 0.0001));

    // Using smaller epsilon
    EXPECT_TRUE(zrt::equal(1.0, 1.0 + 1e-10, 1e-8));
    EXPECT_FALSE(zrt::equal(1.0, 1.0 + 1e-7, 1e-8));

    // Float with custom epsilon
    EXPECT_TRUE(zrt::equal(1.0f, 1.001f, 0.01f));
    EXPECT_FALSE(zrt::equal(1.0f, 1.01f, 0.001f));
}

// Test epsilon constants
TEST_F(ZrtEqualTest, EpsilonConstants) {
    EXPECT_EQ(zrt::kFloatEpsilon, 1e-6f);
    EXPECT_EQ(zrt::kDoubleEpsilon, 1e-8);
}

// Test unsigned integer edge cases
TEST_F(ZrtEqualTest, UnsignedIntegers) {
    unsigned int u1 = 100;
    unsigned int u2 = 100;
    EXPECT_TRUE(zrt::equal(u1, u2));

    // Mixing signed and unsigned
    int s = 100;
    EXPECT_TRUE(zrt::equal(u1, s));
    EXPECT_TRUE(zrt::equal(s, u2));
}

// Test with char arrays and uint
TEST_F(ZrtEqualTest, CharArrayAndUint) {
    EXPECT_TRUE(zrt::equal("100", 100u));
    EXPECT_TRUE(zrt::equal(100u, "100"));
}

// Test empty strings
TEST_F(ZrtEqualTest, EmptyStrings) {
    std::string empty1;
    std::string empty2;

    EXPECT_TRUE(zrt::equal(empty1, empty2));
    EXPECT_TRUE(zrt::equal(empty1, ""));
    EXPECT_TRUE(zrt::equal("", empty2));
    EXPECT_FALSE(zrt::equal(empty1, "nonempty"));
}

// Test very large numbers
TEST_F(ZrtEqualTest, LargeNumbers) {
    double large1 = 1e15;
    double large2 = 1e15;
    EXPECT_TRUE(zrt::equal(large1, large2));

    // Large numbers with small difference
    EXPECT_TRUE(zrt::equal(large1, large1 + 1e6, 1e7));  // Relative epsilon
}

// Test very small numbers
TEST_F(ZrtEqualTest, SmallNumbers) {
    double small1 = 1e-15;
    double small2 = 1e-15;
    EXPECT_TRUE(zrt::equal(small1, small2));

    // Near zero
    EXPECT_TRUE(zrt::equal(1e-10, 0.0, 1e-8));
    EXPECT_FALSE(zrt::equal(1e-6, 0.0, 1e-8));
}
