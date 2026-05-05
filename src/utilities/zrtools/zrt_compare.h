//
// Created by dell on 2024/10/28.
//

#pragma once

#include "zrt_cpp14_compat.h"
#include "zrtools/zrt_type_traits.h"
#include "zrtools/zrt_equal.h"

namespace zrt {

    template <typename T>
    inline bool greater(const T& a, const T& b) {
        return a > b;
    }

    inline bool greater(float a, float b, float epsilon=kFloatEpsilon) {
        return a > b && !equal(a, b, epsilon);
    }

    inline bool greater(double a, double b, double epsilon=kDoubleEpsilon) {
        return a > b && !equal(a, b, epsilon);
    }

    template <typename T>
    inline bool less(const T& a, const T& b) {
        return a < b;
    }

    inline bool less(const char first[], const char second[])
    {
        return std::string(first) < std::string(second);
    }

    inline bool less(const char first, const char second[]) {
        return std::string(1, first) < std::string(second);
    }

    inline bool less(const char first[], const char second) {
        return std::string(first) < std::string(1, second);
    }

    inline bool less(const char first[], const std::string& second) {
        return std::string(first) < second;
    }

    inline bool less(const std::string& first, const char second[]) {
        return first < std::string(second);
    }

    inline bool less(float a, float b, float epsilon=kFloatEpsilon) {
        return a < b && !equal(a, b, epsilon);
    }

    inline bool less(double a, double b, double epsilon=kDoubleEpsilon) {
        return a < b && !equal(a, b, epsilon);
    }

    template <typename T>
    inline bool greater_equal(const T& a, const T& b) {
        return a >= b;
    }

    inline bool greater_equal(float a, float b, float epsilon=kFloatEpsilon) {
        return a >= b || equal(a, b, epsilon);
    }

    inline bool greater_equal(double a, double b, double epsilon=kDoubleEpsilon) {
        return a >= b || equal(a, b, epsilon);
    }

    template <typename T>
    inline bool less_equal(const T& a, const T& b) {
        return a <= b;
    }

    inline bool less_equal(float a, float b, float epsilon=kFloatEpsilon) {
        return a <= b || equal(a, b, epsilon);
    }

    inline bool less_equal(double a, double b, double epsilon=kDoubleEpsilon) {
        return a <= b || equal(a, b, epsilon);
    }

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
    // 两个double值都是NAN的时候视为相等
    inline bool relative_close(double a, double b, double epsilon=kDoubleEpsilon) {
        if (a == b) return true;
        if (std::isnan(a) && std::isnan(b)) return true;
        return std::abs((a - b) / std::max(std::abs(a), std::abs(b))) < epsilon;
    }

    // 两个float值都是NAN的时候视为相等
    inline bool relative_close(float a, float b, float epsilon=kFloatEpsilon) {
        if (a == b) return true;
        if (std::isnan(a) && std::isnan(b)) return true;
        return std::abs((a - b) / std::max(std::abs(a), std::abs(b))) < epsilon;
    }
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

    // 为了规避-Wfloat-equal的检查
    template<typename T, typename = std::enable_if_t<std::is_floating_point<T>::value>>
    inline bool is_empty(T a) {
        return !a;
    }

    template <typename T, typename = std::enable_if_t<!std::is_array<T>::value && !std::is_floating_point<T>::value>>
    inline bool is_empty(const T& a) {
        return !a;
    }

    inline bool is_empty(const char a[]) {
        return !strlen(a);
    }

    inline bool is_empty(const std::string& a) {
        return a.empty();
    }

    inline bool is_empty(const zrt::StrView a) {
        return a.empty();
    }

    template<typename T>
    struct my_less {
        bool operator()(const char a[], const char b[]) const {
            return std::string(a) < std::string(b);
        }

        template<typename U>
        bool operator()(const T& a, const U& b) const {
            return zrt::less(a, b);
        }
    };

    template<typename T>
    struct my_equal {
        bool operator()(const char a[], const char b[]) const {
            return zrt::equal(a, b);
        }

        bool operator()(const char a[], const char b) const {
            return zrt::equal(a, b);
        }

        bool operator()(const char a, const char b[]) const {
            return zrt::equal(a, b);
        }

        template<typename U>
        bool operator()(const T& a, const U& b) const {
            return zrt::equal(a, b);
        }
    };

    template<typename T>
    struct my_hash {
        template<typename = std::enable_if_t<!std::is_pointer<T>::value>>
        bool operator()(const T& a) const {
            return std::hash<T>()(a);
        }

        bool operator()(const char a []) const {
            return std::hash<std::string>()(std::string(a));
        }
    };

    template<typename T>
    constexpr const T& max(const T& val) {
        return val;
    }

    template<typename T0, typename T1, typename... Ts>
    std::enable_if_t<!std::is_floating_point<std::common_type_t<T0, T1, Ts...>>::value, const std::common_type_t<T0, T1, Ts...>&>
    constexpr max(const T0& val1, const T1& val2, const Ts&... vs) {
        return zrt::max(val2 < val1 ? val1 : val2, vs...);
    }

    template<typename T0, typename T1, typename... Ts>
    std::enable_if_t<std::is_floating_point<std::common_type_t<T0, T1, Ts...>>::value, std::common_type_t<T0, T1, Ts...>>
    inline max(const T0& val1, const T1& val2, const Ts&... vs) {

        using CommonType = std::common_type_t<T0, T1, Ts...>;
        CommonType v1 = val1;
        CommonType v2 = val2;

        if (std::isnan(v1)) {
            return zrt::max(val2, vs...);
        } else if (std::isnan(v2)) {
            return zrt::max(val1, vs...);
        } else {
            return zrt::max(v2 < v1 ? val1 : val2, vs...);
        }
    }

    template<typename T>
    constexpr const T& min(const T& val) {
        return val;
    }

    template<typename T0, typename T1, typename... Ts>
    std::enable_if_t<!std::is_floating_point<std::common_type_t<T0, T1, Ts...>>::value, const std::common_type_t<T0, T1, Ts...>&>
    constexpr min(const T0& val1, const T1& val2, const Ts&... vs) {
        return zrt::min(val1 < val2 ? val1 : val2, vs...);
    }

    template<typename T0, typename T1, typename... Ts>
    std::enable_if_t<std::is_floating_point<std::common_type_t<T0, T1, Ts...>>::value, std::common_type_t<T0, T1, Ts...>>
    inline min(const T0& val1, const T1& val2, const Ts&... vs) {
        using CommonType = std::common_type_t<T0, T1, Ts...>;
        CommonType v1 = val1;
        CommonType v2 = val2;

        if (std::isnan(v1)) {
            return zrt::min(val2, vs...);
        } else if (std::isnan(v2)) {
            return zrt::min(val1, vs...);
        } else {
            return zrt::min(v1 < v2 ? val1 : val2, vs...);
        }
    }

}
