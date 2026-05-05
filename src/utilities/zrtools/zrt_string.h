//
// Created by dell on 2024/7/14.
//

#pragma once

#include <string>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <boost/lexical_cast.hpp>
#include "zrtools/zrt_compare.h"

#define DEFAULT_FAIL_OPERATION 0

enum FAIL_OPERATION {
    EM_THROW,
    EM_RETURN_DEFAULT,
    EM_CUSTOM_HANDLER,
};

namespace zrt {
    template<typename ReturnType, typename InputType>
    inline ReturnType convert(const InputType& input, const ReturnType& default_val={}) {
        if (zrt::is_empty(input)) {
            return {};
        }
        else {
            try {
                return boost::lexical_cast<ReturnType>(input);
            } catch (const std::exception& e) {
                SPDLOG_ERROR("cannot convert {} from {} to {}, err: {}", input, typeid(input).name(), typeid(ReturnType).name(), e.what());
                return default_val;
            }
        }
    }

    template<typename T>
    inline std::string convert2str(T input) {
        return std::to_string(input);
    }

    inline std::string convert2str(double input, int precision) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(precision) << input;
        std::string ret = oss.str();
        if (ret == "-0." + std::string(precision, '0')) {
            ret = "0." + std::string(precision, '0');
        }
        return ret;
    }

    inline bool convertible2int(const std::string& str) {
        try {
            std::stoi(str);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    inline bool all_convertible2int(const std::vector<std::string>& vec) {
        return std::all_of(vec.begin(), vec.end(), convertible2int);
    }

    /**
     * 将字符串转换为小写
     * @param str 输入字符串
     * @return 转换后的小写字符串
     */
    inline std::string to_lower(const std::string& str) {
        std::string ret(str.size(), '\0');  // 预分配空间
        std::transform(str.begin(), str.end(), ret.begin(),
                       [](const unsigned char c) { return std::tolower(c); });
        return ret;
    }

    /**
     * 将字符串转换为大写
     * @param str 输入字符串
     * @return 转换后的大写字符串
     */
    inline std::string to_upper(const std::string& str) {
        std::string ret(str.size(), '\0');  // 预分配空间
        std::transform(str.begin(), str.end(), ret.begin(),
                       [](const unsigned char c) { return std::toupper(c); });
        return ret;
    }

    inline bool contain(const std::string& str, const std::string& sub) {
        return str.find(sub) != std::string::npos;
    }
}
