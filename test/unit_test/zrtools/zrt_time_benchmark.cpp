//
// Performance benchmark for civil_from_epoch_utc vs gmtime_r
//

#include <ctime>
#include <chrono>
#include <iostream>
#include <vector>
#include <random>
#include <gtest/gtest.h>
#include "zrtools/zrt_time-inl.h"

namespace {

// 用于对比的原始 gmtime_r 实现
std::tm gmtime_r_baseline(const time_t epoch) {
    std::tm tm{};
    ::gmtime_r(&epoch, &tm);
    return tm;
}

// 测试用例：覆盖各种边界情况
struct TestCase {
    time_t epoch;
    std::string description;
};

std::vector<TestCase> GenerateTestCases() {
    return {
        {0, "Unix Epoch (1970-01-01 00:00:00)"},
        {1696161296, "Regular date (2023-10-01 11:54:56)"},
        {1582934400, "Leap day (2020-02-29 00:00:00)"},
        {-86400, "Before epoch (1969-12-31 00:00:00)"},
        {2147483647, "32-bit max (2038-01-19 03:14:07)"},
        {946684800, "Y2K (2000-01-01 00:00:00)"},
        {1735689599, "End of 2024 (2024-12-31 23:59:59)"},
        {253402300799, "End of 9999 (9999-12-31 23:59:59)"},
    };
}

// 生成随机测试数据
std::vector<time_t> GenerateRandomEpochs(size_t count) {
    std::vector<time_t> epochs;
    epochs.reserve(count);

    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<time_t> dis(0, 2147483647);  // 1970-2038范围

    for (size_t i = 0; i < count; ++i) {
        epochs.push_back(dis(gen));
    }
    return epochs;
}

}  // namespace

// 正确性验证：对比两种实现的结果
TEST(ZrtTimeBenchmark, CorrectnessVerification) {
    const auto test_cases = GenerateTestCases();

    for (const auto& tc : test_cases) {
        SCOPED_TRACE(tc.description);

        // 新实现
        const std::tm tm_new = zrt::detail::civil_from_epoch_utc(tc.epoch);

        // 旧实现 (gmtime_r)
        const std::tm tm_old = gmtime_r_baseline(tc.epoch);

        // 验证关键字段
        EXPECT_EQ(tm_new.tm_year, tm_old.tm_year) << "Year mismatch";
        EXPECT_EQ(tm_new.tm_mon, tm_old.tm_mon) << "Month mismatch";
        EXPECT_EQ(tm_new.tm_mday, tm_old.tm_mday) << "Day mismatch";
        EXPECT_EQ(tm_new.tm_hour, tm_old.tm_hour) << "Hour mismatch";
        EXPECT_EQ(tm_new.tm_min, tm_old.tm_min) << "Minute mismatch";
        EXPECT_EQ(tm_new.tm_sec, tm_old.tm_sec) << "Second mismatch";
        EXPECT_EQ(tm_new.tm_wday, tm_old.tm_wday) << "Weekday mismatch";
        EXPECT_EQ(tm_new.tm_yday, tm_old.tm_yday) << "Year day mismatch";
    }
}

// 大规模随机数据正确性测试
TEST(ZrtTimeBenchmark, RandomCorrectnessTest) {
    const auto epochs = GenerateRandomEpochs(10000);

    size_t mismatch_count = 0;
    for (const auto epoch : epochs) {
        const std::tm tm_new = zrt::detail::civil_from_epoch_utc(epoch);
        const std::tm tm_old = gmtime_r_baseline(epoch);

        if (tm_new.tm_year != tm_old.tm_year ||
            tm_new.tm_mon != tm_old.tm_mon ||
            tm_new.tm_mday != tm_old.tm_mday ||
            tm_new.tm_hour != tm_old.tm_hour ||
            tm_new.tm_min != tm_old.tm_min ||
            tm_new.tm_sec != tm_old.tm_sec) {
            ++mismatch_count;

            // 输出第一个不匹配的详细信息
            if (mismatch_count == 1) {
                SPDLOG_ERROR("First mismatch at epoch {}", epoch);
                SPDLOG_ERROR("New: {}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                    tm_new.tm_year + 1900, tm_new.tm_mon + 1, tm_new.tm_mday,
                    tm_new.tm_hour, tm_new.tm_min, tm_new.tm_sec);
                SPDLOG_ERROR("Old: {}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                    tm_old.tm_year + 1900, tm_old.tm_mon + 1, tm_old.tm_mday,
                    tm_old.tm_hour, tm_old.tm_min, tm_old.tm_sec);
            }
        }
    }

    EXPECT_EQ(mismatch_count, 0) << "Found " << mismatch_count << " mismatches in 10000 random epochs";
}

// 性能对比：civil_from_epoch_utc vs gmtime_r
TEST(ZrtTimeBenchmark, PerformanceComparison) {
    constexpr size_t ITERATIONS = 1000000;  // 100万次迭代
    const auto epochs = GenerateRandomEpochs(ITERATIONS);

    // 预热 CPU 缓存
    volatile int warmup = 0;
    for (size_t i = 0; i < 1000; ++i) {
        warmup += zrt::detail::civil_from_epoch_utc(epochs[i % epochs.size()]).tm_year;
    }

    // 测试新实现 (civil_from_epoch_utc)
    auto start_new = std::chrono::high_resolution_clock::now();
    volatile int result_new = 0;
    for (const auto epoch : epochs) {
        const std::tm tm = zrt::detail::civil_from_epoch_utc(epoch);
        result_new += tm.tm_year;  // 防止优化掉
    }
    auto end_new = std::chrono::high_resolution_clock::now();
    const auto duration_new = std::chrono::duration_cast<std::chrono::microseconds>(end_new - start_new).count();

    // 测试旧实现 (gmtime_r)
    auto start_old = std::chrono::high_resolution_clock::now();
    volatile int result_old = 0;
    for (const auto epoch : epochs) {
        const std::tm tm = gmtime_r_baseline(epoch);
        result_old += tm.tm_year;  // 防止优化掉
    }
    auto end_old = std::chrono::high_resolution_clock::now();
    const auto duration_old = std::chrono::duration_cast<std::chrono::microseconds>(end_old - start_old).count();

    // 输出结果
    const double speedup = static_cast<double>(duration_old) / duration_new;
    std::cout << "\n=== Performance Benchmark Results ===\n";
    std::cout << "Iterations: " << ITERATIONS << "\n";
    std::cout << "civil_from_epoch_utc: " << duration_new << " μs ("
              << (duration_new / static_cast<double>(ITERATIONS)) << " μs/call)\n";
    std::cout << "gmtime_r:             " << duration_old << " μs ("
              << (duration_old / static_cast<double>(ITERATIONS)) << " μs/call)\n";
    std::cout << "Speedup:              " << speedup << "x\n";
    std::cout << "=====================================\n\n";

    // 验证性能提升 (预期至少 1.0x，即不慢于 gmtime_r)
    // 注意：实际加速比可能因环境而异，保守期望不慢于标准库
    EXPECT_GT(speedup, 1.0) << "New implementation should not be slower than gmtime_r";
}

// DateTime<kUTC> 端到端性能测试
TEST(ZrtTimeBenchmark, DateTimeUTCPerformance) {
    constexpr size_t ITERATIONS = 100000;
    const auto epochs = GenerateRandomEpochs(ITERATIONS);

    auto start = std::chrono::high_resolution_clock::now();
    volatile int result = 0;
    for (const auto epoch : epochs) {
        zrt::DateTimeUTC dt(epoch);
        result += dt.GetYmd();  // 触发 UpdateTM() -> civil_from_epoch_utc
    }
    auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "\n=== DateTime<kUTC>::GetYmd() Performance ===\n";
    std::cout << "Iterations: " << ITERATIONS << "\n";
    std::cout << "Total time: " << duration << " μs\n";
    std::cout << "Per call:   " << (duration / static_cast<double>(ITERATIONS)) << " μs\n";
    std::cout << "==========================================\n\n";
}

// ToFormat() 性能测试
TEST(ZrtTimeBenchmark, ToFormatPerformance) {
    constexpr size_t ITERATIONS = 10000;
    const auto epochs = GenerateRandomEpochs(ITERATIONS);

    auto start = std::chrono::high_resolution_clock::now();
    std::string result;
    for (const auto epoch : epochs) {
        zrt::DateTimeUTC dt(epoch);
        result = dt.ToFormat("%Y-%m-%d %H:%M:%S");
    }
    auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "\n=== DateTime<kUTC>::ToFormat() Performance ===\n";
    std::cout << "Iterations: " << ITERATIONS << "\n";
    std::cout << "Total time: " << duration << " μs\n";
    std::cout << "Per call:   " << (duration / static_cast<double>(ITERATIONS)) << " μs\n";
    std::cout << "===========================================\n\n";
}

// 边界情况专项测试
TEST(ZrtTimeBenchmark, EdgeCaseCorrectness) {
    // 负数时间戳
    time_t epoch = -86400;  // 1969-12-31 00:00:00
    std::tm tm_new = zrt::detail::civil_from_epoch_utc(epoch);
    std::tm tm_old = gmtime_r_baseline(epoch);
    EXPECT_EQ(tm_new.tm_year, tm_old.tm_year);
    EXPECT_EQ(tm_new.tm_mon, tm_old.tm_mon);
    EXPECT_EQ(tm_new.tm_mday, tm_old.tm_mday);

    // 闰年2月29日
    epoch = 1582934400;  // 2020-02-29 00:00:00
    tm_new = zrt::detail::civil_from_epoch_utc(epoch);
    tm_old = gmtime_r_baseline(epoch);
    EXPECT_EQ(tm_new.tm_mon, 1);  // February (0-indexed)
    EXPECT_EQ(tm_new.tm_mday, 29);
    EXPECT_EQ(tm_new.tm_yday, tm_old.tm_yday);

    // 世纪交界
    epoch = 946684800;  // 2000-01-01 00:00:00
    tm_new = zrt::detail::civil_from_epoch_utc(epoch);
    tm_old = gmtime_r_baseline(epoch);
    EXPECT_EQ(tm_new.tm_year, 100);  // 2000 - 1900
    EXPECT_EQ(tm_new.tm_wday, tm_old.tm_wday);
}
