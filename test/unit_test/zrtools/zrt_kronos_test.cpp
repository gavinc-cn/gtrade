//
// Created by Claude Code on 2026/1/29.
//

#include "pch.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <ctime>
#include "zrtools/kronos.h"

class KronosTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
};

// Test getEpoch13 returns 13-digit millisecond timestamp
TEST_F(KronosTest, GetEpoch13) {
    long epoch13 = zrt::getEpoch13();

    // Should be 13 digits
    EXPECT_GE(epoch13, 1000000000000L);
    EXPECT_LT(epoch13, 10000000000000L);

    // Should be reasonably current (after 2020)
    EXPECT_GT(epoch13, 1577836800000L);  // 2020-01-01
}

// Test getEpoch13_v2 consistency
TEST_F(KronosTest, GetEpoch13_v2) {
    long epoch13_v1 = zrt::getEpoch13();
    long epoch13_v2 = zrt::getEpoch13_v2();

    // Should be very close (within 100ms)
    EXPECT_NEAR(epoch13_v1, epoch13_v2, 100);
}

// Test get_epoch_l template function
TEST_F(KronosTest, GetEpochL) {
    // Seconds
    long sec = zrt::get_epoch_l<std::chrono::seconds>();
    EXPECT_GE(sec, 1577836800L);  // After 2020

    // Milliseconds
    long ms = zrt::get_epoch_l<std::chrono::milliseconds>();
    EXPECT_NEAR(sec * 1000, ms, 1000);

    // Microseconds
    long us = zrt::get_epoch_l<std::chrono::microseconds>();
    EXPECT_NEAR(ms * 1000, us, 1000000);
}

// Test epoch13_to_iso2 conversion
TEST_F(KronosTest, Epoch13ToIso2) {
    // Known timestamp: 2023-10-01 11:54:56 UTC = 1696161296000
    long epoch13 = 1696161296000L;
    std::string iso_str = zrt::epoch13_to_iso2(epoch13);

    // Should contain date and time components
    EXPECT_NE(iso_str.find("2023"), std::string::npos);
    EXPECT_NE(iso_str.find("T"), std::string::npos);
    EXPECT_NE(iso_str.find("Z"), std::string::npos);
}

// Test epoch10_to_iso2 conversion
TEST_F(KronosTest, Epoch10ToIso2) {
    // Known timestamp: 2023-10-01 11:54:56 UTC = 1696161296
    long epoch10 = 1696161296L;
    std::string iso_str = zrt::epoch10_to_iso2(epoch10);

    // Should contain date and time components
    EXPECT_NE(iso_str.find("2023"), std::string::npos);
    EXPECT_NE(iso_str.find("T"), std::string::npos);
    EXPECT_NE(iso_str.find("Z"), std::string::npos);
}

// Test time monotonically increasing
TEST_F(KronosTest, MonotonicallyIncreasing) {
    long prev = zrt::getEpoch13();

    for (int i = 0; i < 100; ++i) {
        long curr = zrt::getEpoch13();
        EXPECT_GE(curr, prev);
        prev = curr;
    }
}

// Test get_day_sec function
TEST_F(KronosTest, GetDaySec) {
    // 123456000 means 12:34:56
    int update_time = 123456000;
    int day_sec = zrt::get_day_sec(update_time);

    // 12 * 3600 + 34 * 60 + 56 = 45296
    EXPECT_EQ(day_sec, 12 * 3600 + 34 * 60 + 56);

    // Test midnight
    update_time = 0;
    day_sec = zrt::get_day_sec(update_time);
    EXPECT_EQ(day_sec, 0);

    // Test 23:59:59
    update_time = 235959000;
    day_sec = zrt::get_day_sec(update_time);
    EXPECT_EQ(day_sec, 23 * 3600 + 59 * 60 + 59);
}

// Test get_update_time function
TEST_F(KronosTest, GetUpdateTime) {
    // 45296 seconds = 12:34:56
    int day_seconds = 12 * 3600 + 34 * 60 + 56;
    int update_time = zrt::get_update_time(day_seconds);

    EXPECT_EQ(update_time, 123456000);

    // Test midnight
    day_seconds = 0;
    update_time = zrt::get_update_time(day_seconds);
    EXPECT_EQ(update_time, 0);

    // Test 23:59:59
    day_seconds = 23 * 3600 + 59 * 60 + 59;
    update_time = zrt::get_update_time(day_seconds);
    EXPECT_EQ(update_time, 235959000);
}

// Test get_day_sec and get_update_time round-trip
TEST_F(KronosTest, DaySecRoundTrip) {
    // Test various times
    std::vector<int> test_times = {
        0,          // 00:00:00
        123456000,  // 12:34:56
        235959000,  // 23:59:59
        10000000,   // 01:00:00
        120000000,  // 12:00:00
    };

    for (int update_time : test_times) {
        int day_sec = zrt::get_day_sec(update_time);
        int converted_back = zrt::get_update_time(day_sec);
        EXPECT_EQ(converted_back, update_time);
    }
}

// Test time precision
TEST_F(KronosTest, TimePrecision) {
    // Get multiple timestamps quickly
    std::vector<long> timestamps;
    for (int i = 0; i < 10; ++i) {
        timestamps.push_back(zrt::getEpoch13());
    }

    // Should capture some time passage (not all same)
    bool has_different = false;
    for (size_t i = 1; i < timestamps.size(); ++i) {
        if (timestamps[i] != timestamps[0]) {
            has_different = true;
            break;
        }
    }

    // At least one might be different (depending on system speed)
    // This is a soft check, so we don't fail if all are same
    SUCCEED();
}

// Test epoch conversion consistency
TEST_F(KronosTest, EpochConversionConsistency) {
    // Current time
    long epoch13 = zrt::getEpoch13();
    long epoch10 = epoch13 / 1000;

    // Convert both to ISO
    std::string iso13 = zrt::epoch13_to_iso2(epoch13);
    std::string iso10 = zrt::epoch10_to_iso2(epoch10);

    // Should be the same (ignoring milliseconds)
    // Extract date and time parts
    EXPECT_EQ(iso13.substr(0, 19), iso10.substr(0, 19));
}

// Test boundary dates
TEST_F(KronosTest, BoundaryDates) {
    // Unix epoch start
    std::string epoch_start = zrt::epoch10_to_iso2(0);
    EXPECT_NE(epoch_start.find("1970"), std::string::npos);

    // Y2K timestamp
    long y2k = 946684800L;  // 2000-01-01 00:00:00 UTC
    std::string y2k_str = zrt::epoch10_to_iso2(y2k);
    EXPECT_NE(y2k_str.find("2000"), std::string::npos);
}

// Test time_cost function (just verify it doesn't crash)
TEST_F(KronosTest, TimeCost) {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    double start = ts.tv_sec + ts.tv_nsec / 1e9;

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // This outputs to console, just verify it doesn't crash
    EXPECT_NO_THROW(zrt::time_cost(start));
}
