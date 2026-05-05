#ifdef __linux__

#pragma once

#include <string>
#include <chrono>
#include <sys/time.h>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <boost/stacktrace.hpp>
#include <boost/date_time.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/time_facet.hpp>
#include <sstream>
#include "spdlog/spdlog.h"
#include "zrtools/zrt_cpp14_compat.h"

namespace zrt {

    INLINE_VAR constexpr int64_t kKilo = 1000;
    INLINE_VAR constexpr int64_t kMega = kKilo * 1000;
    INLINE_VAR constexpr int64_t kGiga = kMega * 1000;

    inline double get_epoch()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count() / 1e9;
    }

    inline long get_epoch19()
    {
        timespec ts {};
        clock_gettime(CLOCK_REALTIME, &ts);
        return ts.tv_sec * kGiga + ts.tv_nsec;
    }

    inline long get_epoch16()
    {
        return get_epoch19() / kKilo;
    }

    inline long get_epoch13()
    {
        return get_epoch19() / kMega;
    }

    inline long get_epoch10()
    {
        return get_epoch19() / kGiga;
    }

    template<typename T>
    inline long get_epoch()
    {
        return std::chrono::duration_cast<T>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }

    inline double time_now() {
        return get_epoch();
    }

    // 时间字符串转换成13位epoch
    static int64_t get_epoch13(const std::string& time_str, const std::string& format)
    {
        if (time_str.empty()) {
            return 0;
        }
        struct tm tm{};
        strptime(time_str.c_str(), format.c_str(), &tm);
        int64_t ft = mktime(&tm);
        char delimiter = format[format.find_last_of("%f")-2];
        int mill = std::stoi(time_str.substr(time_str.find_last_of(delimiter) + 1));
        return ft * 1000 + mill;
    }

    inline uint64_t rdtsc() {
        uint32_t low, high;
        __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
        return static_cast<uint64_t> (high) << 32 | low;
    }

    inline int64_t get_monotonic19()
    {
        timespec ts {};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * kGiga + ts.tv_nsec;
    }

    // 10位epoch转换成时间字符串
    std::string get_time_str_local(int64_t epoch10, const std::string& format="%FT%T");
    // 获取当前时间的时间字符串, 不带毫秒
    inline std::string get_time_str_local(const std::string& format="%FT%T") {
        return zrt::get_time_str_local(zrt::get_epoch10(), format);
    }
    std::string GetTimeStrUTC10(int64_t epoch, const std::string& format="%Y-%m-%dT%H:%M:%SZ");
    inline std::string GetTimeStrUTC10(const std::string& format="%Y-%m-%dT%H:%M:%SZ") {
        return zrt::GetTimeStrUTC10(zrt::get_epoch10(), format);
    }
    // 获取UTC字符串, 精度最高微秒
    std::string GetTimeStrUTC(const std::string& format="%Y-%m-%dT%H:%M:%SZ", int decimal_places=6);

    inline std::tm ParseTimeStr(const std::string& time_str, const std::string& format) {
        if (time_str.empty()) {
            return {};
        }
        std::tm tm {};
        std::istringstream ss(time_str);
        ss >> std::get_time(&tm, format.c_str());
        if (ss.fail()) {
            SPDLOG_ERROR("parse time_str({}) with format({}) failed", time_str, format);
            return {};
        }
        return tm;
    }

    inline int GetHMS(const std::string& time_str, const std::string& format) {
        std::tm tm = ParseTimeStr(time_str, format);
        return tm.tm_hour * 10000 + tm.tm_min * 100 + tm.tm_sec;
    }

    inline int GetHMS() {
        std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm {};
        ::localtime_r(&now, &tm);
        return tm.tm_hour * 10000 + tm.tm_min * 100 + tm.tm_sec;
    }

    inline int GetYmd(const std::string& time_str, const std::string& format) {
        std::tm tm = ParseTimeStr(time_str, format);
        return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
    }

    inline int GetYmd() {
        std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm {};
        ::localtime_r(&now, &tm);
        return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
    }

    //inline void log_epoch19(const std::string& id) {
    //    long now = get_epoch<std::chrono::nanoseconds>();
    //    SPDLOG_ERROR("[LogTime] ({}) {}", id, now);
    //}

    inline void log_epoch19(long now, const std::string& id) {
        SPDLOG_ERROR("[LogTime] ({}) {}", id, now);
    }

    class Timer
    {
    public:
        explicit Timer(const std::string& func_name="timer"): m_func_name(func_name) {
            reset();
        }
        ~Timer() {
            double time_diff = get_diff();
            SPDLOG_INFO("[{}] time cost: {:.6f}s", m_func_name, time_diff);
        }
        double get_diff() const {
            return zrt::time_now() - m_start;
        }
        void print() const {
            double time_diff = get_diff();
            SPDLOG_INFO("[{}] time cost: {:.6f}s", m_func_name, time_diff);
        }
        void print(const std::string& msg) const {
            double time_diff = get_diff();
            SPDLOG_INFO("[{}] time cost: {:.6f}s", msg, time_diff);
        }
        void reset() {
            m_start = zrt::time_now();
        }
    private:
        double m_start {};
        std::string m_func_name {};
    };

    enum EmTimeZone {
        kUTC,
        kLocal,
    };

    // Howard Hinnant's date algorithms (C++17 compatible)
    // 参考: https://howardhinnant.github.io/date_algorithms.html
    namespace detail {
        // 反向算法：日期 -> 天数（从 Unix epoch 起）
        constexpr int64_t days_from_civil(int y, unsigned m, unsigned d) noexcept {
            y -= m <= 2;
            const int64_t era = (y >= 0 ? y : y - 399) / 400;
            const unsigned yoe = static_cast<unsigned>(y - era * 400);             // [0, 399]
            const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  // [0, 365]
            const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           // [0, 146096]
            return era * 146097 + static_cast<int64_t>(doe) - 719468;
        }

        // 优化的 timegm 实现（struct tm -> epoch）
        inline time_t fast_timegm(const std::tm& tm) noexcept {
            // 1. 计算天数
            int year = tm.tm_year + 1900;
            unsigned month = tm.tm_mon + 1;  // tm_mon 是 [0, 11]
            unsigned day = tm.tm_mday;

            int64_t days = days_from_civil(year, month, day);

            // 2. 计算秒数
            int64_t seconds = days * 86400;
            seconds += tm.tm_hour * 3600;
            seconds += tm.tm_min * 60;
            seconds += tm.tm_sec;

            return static_cast<time_t>(seconds);
        }

        // 正向算法：epoch -> 日期（原有实现）
        inline std::tm civil_from_epoch_utc(const time_t epoch) noexcept {
            std::tm tm{};

            // 1. 提取秒内的时分秒 (简单除法/取模)
            const int64_t z = epoch;
            const int64_t day_seconds = z % 86400;
            tm.tm_hour = static_cast<int>(day_seconds / 3600);
            tm.tm_min = static_cast<int>((day_seconds % 3600) / 60);
            tm.tm_sec = static_cast<int>(day_seconds % 60);

            // 2. 计算从Unix epoch (1970-01-01) 起的天数
            int64_t days = z / 86400;
            if (z < 0 && day_seconds != 0) {
                --days;  // 处理负数时间戳
            }

            // 3. Howard Hinnant算法: 天数 -> 年月日
            // 调整到公历纪元 (0000-03-01 作为起点)
            days += 719468;  // Unix epoch 到 0000-03-01 的天数

            const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
            const uint32_t doe = static_cast<uint32_t>(days - era * 146097);       // [0, 146096] 纪元内天数
            const uint32_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;  // [0, 399] 纪元内年份
            const int64_t y = static_cast<int64_t>(yoe) + era * 400;
            const uint32_t doy = doe - (365*yoe + yoe/4 - yoe/100);                // [0, 365] 年内天数
            const uint32_t mp = (5*doy + 2)/153;                                   // [0, 11] 月份(3月起算)

            tm.tm_mday = static_cast<int>(doy - (153*mp+2)/5 + 1);                 // [1, 31]
            tm.tm_mon = static_cast<int>(mp < 10 ? mp + 2 : mp - 10);              // [0, 11]
            tm.tm_year = static_cast<int>(y + (tm.tm_mon <= 1)) - 1900;            // 1900年起算

            // 4. 计算星期 (0=Sunday)
            // Unix epoch (1970-01-01) 是周四(4), 需要使用原始天数计算
            const int64_t orig_days = z / 86400;  // 从Unix epoch起的天数
            tm.tm_wday = static_cast<int>((orig_days + 4) % 7);  // +4 因为epoch是周四
            if (tm.tm_wday < 0) tm.tm_wday += 7;

            // 5. 计算年内天数 (0-365)
            // 重新计算从1月1日起的天数
            const int is_leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0;
            const int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
            tm.tm_yday = month_days[tm.tm_mon] + tm.tm_mday - 1;
            if (tm.tm_mon > 1) tm.tm_yday += is_leap;  // 3月后考虑闰年

            tm.tm_isdst = 0;  // UTC无夏令时

#ifdef __USE_MISC  // GNU 扩展
            tm.tm_zone = "UTC";
            tm.tm_gmtoff = 0;
#endif

            return tm;
        }
    }

    template<EmTimeZone tz> struct TMUpdater;
    template<>
    struct TMUpdater<kLocal> {
        static std::tm* update(const time_t& tv_sec, std::tm& tm) {
            return localtime_r(&tv_sec, &tm);
        }
    };
    template<>
    struct TMUpdater<kUTC> {
        static std::tm* update(const time_t& tv_sec, std::tm& tm) {
            // 使用优化的算法替代 gmtime_r
            tm = detail::civil_from_epoch_utc(tv_sec);
            return &tm;
        }
    };

    // TSUpdater: 反向转换 (tm -> epoch)
    template<EmTimeZone tz> struct TSUpdater;
    template<>
    struct TSUpdater<kLocal> {
        static time_t update(const std::tm& tm) {
            std::tm tm_copy = tm;  // mktime 会修改输入
            return mktime(&tm_copy);
        }
    };
    template<>
    struct TSUpdater<kUTC> {
        static time_t update(const std::tm& tm) {
            // 使用优化的算法替代 timegm
            return detail::fast_timegm(tm);
        }
    };

    template<EmTimeZone tz=kLocal>
    class DateTime {
    public:
        DateTime() {
            clock_gettime(CLOCK_REALTIME, &m_ts);
        }

        DateTime(std::string time_str, std::string format);

        explicit DateTime(const int64_t epoch10) {
            m_ts.tv_sec = epoch10;
            m_ts.tv_nsec = 0;
        }
        DateTime(const int64_t epoch, const int digits);

        bool operator<(const DateTime& rhs) const {return Epoch19() < rhs.Epoch19();}
        bool operator>(const DateTime& rhs) const {return Epoch19() > rhs.Epoch19();}
        bool operator==(const DateTime& rhs) const {return Epoch19() == rhs.Epoch19();}
        bool operator<=(const DateTime& rhs) const {return Epoch19() <= rhs.Epoch19();}
        bool operator>=(const DateTime& rhs) const {return Epoch19() >= rhs.Epoch19();}

        int64_t Epoch19() const {return m_ts.tv_sec * kGiga + m_ts.tv_nsec;}
        int64_t Epoch16() const {return Epoch19() / 1000;}
        int64_t Epoch13() const {return Epoch19() / 1000 / 1000;}
        int64_t Epoch10() const {return m_ts.tv_sec;}
        double EpochD() const {return m_ts.tv_sec + m_ts.tv_nsec / 1e9;}

        std::string ToFormat(const std::string& format, const uint decimal_places);
        std::string ToFormat(const std::string& format) {return ToFormat(format, 3);}
        std::string ToFormat(const uint decimal_places) {return ToFormat("%FT%T.%f", decimal_places);}
        std::string ToFormat() {return ToFormat("%FT%T.%f", 3);}

        inline int GetHMS() {
            UpdateTM();
            return m_tm.tm_hour * 10000 + m_tm.tm_min * 100 + m_tm.tm_sec;
        }

        inline int GetYmd() {
            UpdateTM();
            return (m_tm.tm_year + 1900) * 10000 + (m_tm.tm_mon + 1) * 100 + m_tm.tm_mday;
        }

        inline int64_t GetYmdHMS() {
            UpdateTM();
            return (static_cast<int64_t>(m_tm.tm_year + 1900) * 10000 + (m_tm.tm_mon + 1) * 100 + m_tm.tm_mday) * zrt::kMega
            + (m_tm.tm_hour * 10000 + m_tm.tm_min * 100 + m_tm.tm_sec);
        }

        DateTime& SetYear(const int year) {
            UpdateTM();
            m_tm.tm_year = year - 1900;
            UpdateEpoch();
            return *this;
        }

        DateTime& SetMon(const int mon) {
            UpdateTM();
            m_tm.tm_mon = mon - 1;
            UpdateEpoch();
            return *this;
        }

        DateTime& SetDay(const int day) {
            UpdateTM();
            m_tm.tm_mday = day;
            UpdateEpoch();
            return *this;
        }

        DateTime& SetHour(const int hour) {
            UpdateTM();
            m_tm.tm_hour = hour;
            UpdateEpoch();
            return *this;
        }

        DateTime& SetMin(const int min) {
            UpdateTM();
            m_tm.tm_min = min;
            UpdateEpoch();
            return *this;
        }

        DateTime& SetSec(const int sec) {
            UpdateTM();
            m_tm.tm_sec = sec;
            UpdateEpoch();
            return *this;
        }

        DateTime& SetNano(const int64_t nano) {
            m_ts.tv_nsec = nano;
            return *this;
        }

        DateTime& OffsetDay(const int64_t day) {
            m_ts.tv_sec += day * 86400;
            m_cached_epoch = -1;  // 使缓存失效
            return *this;
        }

        DateTime& OffsetHour(const int64_t hour) {
            m_ts.tv_sec += hour * 3600;
            m_cached_epoch = -1;  // 使缓存失效
            return *this;
        }

        DateTime& OffsetMin(const int64_t min) {
            m_ts.tv_sec += min * 60;
            m_cached_epoch = -1;  // 使缓存失效
            return *this;
        }

        DateTime& OffsetSec(const int64_t sec) {
            m_ts.tv_sec += sec;
            m_cached_epoch = -1;  // 使缓存失效
            return *this;
        }

        DateTime& OffsetMSec(const int64_t ms) {
            return OffsetNano(ms * 1000 * 1000);
        }

        DateTime& OffsetUSec(const int64_t us) {
            return OffsetNano(us * 1000);
        }

        DateTime& OffsetNano(const int64_t nano) {
            const int64_t time = Epoch19() + nano;
            m_ts.tv_sec = time / kGiga;
            m_ts.tv_nsec = time % kGiga;
            m_cached_epoch = -1;  // 使缓存失效
            return *this;
        }

        int64_t DiffSec(const DateTime& other) const {
            return Epoch10() - other.Epoch10();
        }

        int64_t DiffMill(const DateTime& other) const {
            return Epoch13() - other.Epoch13();
        }

        int64_t DiffMicro(const DateTime& other) const {
            return Epoch16() - other.Epoch16();
        }

        int64_t DiffNano(const DateTime& other) const {
            return Epoch19() - other.Epoch19();
        }

        double DiffD(const DateTime& other) const {
            return EpochD() - other.EpochD();
        }

    protected:
        ::timespec m_ts {};
        std::tm m_tm {};
        time_t m_cached_epoch {-1};  // 缓存上次转换的 epoch

    private:
        void UpdateEpoch() {
            m_ts.tv_sec = TSUpdater<tz>::update(m_tm);
            if (m_ts.tv_sec == -1) {
                SPDLOG_ERROR("std::timespec update failed");
            }
            // epoch 改变，使缓存失效
            m_cached_epoch = -1;
        }

        void UpdateTM() {
            // 缓存优化：如果 epoch 相同，直接复用之前的 m_tm
            if (m_ts.tv_sec == m_cached_epoch) {
                return;  // 缓存命中，耗时 <5ns
            }

            // 缓存未命中，重新计算
            // UTC: ~7μs (civil_from_epoch_utc 算法)
            // Local: ~110μs (localtime_r 系统调用)
            if (!TMUpdater<tz>::update(m_ts.tv_sec, m_tm)) {
                SPDLOG_ERROR("std::tm update failed");
            }
            m_cached_epoch = m_ts.tv_sec;  // 更新缓存
//            m_tm.tm_isdst = -1; // 自动处理夏令时
        }
    };

    using DateTimeUTC = DateTime<kUTC>;
    using DateTimeLocal = DateTime<kLocal>;

}

#endif // __linux__
