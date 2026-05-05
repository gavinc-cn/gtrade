//
// Created by dell on 2024/10/28.
//

#include <cmath>
#include <limits>
#include <iostream>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "zrtools/zrt_time-inl.h"


class ZrtTimeF : public ::testing::Test {
protected:
    void SetUp() override {
    }

    int64_t sec_epoch10 = 1696161296;
    int64_t sec_epoch13 = 1696161296000;
    std::string sec_time_str_utc = "2023-10-01 11:54:56";
    std::string sec_time_str_local = "2023-10-01 19:54:56";
    std::string sec_format = "%Y-%m-%d %H:%M:%S";

    int64_t mill_epoch13 = 1696161296000;
    std::string mill_time_str_utc = "2023-10-01 11:54:56.000";
    std::string mill_time_str_local = "2023-10-01 19:54:56.000";
    std::string mill_format = "%Y-%m-%d %H:%M:%S.%f";
};

TEST(ZrtTime, EpochConversion) {
    // 验证不同精度时间戳的换算关系
    int64_t epoch19 = zrt::get_epoch19();
    EXPECT_EQ(zrt::get_epoch16() - epoch19 / 1000 <= 1000, true);
    EXPECT_EQ(zrt::get_epoch13() - epoch19 / 1000000 <= 1, true);
    EXPECT_EQ(zrt::get_epoch10()- epoch19 / 1000000000 <= 1, true);
}

// 测试时间转换函数
TEST_F(ZrtTimeF, EpochFunctions) {
    // 测试19位时间戳
    long epoch19 = zrt::get_epoch19();
    EXPECT_GT(epoch19, 0);

    int64_t epoch13 = zrt::get_epoch13(mill_time_str_local, mill_format);
    EXPECT_EQ(epoch13, mill_epoch13);

    // 测试时间字符串转换
    std::string time_str_local = zrt::get_time_str_local();
    EXPECT_FALSE(time_str_local.empty());
}

TEST(ZrtTime, ParseEmptyString) {
    EXPECT_EQ(zrt::get_epoch13("", ""), 0);
}

TEST(ZrtTime, DurationMeasurement) {
    zrt::Timer t("unittest");
    usleep(100000);  // 100ms
    EXPECT_NEAR(t.get_diff(), 0.1, 0.01);  // 允许1%误差
}

TEST(ZrtTime, TimeOffset) {
    zrt::DateTime<zrt::kLocal> dt(1672583696);  // 2023-01-01 12:34:56
    dt.OffsetHour(1).OffsetMin(5).OffsetSec(-1000).OffsetNano(-1);
    EXPECT_EQ(dt.Epoch10(), 1672583696 + 3600 + 300 - 1000 - 1);
    EXPECT_EQ(dt.Epoch13(), (1672583696ll + 3600 + 300 - 1000) * 1000 - 1);

    zrt::DateTime<zrt::kLocal> dt2(1672583696999999999, 19);  // 2023-01-01 12:34:56
    dt2.OffsetNano(1);
    EXPECT_EQ(dt2.Epoch19(), 1672583696999999999 + 1);

    zrt::DateTime<zrt::kUTC> dt3(0);
    dt3.SetNano(999999999).OffsetNano(1);
    EXPECT_EQ(dt3.Epoch10(), 1);  // 纳秒溢出后秒数+1
}

// 测试DateTime类基本功能
TEST_F(ZrtTimeF, DateTimeBasic) {
    zrt::DateTime<zrt::kUTC> dt;
    EXPECT_GT(dt.Epoch19(), 0); // 确保时间戳大于0

    // 测试时间格式转换
    std::string format = "%FT%T";
    EXPECT_NO_THROW(dt.ToFormat(format));

    // 测试时间比较运算符
    zrt::DateTime<zrt::kUTC> dt2;
    EXPECT_EQ(dt <= dt2, true);
    dt.OffsetSec(1);
    EXPECT_EQ(dt > dt2, true);
}



// 测试解析函数
TEST_F(ZrtTimeF, ParseFunctions) {
    // 测试时间解析
    std::tm tm = zrt::ParseTimeStr(sec_time_str_local, sec_format);
    EXPECT_EQ(tm.tm_year + 1900, 2023);
    EXPECT_EQ(tm.tm_mon + 1, 10);
    EXPECT_EQ(tm.tm_mday, 1);

    // 测试HMS提取
    int hms = zrt::GetHMS(sec_time_str_local, sec_format);
    EXPECT_EQ(hms, 195456);

    // 测试日期提取
    int ymd = zrt::GetYmd(sec_time_str_local, sec_format);
    EXPECT_EQ(ymd, 20231001);
}

// 测试GetYmdHMS函数
TEST_F(ZrtTimeF, GetYmdHMSFunction) {
    // 测试基本功能 - UTC时间
    zrt::DateTime<zrt::kUTC> dt_utc(sec_epoch13 * 1000000, 19);
    int64_t ymd_hms_utc = dt_utc.GetYmdHMS();
    EXPECT_EQ(ymd_hms_utc, 20231001115456);

    // 测试基本功能 - 本地时间
    zrt::DateTime<zrt::kLocal> dt_local(sec_epoch13 * 1000000, 19);
    int64_t ymd_hms_local = dt_local.GetYmdHMS();
    EXPECT_EQ(ymd_hms_local, 20231001195456);

    // 验证GetYmdHMS与GetYmd、GetHMS的一致性
    int ymd = dt_utc.GetYmd();
    int hms = dt_utc.GetHMS();
    int64_t expected_ymd_hms = static_cast<int64_t>(ymd) * 1000000 + hms;
    EXPECT_EQ(ymd_hms_utc, expected_ymd_hms);
}

// 测试GetYmdHMS边界情况
TEST_F(ZrtTimeF, GetYmdHMSEdgeCases) {
    // 测试午夜时刻 (00:00:00)
    zrt::DateTime<zrt::kUTC> dt_midnight;
    dt_midnight.SetYear(2023).SetMon(1).SetDay(1)
               .SetHour(0).SetMin(0).SetSec(0);
    EXPECT_EQ(dt_midnight.GetYmdHMS(), 20230101000000);

    // 测试接近午夜前 (23:59:59)
    zrt::DateTime<zrt::kUTC> dt_before_midnight;
    dt_before_midnight.SetYear(2023).SetMon(12).SetDay(31)
                      .SetHour(23).SetMin(59).SetSec(59);
    EXPECT_EQ(dt_before_midnight.GetYmdHMS(), 20231231235959);

    // 测试闰年日期 (2020-02-29)
    zrt::DateTime<zrt::kUTC> dt_leap_day(1582934400, 10); // 2020-02-29 00:00:00
    int64_t leap_ymd_hms = dt_leap_day.GetYmdHMS();
    int ymd_part = static_cast<int>(leap_ymd_hms / 1000000);
    EXPECT_EQ(ymd_part, 20200229);

    // 测试月末到月初的跨越
    zrt::DateTime<zrt::kUTC> dt_month_end(1698796799, 10); // 2023-10-31 23:59:59
    int64_t before_next_month = dt_month_end.GetYmdHMS();
    dt_month_end.OffsetSec(1); // 变成 2023-11-01 00:00:00
    int64_t after_next_month = dt_month_end.GetYmdHMS();
    EXPECT_EQ(before_next_month, 20231031235959);
    EXPECT_EQ(after_next_month, 20231101000000);
}

// 测试GetYmdHMS与字符串格式化的一致性
TEST_F(ZrtTimeF, GetYmdHMSConsistency) {
    zrt::DateTime<zrt::kUTC> dt;
    dt.SetYear(2025).SetMon(6).SetDay(15)
      .SetHour(14).SetMin(30).SetSec(45);

    // 获取YmdHMS
    int64_t ymd_hms = dt.GetYmdHMS();
    EXPECT_EQ(ymd_hms, 20250615143045);

    // 验证格式化字符串的一致性
    std::string formatted = dt.ToFormat("%Y%m%d%H%M%S");
    int64_t parsed_ymd_hms = std::stoll(formatted);
    EXPECT_EQ(ymd_hms, parsed_ymd_hms);

    // 验证各个部分
    int ymd = static_cast<int>(ymd_hms / 1000000);
    int hms = static_cast<int>(ymd_hms % 1000000);
    EXPECT_EQ(ymd, 20250615);
    EXPECT_EQ(hms, 143045);
    EXPECT_EQ(dt.GetYmd(), ymd);
    EXPECT_EQ(dt.GetHMS(), hms);
}

// 测试Timer类
TEST_F(ZrtTimeF, TimerFunction) {
    zrt::Timer timer("TestTimer");
    // 模拟耗时操作
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    timer.print();

    // 测试特定时间节点
    long start_time = zrt::get_epoch19();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    long end_time = zrt::get_epoch19();
    zrt::log_epoch19(end_time - start_time, "TimeDifference");
}

// 测试边界情况和错误处理
TEST_F(ZrtTimeF, EdgeCases) {
    // 测试无效时间字符串
    std::string invalid_str = "invalid-time";
    std::tm tm = zrt::ParseTimeStr(invalid_str, "%Y-%m-%d");
    EXPECT_FALSE(tm.tm_year); // 默认值

    // 测试0时间戳
    zrt::DateTime<zrt::kUTC> dt(0);
    EXPECT_EQ(dt.Epoch10(), 0);

    // 测试时间溢出
    zrt::DateTime<zrt::kUTC> max_dt;
    max_dt.OffsetNano(std::numeric_limits<int64_t>::max());
    EXPECT_NE(max_dt.Epoch19(), 0);
}

// 测试时间操作方法链式调用
TEST_F(ZrtTimeF, MethodChaining) {
    zrt::DateTime<zrt::kUTC> dt;
    dt.OffsetDay(1).OffsetHour(2).OffsetMin(30);
    EXPECT_EQ(dt.DiffSec(zrt::DateTime<zrt::kUTC>()), 86400 + 2*3600 + 30*60);
}

// 测试DateTime类的完整生命周期
TEST_F(ZrtTimeF, DateTimeLifecycle) {
    zrt::DateTime<zrt::kUTC> dt1;
    zrt::DateTime<zrt::kUTC> dt2(dt1.Epoch19(), 19);
    EXPECT_EQ(dt1 == dt2, true);

    dt1.SetHour(12).SetMin(30).SetSec(45);
    EXPECT_EQ(dt1.GetHMS(), 123045);

    dt1.SetNano(123456789);
    // EXPECT_EQ(dt1.ToFormat("%f", 9), "123456789");
}

// 测试不同精度的时间差异计算
TEST_F(ZrtTimeF, TimeDifference) {
    zrt::DateTime<zrt::kUTC> now;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    zrt::DateTime<zrt::kUTC> later;

    EXPECT_THAT(later.DiffSec(now), ::testing::AnyOf(0l, 1l)); // 秒级差异可能为0
    EXPECT_GT(later.DiffMill(now), 45); // 毫秒级差异应大于45
    EXPECT_GT(later.DiffNano(now), 45000000); // 纳秒级差异应大于45,000,000
}

// 测试时间格式化选项
TEST_F(ZrtTimeF, FormattingOptions) {
    zrt::DateTime<zrt::kUTC> dt;
    EXPECT_NO_THROW(dt.ToFormat("%Y-%m-%d %H:%M:%S"));
    EXPECT_NO_THROW(dt.ToFormat("%FT%T.%f", 6));
    EXPECT_NO_THROW(dt.ToFormat("%s", 0));
}

// 测试时区处理
TEST_F(ZrtTimeF, TimeZoneHandling) {
    zrt::DateTime<zrt::kUTC> utc_dt;
    zrt::DateTime<zrt::kLocal> local_dt;

    // 不同时区的epoch是相等的（同一时刻）
    EXPECT_EQ(utc_dt.Epoch10(), local_dt.Epoch10());

    // 验证时区名称可以被格式化（不比较具体值，因为环境相关）
    std::string utc_str = utc_dt.ToFormat("%Z");
    std::string local_str = local_dt.ToFormat("%Z");
    EXPECT_NE(utc_str, local_str);
}



// 验证固定时间点的格式化输出
TEST_F(ZrtTimeF, FixedTimeFormatting) {
    zrt::DateTime<zrt::kUTC> dt(sec_epoch13 * 1000000, 19); // 转换为19位纳秒时间戳
    EXPECT_EQ(dt.ToFormat("%Y-%m-%d %H:%M:%S"), sec_time_str_utc);
    EXPECT_EQ(dt.ToFormat("%Y%m%d"), "20231001");
    EXPECT_EQ(dt.GetHMS(), 115456);
}

// 测试DateTime比较运算符（使用确定时间点）
TEST_F(ZrtTimeF, DateTimeComparison) {
    zrt::DateTime<zrt::kUTC> dt1(sec_epoch13 * 1000000);
    zrt::DateTime<zrt::kUTC> dt2(sec_epoch13 * 1000000 + 5000000); // +5ms
    EXPECT_EQ(dt1, dt1);
    EXPECT_LT(dt1, dt2);
    EXPECT_GT(dt2, dt1);
}

// 测试时区转换逻辑
TEST_F(ZrtTimeF, TimeZoneConversion) {
    zrt::DateTime<zrt::kUTC> utc_dt(sec_epoch13 * 1000000);
    zrt::DateTime<zrt::kLocal> local_dt(sec_epoch13 * 1000000);

    // 验证时间戳相同（同一时刻）
    EXPECT_EQ(utc_dt.Epoch10(), local_dt.Epoch10());

    // 验证时区字符串差异
    EXPECT_EQ(utc_dt.ToFormat("%Z"), "UTC");
    EXPECT_EQ(local_dt.ToFormat("%Z"), "CST"); // 假设本地时区不是UTC
}

// 增强版边界测试
TEST_F(ZrtTimeF, AdvancedEdgeCases) {
    // 测试闰年
    zrt::DateTime<zrt::kUTC> leapDay(1582934400000000000, 19); // 2020-02-29 00:00:00
    EXPECT_EQ(leapDay.ToFormat("%Y-%m-%d"), "2020-02-29");

    // 测试月末
    zrt::DateTime<zrt::kUTC> monthEnd(1698796799); // 2023-10-31 23:59:59
    monthEnd.OffsetSec(1);
    EXPECT_EQ(monthEnd.ToFormat("%Y-%m-%d"), "2023-11-01");
}

// 增强版解析功能测试
TEST_F(ZrtTimeF, ParseValidation) {
    // 有效解析测试
    auto valid_tm = zrt::ParseTimeStr("2023-10-01", "%Y-%m-%d");
    EXPECT_EQ(valid_tm.tm_year, 2023-1900);
    EXPECT_EQ(valid_tm.tm_mon, 10-1);
    EXPECT_EQ(valid_tm.tm_mday, 1);

    // 无效解析测试
    auto invalid_tm = zrt::ParseTimeStr("invalid", "%Y-%m-%d");
    EXPECT_EQ(invalid_tm.tm_year, 0); // 根据实现可能需要调整
}

// 增强版时间差测试（带误差容限）
TEST_F(ZrtTimeF, TimeDifferenceWithTolerance) {
    zrt::DateTime<zrt::kUTC> start;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    zrt::DateTime<zrt::kUTC> end;

    long diff = end.DiffMill(start);
    EXPECT_GE(diff, 95);   // 允许5ms误差下限
    EXPECT_LE(diff, 150);  // 允许50ms误差上限
}

// 测试时间间隔
TEST_F(ZrtTimeF, DaylightSavingTime) {
    zrt::DateTime<> preDst(1678593600);
    zrt::DateTime<> postDst(1678597200);
    EXPECT_EQ(preDst.ToFormat("%Z"), "CST");
    EXPECT_EQ(postDst.ToFormat("%Z"), "CST");
    EXPECT_EQ(postDst.DiffSec(preDst), 3600); // 时钟向前跳1小时
}

// 测试完整的日期时间组件设置
TEST_F(ZrtTimeF, ComponentSettings) {
    zrt::DateTime<zrt::kUTC> dt;
    dt.SetYear(2025)
      .SetMon(12)
      .SetDay(31)
      .SetHour(23)
      .SetMin(59)
      .SetSec(59)
      .SetNano(999999999);

    EXPECT_EQ(dt.ToFormat("%Y-%m-%d %H:%M:%S"), "2025-12-31 23:59:59");
    EXPECT_EQ(dt.ToFormat("%Y-%m-%d %H:%M:%S.%f"), "2025-12-31 23:59:59.999");
    EXPECT_EQ(dt.ToFormat("%Y-%m-%d %H:%M:%S.%f", 6), "2025-12-31 23:59:59.999999");
    EXPECT_EQ(dt.ToFormat("%Y-%m-%d %H:%M:%S.%fA", 6), "2025-12-31 23:59:59.999999A");
    zrt::DateTime<zrt::kUTC> dt2(dt.Epoch19(), 19);
    // EXPECT_EQ(dt.ToFormat("%f", 3), "999");
}

TEST(ZrtTime, StringInitialization) {
    zrt::DateTime<zrt::kUTC> dt("2023-01-01 14:34:56.789", "%Y-%m-%d %H:%M:%S.%f");
    EXPECT_EQ(dt.Epoch13(), 1672583696789);  // 已知UTC时间戳

    std::string src_time_str = "2025-12-31 23:59:59.999Z";
    std::string src_fmt = "%Y-%m-%d %H:%M:%S.%fZ";
    EXPECT_EQ(zrt::DateTimeUTC(src_time_str, src_fmt).ToFormat(
        src_fmt), src_time_str);

    src_time_str = "2025-12-31 23:59:59.999";
    src_fmt = "%Y-%m-%d %H:%M:%S.%f";
    EXPECT_EQ(zrt::DateTimeUTC(src_time_str, src_fmt).ToFormat(
    src_fmt), src_time_str);

    src_time_str = "2025-12-31 23:59:59";
    src_fmt = "%Y-%m-%d %H:%M:%S";
    EXPECT_EQ(zrt::DateTimeUTC(src_time_str, src_fmt).ToFormat(
    src_fmt), src_time_str);

    // EXPECT_EQ(dt.ToFormat("%f", 3), "999");
}