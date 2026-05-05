//
// Created by dell on 2024/7/3.
//

#pragma once

#include <type_traits>
#include "zrtools/zrt_cpp14_compat.h"

namespace zrt {

    // 辅助模板，用于检查类型T是否是一个std::pair的特化
    template<typename T, typename = void>
    struct is_pair : std::false_type {};
    template<typename First, typename Second>
    struct is_pair<std::pair<First, Second>, void> : std::true_type {};

    // 用于检测类型的成员函数或嵌套类型是否存在
    template<typename...>
    using void_t = void;

    // 检测是否有嵌套的iterator类型
    template<typename T, typename = void>
    struct has_iterator : std::false_type {};
    template<typename T>
    struct has_iterator<T, void_t<typename T::iterator>> : std::true_type {};

    // 检测是否有begin和end成员函数
    template<typename T, typename = void>
    struct has_begin_end : std::false_type {};
    template<typename T>
    struct has_begin_end<T, void_t<decltype(std::begin(std::declval<T&>())),
                                   decltype(std::end(std::declval<T&>()))>>
            : std::true_type {};

    // 使用enable_if_t来启用函数，仅当类型具有begin和end成员函数时
    template<typename T, typename Enable = void>
    struct is_iterable : std::false_type {};
    template<typename T>
    struct is_iterable<T, std::enable_if_t<
            has_begin_end<T>::value &&
            has_iterator<T>::value>>
            : std::true_type {};

    //template<typename T>
    //using is_iterable = typename is_iterable_helper<T>::type;

    // 判断是否是C风格字符串
    template <typename T>
    struct IsCStr : std::integral_constant<bool,
            // 检查数组类型（如 char[N], const char[]）
            (std::is_array<T>::value &&
             std::is_same<std::remove_cv_t<std::remove_extent_t<T>>, char>::value) ||
            // 检查指针类型（如 char*, const char*）
            (std::is_pointer<T>::value &&
             std::is_same<std::remove_cv_t<std::remove_pointer_t<T>>, char>::value)
    > {};
    template <typename T>
    INLINE_VAR constexpr bool IsCStr_v = IsCStr<T>::value;

    // 是char
    template <typename T>
    INLINE_VAR constexpr bool IsChar_v = std::is_same<T,char>::value;

    // 是整数类型, 不包含char, 但包含bool
    template <typename T>
    struct IsIntegralNum : std::integral_constant<bool,
            std::is_integral<T>::value && !std::is_same<T,char>::value> {};
    template <typename T>
    INLINE_VAR constexpr bool IsIntegralNum_v = IsIntegralNum<T>::value;

    // 是整数类型, 不包含char, bool
    template <typename T>
    struct IsIntNum : std::integral_constant<bool,
        std::is_integral<T>::value &&
        !std::is_same<T,char>::value &&
        !std::is_same<T,bool>::value> {};
    template <typename T>
    INLINE_VAR constexpr bool IsIntNum_v = IsIntNum<T>::value;

    // 是浮点数类型
    template <typename T>
    INLINE_VAR constexpr bool IsFloatingPoint_v = std::is_floating_point<T>::value;

    // 是指针类型
    template <typename T>
    INLINE_VAR constexpr bool IsPointer_v = std::is_pointer<T>::value;

    // 是枚举值
    template<typename T>
    INLINE_VAR constexpr bool IsEnum_v = std::is_enum<T>::value;

    // 两个都是整数类型
    template <typename T, typename U>
    INLINE_VAR constexpr bool IsBothIntegralNum_v = IsIntegralNum_v<T> && IsIntegralNum_v<U>;
}
