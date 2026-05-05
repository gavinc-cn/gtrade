//
// Created by dell on 2025/3/9.
//

#pragma once

#ifdef __linux__

#include "zrtools/zrt_time.h"
#include <iostream>

namespace zrt {
    template<EmTimeZone tz>
    std::string DateTime<tz>::ToFormat(const std::string& format, const uint decimal_places) {
        UpdateTM();
        std::stringstream ss {};
        ss << std::put_time(&m_tm, format.c_str());  // ISO格式
        std::string ret = ss.str();
        const zrt::StrView decimal_fmt = ".%f";
        const size_t pos = ret.find(decimal_fmt.data());
        if (pos != std::string::npos) {
            const std::string decimals = fmt::format("{:09}", m_ts.tv_nsec).substr(0, decimal_places);
            const std::string suffix = decimals.empty() ? "" : std::string(".").append(decimals);
            ret.replace(pos, decimal_fmt.size(), suffix);
        }
        return ret;
    }

    template<EmTimeZone tz>
    DateTime<tz>::DateTime(std::string time_str, std::string format) {
        if (time_str.empty()) {
            return;
        }

        const zrt::StrView decimal_fmt = ".%f";
        const size_t fmt_dot_pos = format.find(decimal_fmt.data());
        if (fmt_dot_pos != std::string::npos) {
            const size_t time_dot_pos = time_str.find_last_of('.');
            const size_t time_decimal_pos = time_dot_pos + 1;
            const size_t suffix_pos = std::find_if(time_str.begin() + time_decimal_pos, time_str.end(), [](const char c) {return !std::isdigit(c);}) - time_str.begin();
            const std::string decimals = time_str.substr(time_decimal_pos, suffix_pos - time_decimal_pos);
            if (!decimals.empty()) {
                m_ts.tv_nsec = std::stol(decimals) * static_cast<int64_t>(std::pow(10, 9 - decimals.size()));
            }
            format.erase(fmt_dot_pos, decimal_fmt.size());
            // 小数前面的.也删掉
            time_str.erase(time_dot_pos, decimals.size() + 1);
        }

        std::istringstream ss(time_str);
        ss >> std::get_time(&m_tm, format.c_str());
        if (ss.fail()) {
            SPDLOG_ERROR("parse time_str({}) with format({}) failed", time_str, format);
            return;
        }
        UpdateEpoch();
    }

    template<EmTimeZone tz>
    DateTime<tz>::DateTime(const int64_t epoch, const int digits) {
        assert(digits == 10 || digits == 13 || digits == 16 || digits == 19);
        switch (digits) {
            case 10: {
                m_ts.tv_sec = epoch;
                m_ts.tv_nsec = 0;
                break;
            }
            case 13: {
                m_ts.tv_sec = epoch / kKilo;
                m_ts.tv_nsec = (epoch % kKilo) * kMega;
                break;
            }
            case 16: {
                m_ts.tv_sec = epoch / kMega;
                m_ts.tv_nsec = (epoch % kMega) * kKilo;
                break;
            }
            case 19: {
                m_ts.tv_sec = epoch / kGiga;
                m_ts.tv_nsec = epoch % kGiga;
                break;
            }
            default: {
                SPDLOG_ERROR("unexpected digits({})", digits);
                break;
            }
        }
    }

}

#endif // __linux__
