//
// Created by dell on 2024/10/28.
//

#pragma once

#include "zrtools/zrt_type_traits.h"
#include "zrtools/zrt_cpp14_compat.h"

namespace zrt {

    INLINE_VAR constexpr float kFloatEpsilon = 1e-6f;
    INLINE_VAR constexpr double kDoubleEpsilon = 1e-8;

    // 两个参数相同, 但不是float, char[]
    template <typename T, typename = std::enable_if_t<
            !std::is_array<T>::value &&
            !std::is_floating_point<T>::value>>
    inline bool equal(const T& first, const T& second)
    {
        return first == second;
    }

    // 两个参数都是整数但不是char
    template<typename T, typename U, typename = std::enable_if_t<
            zrt::IsIntegralNum_v<T> && zrt::IsIntegralNum_v<U>>>
    inline bool equal(const T& first, const U& second)
    {
        return first == second;
    }

    // 一个参数是整数, 另一个是char
    template<typename T, typename U>
    std::enable_if_t<std::is_same<T,char>::value && zrt::IsIntegralNum_v<U>,bool>
    inline equal(const T& first, const U& second)
    {
        return std::string(1, first) == std::to_string(second);
    }
    template<typename T, typename U>
    std::enable_if_t<zrt::IsIntegralNum_v<T> && std::is_same<U,char>::value,bool>
    inline equal(const T& first, const U& second)
    {
        return std::to_string(first) == std::string(1, second);
    }

    inline bool equal(const char first[], const char second[])
    {
        return std::string(first) == std::string(second);
    }

    inline bool equal(const char first, const char second[]) {
        return std::string(1, first) == std::string(second);
    }

    inline bool equal(const char first[], const char second) {
        return std::string(first) == std::string(1, second);
    }

    inline bool equal(const char first[], const std::string& second) {
        return std::string(first) == second;
    }

    inline bool equal(const std::string& first, const char second[]) {
        return first == std::string(second);
    }

    inline bool equal(const char first, const std::string& second) {
        return std::string(1, first) == second;
    }

    inline bool equal(const std::string& first, const char second) {
        return first == std::string(1, second);
    }

    inline bool equal(const char first[], const int second) {
        return std::string(first) == std::to_string(second);
    }

    inline bool equal(const int first, const char second[]) {
        return std::to_string(first) == std::string(second);
    }

    inline bool equal(const char first[], const uint second) {
        return std::string(first) == std::to_string(second);
    }

    inline bool equal(const uint first, const char second[]) {
        return std::to_string(first) == std::string(second);
    }

    template<typename T, typename = std::enable_if_t<zrt::IsIntegralNum_v<T>>>
    inline bool equal(const std::string& first, const T& second) {
        return first == std::to_string(second);
    }
    template<typename T, typename = std::enable_if_t<zrt::IsIntegralNum_v<T>>>
    inline bool equal(const T& first, const std::string& second) {
        return std::to_string(first) == second;
    }

    template<typename T, typename = std::enable_if_t<zrt::IsEnum_v<T>>>
    inline bool equal(const int first, const T second) {
        return first == static_cast<int>(second);
    }

    template<typename T, typename = std::enable_if_t<zrt::IsEnum_v<T>>>
    inline bool equal(const char first, const T second) {
        return first == static_cast<char>(second);
    }

    template<typename T, typename = std::enable_if_t<zrt::IsEnum_v<T>>>
    inline bool equal(const T first, const int second) {
        return static_cast<int>(first) == second;
    }

    template<typename T, typename = std::enable_if_t<zrt::IsEnum_v<T>>>
    inline bool equal(const T first, const char second) {
        return static_cast<char>(first) == second;
    }

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
    // 两个double值都是NAN的时候视为相等
    inline bool equal(const double a, const double b, double epsilon=kDoubleEpsilon) {
        if (a == b) return true;
        if (std::isnan(a) && std::isnan(b)) return true;
        return std::abs(a - b) < epsilon;
    }

    // 两个float值都是NAN的时候视为相等
    inline bool equal(const float a, const float b, float epsilon=kFloatEpsilon) {
        if (a == b) return true;
        if (std::isnan(a) && std::isnan(b)) return true;
        return std::abs(a - b) < epsilon;
    }
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

}
