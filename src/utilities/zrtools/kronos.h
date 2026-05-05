#pragma once

#include <iostream>
#include <chrono>
#include <ctime>
#include <sys/time.h>
#include <unistd.h>
#include <sstream>
#include <iostream>
#include <chrono>
#include <ctime>
#include <sstream>
#include "spdlog/spdlog.h"

namespace zrt {
    template<typename T>
    inline long get_epoch_l() {
        return std::chrono::duration_cast<T>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    inline long getEpoch13() {
        long time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        return time_ms;
    }

    inline long getEpoch13_v2() {
        struct timeval tv{};
        gettimeofday(&tv, nullptr);
        return tv.tv_sec * 1000 + tv.tv_usec / 1000;
    }

    inline std::string epoch13_to_iso2(long epoch13) {
        char now[32];
        struct tm *time_tm;
        time_t epoch10 = epoch13 / 1000;
        time_tm = localtime(&epoch10);
        strftime(now, 32, "%Y-%m-%dT%H:%M:%SZ", time_tm);
        std::string now_str = now;
        return now_str;
    }

    inline std::string epoch10_to_iso2(long epoch10) {
        char now[32];
        strftime(now, 32, "%Y-%m-%dT%H:%M:%SZ", localtime(&epoch10));
        return std::string(now);
    }

    // inline long get_epoch19() {
    //     timespec ts {};
    //     clock_gettime(CLOCK_REALTIME, &ts);
    //     constexpr long kGiga = 1000 * 1000 * 1000;
    //     return ts.tv_sec * kGiga + ts.tv_nsec;
    // }
    //
    // inline long get_epoch16() {
    //     return get_epoch19() / 1000;
    // }
    //
    // inline long get_epoch13() {
    //     return get_epoch16() / 1000;
    // }
    //
    // inline long get_epoch10() {
    //     return get_epoch13() / 1000;
    // }
    //
    // inline double get_epoch() {
    //     return static_cast<double>(get_epoch19()) / 1e9;
    // }
    //
    // inline double time_now() {
    //     return get_epoch();
    // }

    inline void time_cost(double start) {
        timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);
        std::cout << "time cost: " << ts.tv_sec + ts.tv_nsec / 1e9 - start << std::endl;
    }

    inline int get_day_sec(int update_time) {
        int update_time_sec = update_time / 1000;
        int hour = update_time_sec / 10000;
        int minute = update_time_sec % 10000 / 100;
        int second = update_time_sec % 100;
        return hour * 3600 + minute * 60 + second;
    }

    inline int get_update_time(int day_seconds) {
        int hour = day_seconds / 3600;
        int minute = day_seconds % 3600 / 60;
        int second = day_seconds % 60;
        int update_time = hour * 10000 + minute * 100 + second;
        return update_time * 1000;
    }

//class Timer
//{
//public:
//    void print()
//    {
//        cout << _t.elapsed() << endl;
//    }
//    boost::timer _t;
//};

    // class Timer {
    // public:
    //     explicit Timer(const std::string& func_name="timer"): m_func_name(func_name) {
    //         reset();
    //     }
    //
    //     ~Timer() {
    //         double time_diff = get_diff();
    //
    //     }
    //     double get_diff() const {
    //         return zrt::time_now() - m_start;
    //     }
    //
    //     void print() const {
    //         double time_diff = get_diff();
    //         SPDLOG_INFO("[{}] time cost: {:.6f}s", m_func_name, time_diff);
    //     }
    //
    //     void print(const std::string& msg) const {
    //         double time_diff = get_diff();
    //         SPDLOG_INFO("[{}] time cost: {:.6f}s", msg, time_diff);
    //     }
    //
    //     void reset() {
    //         m_start = zrt::time_now();
    //     }
    // private:
    //     double m_start {};
    //     std::string m_func_name;
    // };
}

#ifdef HAS_BOOST
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/timer.hpp>

namespace zrt {

    long timeStrToEpoch10(const string& timeStr)
    {
        boost::posix_time::ptime t(boost::posix_time::time_from_string(timeStr));
        boost::posix_time::ptime start(boost::gregorian::date(1970, 1, 1));
        boost::posix_time::time_duration td = t - start;
        time_t epoch = td.total_seconds();
        return epoch;
    }

    long timeStrToEpoch13(const string& timeStr)
    {
        boost::posix_time::ptime t(boost::posix_time::time_from_string(timeStr));
        boost::posix_time::ptime start(boost::gregorian::date(1970, 1, 1));
        boost::posix_time::time_duration td = t - start;
        time_t epoch = td.total_milliseconds();
        return epoch;
    }

    string epoch10ToTimeIso(time_t epoch)
    {
        return to_iso_extended_string(boost::posix_time::from_time_t(epoch));
    }

    string epoch13ToTimeIso(time_t epoch)
    {
        time_t sec_part = epoch / 1000;
        time_t mill_part = epoch % 1000;
        string timeStr = to_iso_extended_string(boost::posix_time::from_time_t(sec_part));
        return timeStr + "." + std::to_string(mill_part);
    }

//class Timer
//{
//public:
//    void print()
//    {
//        cout << _t.elapsed() << endl;
//    }
//    boost::timer _t;
//};
}
#endif



