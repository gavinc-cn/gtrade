
#pragma once

#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/regex.hpp>

namespace zrt
{
    /**
     * 按字符分割字符串
     * @param s 待分割字符串
     * @param delimiter 分隔符
     * @return 分割后的字符串数组
     * @note 结尾的分隔符会产生空字符串，如 "a,b," -> ["a", "b", ""]
     */
    inline std::vector<std::string> split(const std::string &s, char delimiter)
    {
        std::vector<std::string> elements {};
        if (s.empty()) {
            return elements;
        }

        std::stringstream ss(s);
        std::string item {};
        while (std::getline(ss, item, delimiter)) {
            elements.push_back(item);
        }

        // std::getline 不会为结尾的分隔符产生空字符串，需要手动处理
        if (!s.empty() && s.back() == delimiter) {
            elements.push_back("");
        }

        return elements;
    }

    inline std::vector<std::string> split(const std::string &s, const std::string& delimiter)
    {
        std::vector<std::string> vec;
        boost::split_regex(vec, s, boost::regex(delimiter));
        return vec;
    }

    inline std::string join(const std::vector<std::string>& v, const char delimiter)
    {
        if (v.empty()) return std::string();
        std::stringstream s;
        s << v[0];
        for (size_t i=1; i<v.size(); ++i) {
            s << delimiter << v[i];
        }
        return s.str();
    }

    static std::string join(const std::vector<std::string>& v, const char delimiter, const size_t begin, const size_t end)
    {
        if (v.empty()) return std::string();
        std::stringstream s;
        s << v[begin];
        for (size_t i=begin+1; i<end and i<v.size(); ++i) {
            s << delimiter << v[i];
        }
        return s.str();
    }

    inline std::string lower(std::string& s)
    {
        transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    }

    inline std::string upper(std::string& s)
    {
        transform(s.begin(), s.end(), s.begin(), ::toupper);
        return s;
    }
}
