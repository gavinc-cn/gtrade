#pragma once

#include <cmath>
#include "spdlog/spdlog.h"
#include "zrtools/zrt_equal.h"
#include "zrtools/zrt_compare.h"
#include "zrtools/zrt_misc.h"

namespace zrt {
    inline double round(double num, int n) {
        return std::round(num * std::pow(10, n)) / std::pow(10, n);
    }

    // 四舍五入到指定精度, precision为0时返回原值
    inline double round_to(double value, double precision) {
        if (zrt::is_empty(precision)) {
            SPDLOG_ERROR("unexpected precision={}", precision);
            return value;
        }
        return std::round(value / precision) * precision;
    }

    // 向下取整到指定精度, precision为0时返回原值
    inline double floor_to(double value, double precision) {
        if (zrt::is_empty(precision)) {
            SPDLOG_ERROR("unexpected precision={}", precision);
            return value;
        }
        return std::floor(value / precision) * precision;
    }

    inline int get_decimal_places(double num) {
        if (zrt::is_empty(num)) return 0;
        int decimal_places = 0;
        while (zrt::less(std::abs(num), 1.0)) {
            num *= 10;
            ++decimal_places;
        }
        return decimal_places;
    }

    inline std::string round_str(double num, int n) {
        return fmt::format("{0:.{1}f}", num, n);
    }

    inline std::string round_to_str(double value, double precision) {
        return round_str(round_to(value, precision), get_decimal_places(precision));
    }

    inline std::string floor_to_str(double value, double precision) {
        return round_str(floor_to(value, precision), get_decimal_places(precision));
    }

    template<typename T>
    class SMA {
    public:
        SMA() = default;
        explicit SMA(const size_t window_size) : m_window_size(window_size) {}
        void SetWindowSize(const size_t window_size) { m_window_size = window_size; }
        void Add(const T& data) {
            m_sum += data;
            m_data.emplace_back(data);
            m_avg = m_sum / m_data.size();
            if (m_data.size() > m_window_size) {
                m_sum -= m_data.front();
                m_data.pop_front();
                m_avg = zrt::safe_div(m_sum, m_data.size(), 0);
            }
        }
        T GetAvg() {
            return m_avg;
        }
        size_t GetSize() {
            return m_data.size();
        }
    private:
        size_t m_window_size {};
        T m_sum {};
        T m_avg {};
        std::deque<T> m_data {};
    };
}
