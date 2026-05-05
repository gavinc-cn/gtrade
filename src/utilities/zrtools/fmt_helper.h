#pragma once

#include <spdlog/spdlog.h>
#include <iostream>
#include <string>
#include <type_traits>

namespace zrt
{
    // C++17版本：使用if constexpr统一处理所有类型
    template<typename T>
    constexpr auto safe_format_value(T&& value) {
        if constexpr (std::is_same_v<std::decay_t<T>, char>) {
            // char类型：将空char转换为空字符串
            return (value == '\0') ? std::string{} : std::string(1, value);
        } else {
            // 其他类型：完美转发原值
            return std::forward<T>(value);
        }
    }

    template <typename ...Args>
    void print_fmt(Args&&... input) {
        std::cout << fmt::format(std::forward<Args>(input)...) << std::endl;
    }

    template<typename... Args>
    std::string safe_fmt(Args&&... args) {
        try {
            return fmt::format(std::forward<Args>(args)...);
        } catch (const std::exception& e) {
            std::string err = "fmt::format error: ";
            err.append(e.what());
            return err;
        } catch (...) {
            return "fmt::format error: unknown exception";
        }
    }
}


