//
// Created by dell on 2024/10/28.
//

#include "pch.h"
#include <cmath>
#include <limits>
#include <iostream>
#include <gtest/gtest.h>
#include <string>
#include <type_traits>
#include <vector>
#include <chrono>

#include "zrtools/zrt_compare.h"
#include "zrtools/fmt_helper.h"

TEST(zrt_compare, float_compare) {
    float f1 = 1.1;
    float f2 = 1;
    EXPECT_TRUE(zrt::greater(f1, f2));
    EXPECT_TRUE(zrt::greater_equal(f1, f2));
    EXPECT_TRUE(!zrt::equal(f1, f2));
    EXPECT_TRUE(!zrt::less_equal(f1, f2));
    EXPECT_TRUE(!zrt::less(f1, f2));

    f1 = 1;
    f2 = 1.00001;
    EXPECT_TRUE(!zrt::greater(f1, f2));
    EXPECT_TRUE(!zrt::greater_equal(f1, f2));
    EXPECT_TRUE(!zrt::equal(f1, f2));
    EXPECT_TRUE(zrt::less_equal(f1, f2));
    EXPECT_TRUE(zrt::less(f1, f2));

    f1 = 1;
    f2 = 1.000001;
    EXPECT_TRUE(!zrt::greater(f1, f2));
    EXPECT_TRUE(zrt::greater_equal(f1, f2));
    EXPECT_TRUE(zrt::equal(f1, f2));
    EXPECT_TRUE(zrt::less_equal(f1, f2));
    EXPECT_TRUE(!zrt::less(f1, f2));

    f1 = 99999.0;
    f2 = 99999.1;
    EXPECT_TRUE(!zrt::greater(f1, f2));
    EXPECT_TRUE(!zrt::greater_equal(f1, f2));
    EXPECT_TRUE(!zrt::equal(f1, f2));
    EXPECT_TRUE(zrt::less_equal(f1, f2));
    EXPECT_TRUE(zrt::less(f1, f2));

    f1 = 999999.0;
    f2 = 999999.1;
    EXPECT_TRUE(!zrt::greater(f1, f2));
    EXPECT_TRUE(!zrt::greater_equal(f1, f2));
    EXPECT_TRUE(!zrt::equal(f1, f2));
    EXPECT_TRUE(zrt::less_equal(f1, f2));
    EXPECT_TRUE(zrt::less(f1, f2));

    f1 = -0.00000001;
    f2 =  0.00000001;
    EXPECT_TRUE(!zrt::greater(f1, f2));
    EXPECT_TRUE(zrt::greater_equal(f1, f2));
    EXPECT_TRUE(zrt::equal(f1, f2));
    EXPECT_TRUE(zrt::less_equal(f1, f2));
    EXPECT_TRUE(!zrt::less(f1, f2));

    f1 =  0.00000001;
    f2 = -0.00000001;
    printf("%.20f\n", f1);
    printf("%.20f\n", f2);
    zrt::print(std::fabs(f1 / f2 -1));
    zrt::print(std::fabs((f1 - f2) / std::max(std::fabs(f1), std::fabs(f2))));
    EXPECT_TRUE(!zrt::greater(f1, f2));
    EXPECT_TRUE(zrt::greater_equal(f1, f2));
    EXPECT_TRUE(zrt::equal(f1, f2));
    EXPECT_TRUE(zrt::less_equal(f1, f2));
    EXPECT_TRUE(!zrt::less(f1, f2));
}

TEST(zrt_compare, double_compare) {
    double f1 = 1.1;
    double f2 = 1;
    EXPECT_TRUE(zrt::greater(f1, f2));
    EXPECT_TRUE(zrt::greater_equal(f1, f2));
    EXPECT_TRUE(!zrt::equal(f1, f2));
    EXPECT_TRUE(!zrt::less_equal(f1, f2));
    EXPECT_TRUE(!zrt::less(f1, f2));

    f1 = 1;
    f2 = 1.00001;
    EXPECT_TRUE(!zrt::greater(f1, f2));
    EXPECT_TRUE(!zrt::greater_equal(f1, f2));
    EXPECT_TRUE(!zrt::equal(f1, f2));
    EXPECT_TRUE(zrt::less_equal(f1, f2));
    EXPECT_TRUE(zrt::less(f1, f2));

    f1 = 1;
    f2 = 1.00000001;
    EXPECT_TRUE(!zrt::greater(f1, f2));
    EXPECT_TRUE(zrt::greater_equal(f1, f2));
    EXPECT_TRUE(zrt::equal(f1, f2));
    EXPECT_TRUE(zrt::less_equal(f1, f2));
    EXPECT_TRUE(!zrt::less(f1, f2));

    f1 = 9999999.0;
    f2 = 9999999.1;
    EXPECT_TRUE(!zrt::greater(f1, f2));
    EXPECT_TRUE(!zrt::greater_equal(f1, f2));
    EXPECT_TRUE(!zrt::equal(f1, f2));
    EXPECT_TRUE(zrt::less_equal(f1, f2));
    EXPECT_TRUE(zrt::less(f1, f2));

    f1 = 99999999.0;
    f2 = 99999999.1;
    EXPECT_TRUE(!zrt::greater(f1, f2));
    EXPECT_TRUE(!zrt::greater_equal(f1, f2));
    EXPECT_TRUE(!zrt::equal(f1, f2));
    EXPECT_TRUE(zrt::less_equal(f1, f2));
    EXPECT_TRUE(zrt::less(f1, f2));
}

TEST(zrt_compare, error_accumulation) {
    const double a = 0.1;
    const double b = 10000;
    double sum = 0.0;
    for (int i = 0; i < b; ++i) {
        sum += a;
        printf("sum: %.20f\n", sum);
    }
    double multi = a * b;
    printf("multi: %.20f\n", multi);
    EXPECT_TRUE(sum != multi);
    zrt::print_fmt("误差累计{}次依然可以相等", b);
    EXPECT_TRUE(zrt::equal(sum, multi));
}

// Test for double equality
TEST(zrt_compare, double_equality) {
    EXPECT_TRUE(zrt::equal(0.0, 0.0)); // Both zero
    EXPECT_TRUE(zrt::equal(1.0, 1.0)); // Both same non-zero value
    EXPECT_TRUE(!zrt::equal(1.0, 2.0)); // Different values
    EXPECT_TRUE(zrt::equal(-0.0, 0.0)); // Negative zero and zero
    EXPECT_TRUE(zrt::equal(0.0, -0.0)); // Zero and negative zero
    EXPECT_TRUE(zrt::equal(std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()));
    EXPECT_TRUE(zrt::equal(-std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()));
    EXPECT_TRUE(!zrt::equal(std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()));
    EXPECT_TRUE(zrt::equal(std::nan(""), std::nan("")));
    EXPECT_TRUE(!zrt::equal(std::nan(""), 0.0));
    EXPECT_TRUE(!zrt::equal(0.0, std::nan("")));
}

// Test for float equality
TEST(zrt_compare, float_equality) {
    EXPECT_TRUE(zrt::equal(0.0f, 0.0f)); // Both zero
    EXPECT_TRUE(zrt::equal(1.0f, 1.0f)); // Both same non-zero value
    EXPECT_TRUE(!zrt::equal(1.0f, 2.0f)); // Different values
    EXPECT_TRUE(zrt::equal(-0.0f, 0.0f)); // Negative zero and zero
    EXPECT_TRUE(zrt::equal(0.0f, -0.0f)); // Zero and negative zero
    EXPECT_TRUE(zrt::equal(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()));
    EXPECT_TRUE(zrt::equal(-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()));
    EXPECT_TRUE(!zrt::equal(std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()));
    EXPECT_TRUE(zrt::equal(std::nanf(""), std::nanf("")));
    EXPECT_TRUE(!zrt::equal(std::nanf(""), 0.0f));
    EXPECT_TRUE(!zrt::equal(0.0f, std::nanf("")));
}

TEST(zrt_compare, buildin_equal) {
    EXPECT_EQ(INFINITY, INFINITY);
    EXPECT_NE(-INFINITY, INFINITY);
    EXPECT_NE(INFINITY, -INFINITY);
    EXPECT_EQ(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity());
    EXPECT_NE(NAN, NAN);
    EXPECT_NE(-NAN, NAN);
    EXPECT_NE(NAN, -NAN);
    EXPECT_NE(NAN, INFINITY);
    EXPECT_NE(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    EXPECT_NE(std::numeric_limits<float>::signaling_NaN(), std::numeric_limits<float>::signaling_NaN());
}

// 测试用的Point结构体
struct Point {
    int x, y;

    Point(int x, int y) : x(x), y(y) {
    }

    bool operator<(const Point &other) const {
        return (x * x + y * y) < (other.x * other.x + other.y * other.y);
    }

    bool operator==(const Point &other) const {
        return x == other.x && y == other.y;
    }

    friend std::ostream &operator<<(std::ostream &os, const Point &p) {
        return os << "Point(" << p.x << ", " << p.y << ")";
    }
};

// 基本类型测试
TEST(MaxTest, BasicTypes) {
    EXPECT_EQ(5, zrt::max(1, 2, 3, 4, 5));
    EXPECT_EQ(4.18, zrt::max(3.14, 2.71, 1.41, 4.18));
    EXPECT_EQ('z', zrt::max('a', 'z', 'm', 'c'));
}

TEST(MinTest, BasicTypes) {
    EXPECT_EQ(1, zrt::min(1, 2, 3, 4, 5));
    EXPECT_EQ(1.41, zrt::min(3.14, 2.71, 1.41, 4.18));
    EXPECT_EQ('a', zrt::min('a', 'z', 'm', 'c'));
}

// 混合类型测试
TEST(MaxTest, MixedTypes) {
    EXPECT_EQ(3, zrt::max(1, 2.5, 3));
    EXPECT_EQ(3, zrt::max(1.0f, 2.5, 3));
}

TEST(MinTest, MixedTypes) {
    EXPECT_EQ(1, zrt::min(1, 2.5, 3));
    EXPECT_EQ(1.0f, zrt::min(1.0f, 2.5, 3));
}

// 字符串测试
TEST(MaxTest, StringTypes) {
    std::string s1 = "apple", s2 = "banana", s3 = "cherry", s4 = "date";
    EXPECT_EQ("date", zrt::max(s1, s2, s3, s4));
}

TEST(MinTest, StringTypes) {
    std::string s1 = "apple", s2 = "banana", s3 = "cherry", s4 = "date";
    EXPECT_EQ("apple", zrt::min(s1, s2, s3, s4));
}

// 自定义类型测试
class PointTest : public ::testing::Test {
protected:
    Point p1{1, 2};
    Point p2{3, 4};
    Point p3{0, 5};
    Point p4{2, 1};
};

TEST_F(PointTest, MaxFunction) {
    EXPECT_EQ(Point(0, 5), zrt::max(p1, p2, p3, p4));
}

TEST_F(PointTest, MinFunction) {
    EXPECT_EQ(Point(2, 1), zrt::min(p1, p2, p3, p4));
}

// 编译时常量测试
TEST(MaxTest, CompileTimeConstants) {
    constexpr int max_result = zrt::max(10, 20, 5, 15, 8);
    EXPECT_EQ(20, max_result);
}

TEST(MinTest, CompileTimeConstants) {
    constexpr int min_result = zrt::min(10, 20, 5, 15, 8);
    EXPECT_EQ(5, min_result);
}

// 单参数测试
TEST(MaxTest, SingleParameter) {
    EXPECT_EQ(42, zrt::max(42));
}

TEST(MinTest, SingleParameter) {
    EXPECT_EQ(42, zrt::min(42));
}

// 两参数测试
TEST(MaxTest, TwoParameters) {
    EXPECT_EQ(20, zrt::max(10, 20));
}

TEST(MinTest, TwoParameters) {
    EXPECT_EQ(10, zrt::min(10, 20));
}

// 大量参数测试
TEST(MaxTest, ManyParameters) {
    EXPECT_EQ(10, zrt::max(1,2,3,4,5,6,7,8,9,10));
}

TEST(MinTest, ManyParameters) {
    EXPECT_EQ(1, zrt::min(1,2,3,4,5,6,7,8,9,10));
}

// 显式命名空间测试
TEST(MaxTest, ExplicitNamespace) {
    EXPECT_EQ(200, zrt::max(100, 200, 150));
}

TEST(MinTest, ExplicitNamespace) {
    EXPECT_EQ(100, zrt::min(100, 200, 150));
}

// NAN处理测试
class NanTest : public ::testing::Test {
protected:
    void SetUp() override {
        nan_val = std::numeric_limits<double>::quiet_NaN();
        nan_f = std::numeric_limits<float>::quiet_NaN();
    }

    double nan_val;
    float nan_f;
};

TEST_F(NanTest, MaxFunction) {
    EXPECT_EQ(3.14, zrt::max(3.14, nan_val));
    EXPECT_EQ(3.14, zrt::max(nan_val, 3.14));
    EXPECT_EQ(3.0, zrt::max(1.0, nan_val, 3.0, 2.0));
    EXPECT_TRUE(std::isnan(zrt::max(nan_val, nan_val)));
    EXPECT_EQ(2.5f, zrt::max(2.5f, nan_f));
    EXPECT_EQ(3, zrt::max(1, 2.5, nan_val, 3));
}

TEST_F(NanTest, MinFunction) {
    EXPECT_EQ(3.14, zrt::min(3.14, nan_val));
    EXPECT_EQ(3.14, zrt::min(nan_val, 3.14));
    EXPECT_EQ(1.0, zrt::min(1.0, nan_val, 3.0, 2.0));
    EXPECT_TRUE(std::isnan(zrt::min(nan_val, nan_val)));
    EXPECT_EQ(2.5f, zrt::min(2.5f, nan_f));
    EXPECT_EQ(1, zrt::min(1, 2.5, nan_val, 3));

    EXPECT_EQ(3.0, zrt::max(1.0, NAN, 3.0, 2));
    EXPECT_EQ(1.0, zrt::min(1.0, NAN, 3.0, 2));
}

// 边界值测试
TEST(MaxTest, BoundaryValues) {
    EXPECT_EQ(std::numeric_limits<int>::max(),
              zrt::max(std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
    EXPECT_EQ(0, zrt::max(-1, 0));
    EXPECT_EQ(0.0, zrt::max(-0.0, 0.0));
}

TEST(MinTest, BoundaryValues) {
    EXPECT_EQ(std::numeric_limits<int>::min(),
              zrt::min(std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
    EXPECT_EQ(-1, zrt::min(-1, 0));
    EXPECT_EQ(-0.0, zrt::min(-0.0, 0.0));
}

// 浮点数精度测试
TEST(MaxTest, FloatingPointPrecision) {
    EXPECT_NEAR(0.1 + 0.2, zrt::max(0.1, 0.2, 0.3), 1e-10);
    EXPECT_NEAR(1.0/3.0, zrt::max(0.333333, 1.0/3.0), 1e-10);
}

TEST(MinTest, FloatingPointPrecision) {
    EXPECT_NEAR(0.1, zrt::min(0.1, 0.2, 0.3), 1e-10);
    EXPECT_NEAR(0.333333, zrt::min(0.333333, 1.0/3.0), 1e-10);
}

// 无符号整数测试
TEST(MaxTest, UnsignedIntegers) {
    EXPECT_EQ(255u, zrt::max(100u, 200u, 255u));
    EXPECT_EQ(std::numeric_limits<unsigned int>::max(),
              zrt::max(0u, std::numeric_limits<unsigned int>::max()));
}

TEST(MinTest, UnsignedIntegers) {
    EXPECT_EQ(100u, zrt::min(100u, 200u, 255u));
    EXPECT_EQ(0u, zrt::min(0u, std::numeric_limits<unsigned int>::max()));
}

// 指针比较测试
TEST(MaxTest, PointerComparison) {
    int arr[] = {1, 2, 3};
    int *p1 = &arr[0];
    int *p2 = &arr[1];
    int *p3 = &arr[2];
    EXPECT_EQ(p3, zrt::max(p1, p2, p3));
}

TEST(MinTest, PointerComparison) {
    int arr[] = {1, 2, 3};
    int *p1 = &arr[0];
    int *p2 = &arr[1];
    int *p3 = &arr[2];
    EXPECT_EQ(p1, zrt::min(p1, p2, p3));
}

// 复数测试
struct Complex {
    double real, imag;

    Complex(double r, double i) : real(r), imag(i) {
    }

    bool operator<(const Complex &other) const {
        return (real * real + imag * imag) < (other.real * other.real + other.imag * other.imag);
    }

    bool operator==(const Complex &other) const {
        return real == other.real && imag == other.imag;
    }
};

TEST(MaxTest, ComplexNumbers) {
    Complex c1(1, 1), c2(2, 2), c3(0, 3);
    EXPECT_EQ(Complex(0, 3), zrt::max(c1, c2, c3));
}

TEST(MinTest, ComplexNumbers) {
    Complex c1(1, 1), c2(2, 2), c3(0, 3);
    EXPECT_EQ(Complex(1, 1), zrt::min(c1, c2, c3));
}

// 新增测试用例

// 相等值测试
TEST(MaxTest, EqualValues) {
    EXPECT_EQ(5, zrt::max(5, 5, 5));
    EXPECT_EQ(3.14, zrt::max(3.14, 3.14));
    EXPECT_EQ('a', zrt::max('a', 'a', 'a', 'a'));
}

TEST(MinTest, EqualValues) {
    EXPECT_EQ(5, zrt::min(5, 5, 5));
    EXPECT_EQ(3.14, zrt::min(3.14, 3.14));
    EXPECT_EQ('a', zrt::min('a', 'a', 'a', 'a'));
}

// 负数测试
TEST(MaxTest, NegativeNumbers) {
    EXPECT_EQ(-1, zrt::max(-5, -3, -1, -4));
    EXPECT_EQ(-1.1, zrt::max(-5.5, -3.3, -1.1, -4.4));
}

TEST(MinTest, NegativeNumbers) {
    EXPECT_EQ(-5, zrt::min(-5, -3, -1, -4));
    EXPECT_EQ(-5.5, zrt::min(-5.5, -3.3, -1.1, -4.4));
}

// 零值测试
TEST(MaxTest, ZeroValues) {
    EXPECT_EQ(0, zrt::max(0, 0, 0));
    EXPECT_EQ(1, zrt::max(-1, 0, 1));
    EXPECT_EQ(0.0, zrt::max(-1.0, 0.0, -2.0));
}

TEST(MinTest, ZeroValues) {
    EXPECT_EQ(0, zrt::min(0, 0, 0));
    EXPECT_EQ(-1, zrt::min(-1, 0, 1));
    EXPECT_EQ(-2.0, zrt::min(-1.0, 0.0, -2.0));
}

// 长整型测试
TEST(MaxTest, LongTypes) {
    EXPECT_EQ(1000000000L, zrt::max(100L, 1000000000L, 500L));
    EXPECT_EQ(std::numeric_limits<long long>::max(),
              zrt::max(0LL, std::numeric_limits<long long>::max()));
}

TEST(MinTest, LongTypes) {
    EXPECT_EQ(100L, zrt::min(100L, 1000000000L, 500L));
    EXPECT_EQ(std::numeric_limits<long long>::min(),
              zrt::min(0LL, std::numeric_limits<long long>::min()));
}

// 布尔值测试
TEST(MaxTest, BooleanValues) {
    EXPECT_EQ(true, zrt::max(true, false));
    EXPECT_EQ(true, zrt::max(false, true, false));
}

TEST(MinTest, BooleanValues) {
    EXPECT_EQ(false, zrt::min(true, false));
    EXPECT_EQ(false, zrt::min(false, true, false));
}

// 枚举类型测试
enum class Color { RED = 1, GREEN = 2, BLUE = 3 };

TEST(MaxTest, EnumValues) {
    EXPECT_EQ(Color::BLUE, zrt::max(Color::RED, Color::GREEN, Color::BLUE));
}

TEST(MinTest, EnumValues) {
    EXPECT_EQ(Color::RED, zrt::min(Color::RED, Color::GREEN, Color::BLUE));
}

// 无穷大测试
TEST(MaxTest, InfinityValues) {
    double inf = std::numeric_limits<double>::infinity();
    double neg_inf = -std::numeric_limits<double>::infinity();
    EXPECT_EQ(inf, zrt::max(1.0, inf, 2.0));
    EXPECT_EQ(1.0, zrt::max(1.0, neg_inf, -2.0));
}

TEST(MinTest, InfinityValues) {
    double inf = std::numeric_limits<double>::infinity();
    double neg_inf = -std::numeric_limits<double>::infinity();
    EXPECT_EQ(1.0, zrt::min(1.0, inf, 2.0));
    EXPECT_EQ(neg_inf, zrt::min(1.0, neg_inf, -2.0));
}

// 类型推导测试
TEST(MaxTest, TypeDeduction) {
    auto result1 = zrt::max(1, 2.5);
    EXPECT_TRUE((std::is_same<decltype(result1), double>::value));

    auto result2 = zrt::max(1.0f, 2.0);
    EXPECT_TRUE((std::is_same<decltype(result2), double>::value));
}

TEST(MinTest, TypeDeduction) {
    auto result1 = zrt::min(1, 2.5);
    EXPECT_TRUE((std::is_same<decltype(result1), double>::value));

    auto result2 = zrt::min(1.0f, 2.0);
    EXPECT_TRUE((std::is_same<decltype(result2), double>::value));
}

// 递归深度测试
TEST(MaxTest, RecursionDepth) {
    EXPECT_EQ(20, zrt::max(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20));
}

TEST(MinTest, RecursionDepth) {
    EXPECT_EQ(1, zrt::min(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20));
}

// 自定义比较类型测试
struct CustomCompare {
    int value;

    CustomCompare(int v) : value(v) {
    }

    bool operator<(const CustomCompare &other) const {
        return value > other.value; // 反向比较
    }

    bool operator==(const CustomCompare &other) const {
        return value == other.value;
    }
};

TEST(MaxTest, CustomComparison) {
    CustomCompare c1(1), c2(2), c3(3);
    EXPECT_EQ(CustomCompare(1), zrt::max(c1, c2, c3)); // 因为反向比较，最小值变成最大值
}

TEST(MinTest, CustomComparison) {
    CustomCompare c1(1), c2(2), c3(3);
    EXPECT_EQ(CustomCompare(3), zrt::min(c1, c2, c3)); // 因为反向比较，最大值变成最小值
}
