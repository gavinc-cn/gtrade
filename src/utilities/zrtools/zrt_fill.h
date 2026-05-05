#pragma once

#include "zrtools/zrt_define.h"
#include "zrtools/zrt_string.h"
#include "zrtools/zrt_assert.h"
#include "zrtools/zrt_compare.h"
#include "zrtools/zrt_cpp14_compat.h"


namespace zrt {
    template<typename Dst, typename Src>
    inline void on_err_log(Dst& dst, const Src& src) {
        SPDLOG_ERROR("fill field({}) failed", src);
    }

    template<typename Dst, typename Src>
    inline void on_err_fill_default(Dst& dst, const Src& src) {
        SPDLOG_ERROR("fill field({}) failed", src);
        dst = Dst {};
    }


    // 都是cstr
    template<size_t N, size_t M>
    inline void fill_field(char(&dst)[N], const char(&src)[M]) {
        static_assert(N > 0 && M > 0);
        constexpr size_t min_sz = std::min(N, M) - 1;
        const size_t copy_len = ::strnlen(src, min_sz);
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }

    // cstr转整数
    template<typename T, typename = std::enable_if_t<zrt::IsIntegralNum_v<T>>>
    inline void fill_field(T& dst, const char* src) {
        dst = zrt::convert<T>(src);
    }

    // cstr转char
    inline void fill_field(char& dst, const char* src) {
        dst = src[0];
    }

    // cstr转str
    inline void fill_field(std::string& dst, const char src[]) {
        dst = src;
    }

    // cstr转double
    inline void fill_field(double& dst, const char src[]) {
        dst = zrt::convert<double>(src);
    }

    // str转整数
    template<typename T, typename = std::enable_if_t<zrt::IsIntegralNum_v<T>>>
    inline void fill_field(T& dst, const std::string& src) {
        dst = zrt::convert<T>(src);
    }

    // str转double
    inline void fill_field(double& dst, const std::string& src) {
        dst = zrt::convert<double>(src);
    }

    // str转cstr
    template<size_t N, typename T, typename = std::enable_if_t<std::is_same_v<std::string,T>>>
    inline void fill_field(char(&dst)[N], const T& src) {
        static_assert(N > 0);
        const size_t min_sz = std::min(N, src.size() + 1);
        strncpy(dst, src.data(), min_sz);
        dst[min_sz - 1] = '\0';
    }

    // view转char
    inline void fill_field(char& dst, const zrt::StrView src) {
        if (ZRT_UNLIKELY(src.empty())) return;
        dst = src[0];
    }

    // view转cstr, src是string_view或者char*都会用这个
    template<size_t N>
    inline void fill_field(char(&dst)[N], const zrt::StrView src) {
        static_assert(N > 0);
        const size_t min_sz = std::min(N, src.size() + 1);
        strncpy(dst, src.data(), min_sz);
        dst[min_sz - 1] = '\0';
    }

    // view转str
    inline void fill_field(std::string& dst, const zrt::StrView src) {
        dst = std::string(src);
    }

    // char转cstr
    template<size_t N, typename T>
    std::enable_if_t<zrt::IsChar_v<T>>
    inline fill_field(char(&dst)[N], const T src) {
        static_assert(N > 1);
        dst[0] = src;
        dst[1] = '\0';
    }

    // char转str
    template<typename T>
    std::enable_if_t<zrt::IsChar_v<T>>
    inline fill_field(std::string& dst, const T src) {
        dst = std::string(1, src);
    }

    // 整数转cstr
    template<size_t N, typename T>
    std::enable_if_t<zrt::IsIntegralNum_v<T>>
    inline fill_field(char(&dst)[N], const T src) {
        static_assert(N > 0);
        const std::string src_str = std::to_string(src);
        const size_t min_sz = std::min(N, src_str.size() + 1);
        strncpy(dst, src_str.c_str(), min_sz);
        dst[min_sz - 1] = '\0';
    }

    // 整数转double
    template <typename T, typename = std::enable_if_t<zrt::IsIntegralNum_v<T>>>
    inline void fill_field(double& dst, const T src) {
        dst = src;
    }

    // // 整数互转
    // template <typename T, typename U, typename = std::enable_if_t<zrt::IsIntegralNum_v<T> && zrt::IsIntegralNum_v<U>>>
    // inline void fill_field(T& dst, const U src) {
    //     dst = src;
    // }

    // 能隐式转换的直接赋值
    template <typename T, typename U, typename = std::enable_if_t<std::is_convertible_v<U,T>>>
    inline void fill_field(T& dst, const U& src) {
        dst = src;
    }

    // 整数或浮点数转str
    template <typename T, typename = std::enable_if_t<zrt::IsFloatingPoint_v<T> || zrt::IsIntegralNum_v<T>>>
    inline void fill_field(std::string& dst, const T src) {
        dst = std::to_string(src);
    }

    // 类型相等但不是cstr
    template <typename T, typename = std::enable_if_t<!zrt::IsCStr_v<T>>>
    inline void fill_field(T& dst, const T& src) {
        dst = src;
    }

    template<typename T>
    std::enable_if_t<zrt::IsEnum_v<T>>
    inline fill_field(int& dst, const T src) {
        dst = static_cast<int>(src);
    }

    template<typename T>
    std::enable_if_t<zrt::IsEnum_v<T>>
    inline fill_field(char& dst, const T src) {
        dst = static_cast<char>(src);
    }

    template<typename T>
    std::enable_if_t<zrt::IsEnum_v<T>>
    inline fill_field(char(&dst)[2], const T src) {
        dst[0] = static_cast<char>(src);
        dst[1] = '\0';
    }

    // 不相等才填充
    template <typename T, typename U>
    inline void equal_or_fill(T& dst, const U& src) {
        if (!zrt::equal(dst, src)) {
            SPDLOG_ERROR("dst({}) != src({})", dst, src);
            zrt::fill_field(dst, src);
        }
    }

    // 不相等报错
    template <typename T, typename U>
    inline void equal_or_pass(T& dst, const U& src) {
        if (!zrt::equal(dst, src)) {
            SPDLOG_ERROR("dst({}) != src({})", dst, src);
        }
    }

    // src不为空才填充
    template <typename T, typename U>
    inline void fill_if_not_empty(T& dst, const U& src) {
        if (!zrt::is_empty(src)) {
            zrt::fill_field(dst, src);
        }
    }

#define ZRT_EQUAL_OR_PASS(dst, src) do { \
    if (!zrt::equal(dst, src)) { \
        SPDLOG_ERROR(#dst"({}) != "#src"({})", dst, src); \
    } \
} while(0)
}
