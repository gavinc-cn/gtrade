#include "pch.h"
#include "gtest/gtest.h"
#include "zrtools/zrt_math.h"

TEST(ZrtMathTest, RoundTest) {
    EXPECT_DOUBLE_EQ(zrt::round(3.14159, 2), 3.14);
    EXPECT_DOUBLE_EQ(zrt::round(3.14159, 3), 3.142);
    EXPECT_DOUBLE_EQ(zrt::round(-3.14159, 2), -3.14);
}

TEST(ZrtMathTest, RoundToTest) {
    EXPECT_DOUBLE_EQ(zrt::round_to(3.14159, 0.1), 3.1);
    EXPECT_DOUBLE_EQ(zrt::round_to(3.14159, 0.01), 3.14);
    EXPECT_DOUBLE_EQ(zrt::round_to(-3.14159, 0.1), -3.1);
}

TEST(ZrtMathTest, FloorToTest) {
    EXPECT_DOUBLE_EQ(zrt::floor_to(3.14159, 0.1), 3.1);
    EXPECT_DOUBLE_EQ(zrt::floor_to(3.14159, 0.01), 3.14);
    EXPECT_DOUBLE_EQ(zrt::floor_to(-3.14159, 0.1), -3.2);
}

TEST(ZrtMathTest, RoundStrTest) {
    EXPECT_EQ(zrt::round_str(3.14159, 2), "3.14");
    EXPECT_EQ(zrt::round_str(3.14159, 3), "3.142");
    EXPECT_EQ(zrt::round_str(-3.14159, 2), "-3.14");
    EXPECT_EQ(zrt::round_str(-3.14159, 3), "-3.142");
}

TEST(ZrtMathTest, RoundToStrTest) {
    EXPECT_EQ(zrt::round_to_str(3.14159, 0.1), "3.1");
    EXPECT_EQ(zrt::round_to_str(3.14159, 0.01), "3.14");
    EXPECT_EQ(zrt::round_to_str(-3.14159, 0.1), "-3.1");
    EXPECT_EQ(zrt::round_to_str(-3.14159, 0.5), "-3.0");
    EXPECT_EQ(zrt::round_to_str(-3.5, 0.5), "-3.5");
}

TEST(ZrtMathTest, FloorToStrTest) {
    EXPECT_EQ(zrt::floor_to_str(3.14159, 0.1), "3.1");
    EXPECT_EQ(zrt::floor_to_str(3.14159, 0.01), "3.14");
    EXPECT_EQ(zrt::floor_to_str(-3.14159, 0.1), "-3.2");
}

// Test get_decimal_places function
TEST(ZrtMathTest, GetDecimalPlaces) {
    // Test with various precision values
    EXPECT_EQ(zrt::get_decimal_places(0.1), 1);
    EXPECT_EQ(zrt::get_decimal_places(0.01), 2);
    EXPECT_EQ(zrt::get_decimal_places(0.001), 3);
    EXPECT_EQ(zrt::get_decimal_places(0.0001), 4);

    // Test with zero
    EXPECT_EQ(zrt::get_decimal_places(0.0), 0);

    // Test with values >= 1
    EXPECT_EQ(zrt::get_decimal_places(1.0), 0);
    EXPECT_EQ(zrt::get_decimal_places(10.0), 0);
}

// Test SMA class basic functionality
TEST(ZrtMathTest, SMABasic) {
    zrt::SMA<double> sma(3);

    // Add values
    sma.Add(10.0);
    EXPECT_DOUBLE_EQ(sma.GetAvg(), 10.0);
    EXPECT_EQ(sma.GetSize(), 1);

    sma.Add(20.0);
    EXPECT_DOUBLE_EQ(sma.GetAvg(), 15.0);
    EXPECT_EQ(sma.GetSize(), 2);

    sma.Add(30.0);
    EXPECT_DOUBLE_EQ(sma.GetAvg(), 20.0);
    EXPECT_EQ(sma.GetSize(), 3);
}

// Test SMA window sliding
TEST(ZrtMathTest, SMAWindowSliding) {
    zrt::SMA<double> sma(3);

    sma.Add(10.0);
    sma.Add(20.0);
    sma.Add(30.0);
    EXPECT_DOUBLE_EQ(sma.GetAvg(), 20.0);

    // Add 4th value, first one should slide out
    sma.Add(40.0);
    // Average should be (20 + 30 + 40) / 3 = 30
    EXPECT_DOUBLE_EQ(sma.GetAvg(), 30.0);
    EXPECT_EQ(sma.GetSize(), 3);

    // Add 5th value
    sma.Add(50.0);
    // Average should be (30 + 40 + 50) / 3 = 40
    EXPECT_DOUBLE_EQ(sma.GetAvg(), 40.0);
    EXPECT_EQ(sma.GetSize(), 3);
}

// Test SMA SetWindowSize
TEST(ZrtMathTest, SMASetWindowSize) {
    zrt::SMA<double> sma;

    sma.SetWindowSize(5);

    for (int i = 1; i <= 5; ++i) {
        sma.Add(static_cast<double>(i));
    }

    // Average of 1, 2, 3, 4, 5 = 3
    EXPECT_DOUBLE_EQ(sma.GetAvg(), 3.0);
    EXPECT_EQ(sma.GetSize(), 5);

    // Add more values
    sma.Add(6.0);
    // Average of 2, 3, 4, 5, 6 = 4
    EXPECT_DOUBLE_EQ(sma.GetAvg(), 4.0);
}

// Test SMA with integer type
TEST(ZrtMathTest, SMAInteger) {
    zrt::SMA<int> sma(4);

    sma.Add(10);
    sma.Add(20);
    sma.Add(30);
    sma.Add(40);

    // Average of 10, 20, 30, 40 = 25
    EXPECT_EQ(sma.GetAvg(), 25);

    sma.Add(50);
    // Average of 20, 30, 40, 50 = 35
    EXPECT_EQ(sma.GetAvg(), 35);
}

// Test SMA with negative values
TEST(ZrtMathTest, SMANegativeValues) {
    zrt::SMA<double> sma(3);

    sma.Add(-10.0);
    sma.Add(-20.0);
    sma.Add(-30.0);

    EXPECT_DOUBLE_EQ(sma.GetAvg(), -20.0);
}

// Test SMA with mixed positive and negative
TEST(ZrtMathTest, SMAMixedValues) {
    zrt::SMA<double> sma(4);

    sma.Add(-10.0);
    sma.Add(10.0);
    sma.Add(-5.0);
    sma.Add(5.0);

    EXPECT_DOUBLE_EQ(sma.GetAvg(), 0.0);
}

// Test SMA default constructor
TEST(ZrtMathTest, SMADefaultConstructor) {
    zrt::SMA<double> sma;

    // With default window size 0, behavior may vary
    sma.SetWindowSize(2);

    sma.Add(5.0);
    sma.Add(15.0);
    EXPECT_DOUBLE_EQ(sma.GetAvg(), 10.0);

    sma.Add(25.0);
    EXPECT_DOUBLE_EQ(sma.GetAvg(), 20.0);
}

// Test SMA with single element window
TEST(ZrtMathTest, SMASingleElement) {
    zrt::SMA<double> sma(1);

    sma.Add(10.0);
    EXPECT_DOUBLE_EQ(sma.GetAvg(), 10.0);

    sma.Add(20.0);
    EXPECT_DOUBLE_EQ(sma.GetAvg(), 20.0);

    sma.Add(30.0);
    EXPECT_DOUBLE_EQ(sma.GetAvg(), 30.0);
}

// Test round_to with edge cases
TEST(ZrtMathTest, RoundToEdgeCases) {
    // Test with very small precision
    EXPECT_NEAR(zrt::round_to(3.14159265, 0.0001), 3.1416, 1e-10);

    // Test with large values
    EXPECT_DOUBLE_EQ(zrt::round_to(1234.5, 10), 1230.0);
    EXPECT_DOUBLE_EQ(zrt::round_to(1235.5, 10), 1240.0);
}

// Test floor_to with edge cases
TEST(ZrtMathTest, FloorToEdgeCases) {
    // Test with very small precision
    EXPECT_NEAR(zrt::floor_to(3.14159265, 0.0001), 3.1415, 1e-10);

    // Test with large values
    EXPECT_DOUBLE_EQ(zrt::floor_to(1239.9, 10), 1230.0);
}

// Test round with boundary cases
TEST(ZrtMathTest, RoundBoundaryCases) {
    // Rounding up (std::round uses round-half-away-from-zero)
    EXPECT_DOUBLE_EQ(zrt::round(1.5, 0), 2.0);
    EXPECT_DOUBLE_EQ(zrt::round(2.5, 0), 3.0);  // std::round rounds 2.5 to 3

    // Many decimal places
    EXPECT_DOUBLE_EQ(zrt::round(3.14159265359, 5), 3.14159);
}