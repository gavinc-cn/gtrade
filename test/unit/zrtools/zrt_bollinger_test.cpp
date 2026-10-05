//
// Created by Claude Code on 2026/1/29.
//

#include "pch.h"
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "zrtools/bollinger.h"

class BollingerTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    // Helper function to check floating point equality
    bool nearEqual(double a, double b, double epsilon = 1e-9) {
        return std::abs(a - b) < epsilon;
    }
};

// Test basic construction
TEST_F(BollingerTest, BasicConstruction) {
    Bollinger bb(20);

    // Initial state should be empty
    EXPECT_DOUBLE_EQ(bb.GetAverage(), 0.0);
    EXPECT_DOUBLE_EQ(bb.GetStdDev(), 0.0);
}

// Test adding values
TEST_F(BollingerTest, AddValues) {
    Bollinger bb(5);

    bb.AddValue(10.0);
    EXPECT_DOUBLE_EQ(bb.GetAverage(), 10.0);

    bb.AddValue(20.0);
    EXPECT_DOUBLE_EQ(bb.GetAverage(), 15.0);

    bb.AddValue(30.0);
    EXPECT_DOUBLE_EQ(bb.GetAverage(), 20.0);
}

// Test window sliding
TEST_F(BollingerTest, WindowSliding) {
    Bollinger bb(3);

    // Add 3 values
    bb.AddValue(10.0);
    bb.AddValue(20.0);
    bb.AddValue(30.0);
    EXPECT_DOUBLE_EQ(bb.GetAverage(), 20.0);

    // Add 4th value, first one should slide out
    bb.AddValue(40.0);
    // Average should be (20 + 30 + 40) / 3 = 30
    EXPECT_DOUBLE_EQ(bb.GetAverage(), 30.0);

    // Add 5th value
    bb.AddValue(50.0);
    // Average should be (30 + 40 + 50) / 3 = 40
    EXPECT_DOUBLE_EQ(bb.GetAverage(), 40.0);
}

// Test standard deviation calculation
TEST_F(BollingerTest, StdDevCalculation) {
    Bollinger bb(3);

    // All same values should have zero std dev
    bb.AddValue(10.0);
    bb.AddValue(10.0);
    bb.AddValue(10.0);
    EXPECT_NEAR(bb.GetStdDev(), 0.0, 1e-9);

    // Reset with different values
    Bollinger bb2(3);
    bb2.AddValue(1.0);
    bb2.AddValue(2.0);
    bb2.AddValue(3.0);

    // Mean = 2, variance = ((1-2)^2 + (2-2)^2 + (3-2)^2) / 3 = 2/3
    // StdDev = sqrt(2/3) ≈ 0.8165
    double expected_stddev = std::sqrt(2.0/3.0);
    EXPECT_NEAR(bb2.GetStdDev(), expected_stddev, 1e-9);
}

// Test Bollinger bands calculation
TEST_F(BollingerTest, BollingerBands) {
    Bollinger bb(5);
    bb.SetStdMulti(2);

    // Add some values
    for (int i = 1; i <= 5; ++i) {
        bb.AddValue(static_cast<double>(i));
    }

    // {} 初始化：GetBollinger 在窗口为空时提前返回不写输出，未初始化读的是不确定值
    double upper{}, middle{}, lower{};
    bb.GetBollinger(upper, middle, lower);

    // Mean should be 3
    EXPECT_DOUBLE_EQ(middle, 3.0);

    // Upper and lower should be symmetric around middle
    EXPECT_NEAR(upper - middle, middle - lower, 1e-9);

    // Verify via direct methods
    EXPECT_DOUBLE_EQ(bb.GetAverage(), middle);
    EXPECT_DOUBLE_EQ(bb.GetUpperBound(), upper);
    EXPECT_DOUBLE_EQ(bb.GetLowerBound(), lower);
}

// Test SetWindowSize
TEST_F(BollingerTest, SetWindowSize) {
    Bollinger bb(5);

    // Can increase window size
    EXPECT_TRUE(bb.SetWindowSize(10));

    // Cannot decrease window size
    EXPECT_FALSE(bb.SetWindowSize(3));

    // Setting same size should fail
    EXPECT_FALSE(bb.SetWindowSize(10));
}

// Test SetStdMulti
TEST_F(BollingerTest, SetStdMulti) {
    Bollinger bb(5);

    // Add some values with known std dev
    bb.AddValue(1.0);
    bb.AddValue(2.0);
    bb.AddValue(3.0);
    bb.AddValue(4.0);
    bb.AddValue(5.0);

    double avg = bb.GetAverage();
    double stddev = bb.GetStdDev();

    // Default multiplier is 2
    EXPECT_DOUBLE_EQ(bb.GetUpperBound(), avg + 2 * stddev);
    EXPECT_DOUBLE_EQ(bb.GetLowerBound(), avg - 2 * stddev);

    // Change multiplier to 3
    bb.SetStdMulti(3);
    EXPECT_DOUBLE_EQ(bb.GetUpperBound(), avg + 3 * stddev);
    EXPECT_DOUBLE_EQ(bb.GetLowerBound(), avg - 3 * stddev);

    // Change multiplier to 1
    bb.SetStdMulti(1);
    EXPECT_DOUBLE_EQ(bb.GetUpperBound(), avg + 1 * stddev);
    EXPECT_DOUBLE_EQ(bb.GetLowerBound(), avg - 1 * stddev);
}

// Test empty Bollinger bands
TEST_F(BollingerTest, EmptyBands) {
    Bollinger bb(5);

    double upper = 999, middle = 999, lower = 999;
    bb.GetBollinger(upper, middle, lower);

    // Values should remain unchanged when window is empty
    EXPECT_DOUBLE_EQ(upper, 999);
    EXPECT_DOUBLE_EQ(middle, 999);
    EXPECT_DOUBLE_EQ(lower, 999);
}

// Test with negative values
TEST_F(BollingerTest, NegativeValues) {
    Bollinger bb(3);

    bb.AddValue(-10.0);
    bb.AddValue(-20.0);
    bb.AddValue(-30.0);

    EXPECT_DOUBLE_EQ(bb.GetAverage(), -20.0);
    EXPECT_GT(bb.GetStdDev(), 0.0);

    // {} 初始化：GetBollinger 在窗口为空时提前返回不写输出，未初始化读的是不确定值
    double upper{}, middle{}, lower{};
    bb.GetBollinger(upper, middle, lower);

    EXPECT_GT(upper, middle);
    EXPECT_LT(lower, middle);
}

// Test with mixed positive and negative values
TEST_F(BollingerTest, MixedValues) {
    Bollinger bb(4);

    bb.AddValue(-10.0);
    bb.AddValue(10.0);
    bb.AddValue(-5.0);
    bb.AddValue(5.0);

    EXPECT_DOUBLE_EQ(bb.GetAverage(), 0.0);
    EXPECT_GT(bb.GetStdDev(), 0.0);
}

// Test with large values
TEST_F(BollingerTest, LargeValues) {
    Bollinger bb(3);

    bb.AddValue(1e10);
    bb.AddValue(2e10);
    bb.AddValue(3e10);

    EXPECT_DOUBLE_EQ(bb.GetAverage(), 2e10);
    EXPECT_GT(bb.GetStdDev(), 0.0);
}

// Test with small values
TEST_F(BollingerTest, SmallValues) {
    Bollinger bb(3);

    bb.AddValue(1e-10);
    bb.AddValue(2e-10);
    bb.AddValue(3e-10);

    EXPECT_NEAR(bb.GetAverage(), 2e-10, 1e-20);
    EXPECT_GT(bb.GetStdDev(), 0.0);
}

// Test single value in window
TEST_F(BollingerTest, SingleValue) {
    Bollinger bb(10);

    bb.AddValue(50.0);

    EXPECT_DOUBLE_EQ(bb.GetAverage(), 50.0);
    EXPECT_DOUBLE_EQ(bb.GetStdDev(), 0.0);
    EXPECT_DOUBLE_EQ(bb.GetUpperBound(), 50.0);
    EXPECT_DOUBLE_EQ(bb.GetLowerBound(), 50.0);
}

// Test real-world stock price scenario
TEST_F(BollingerTest, RealWorldScenario) {
    Bollinger bb(20);
    bb.SetStdMulti(2);

    // Simulate 25 days of price data
    std::vector<double> prices = {
        100.0, 101.5, 99.0, 102.0, 103.5,
        101.0, 98.5, 97.0, 99.5, 101.0,
        102.5, 104.0, 103.0, 105.0, 106.5,
        105.0, 107.0, 108.5, 107.0, 109.0,
        110.0, 108.0, 111.0, 112.0, 110.5
    };

    for (double price : prices) {
        bb.AddValue(price);
    }

    // {} 初始化：GetBollinger 在窗口为空时提前返回不写输出，未初始化读的是不确定值
    double upper{}, middle{}, lower{};
    bb.GetBollinger(upper, middle, lower);

    // Sanity checks
    EXPECT_GT(upper, middle);
    EXPECT_LT(lower, middle);
    EXPECT_GT(middle, 0);

    // Upper should be above current price range
    EXPECT_GT(upper, 100);
    // Lower should be below some prices
    EXPECT_LT(lower, 112);
}

// Test consistency between GetBollinger and individual methods
TEST_F(BollingerTest, MethodConsistency) {
    Bollinger bb(10);
    bb.SetStdMulti(2);

    for (int i = 1; i <= 10; ++i) {
        bb.AddValue(static_cast<double>(i));
    }

    // {} 初始化：GetBollinger 在窗口为空时提前返回不写输出，未初始化读的是不确定值
    double upper{}, middle{}, lower{};
    bb.GetBollinger(upper, middle, lower);

    EXPECT_DOUBLE_EQ(upper, bb.GetUpperBound());
    EXPECT_DOUBLE_EQ(middle, bb.GetAverage());
    EXPECT_DOUBLE_EQ(lower, bb.GetLowerBound());
}
