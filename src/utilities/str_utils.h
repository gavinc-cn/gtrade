
#pragma once

#include <boost/algorithm/string/regex.hpp>
#include <boost/regex.hpp>

namespace utils
{
    inline std::vector<std::string> split(const std::string &s, char delimiter)
    {
        std::vector<std::string> elements;
        std::stringstream ss(s);
        std::string item;
        while (getline(ss, item, delimiter)) {
            elements.push_back(item);
        }
        return elements;
    }

    inline std::vector<std::string> split(const std::string &s, const std::string& delimiter)
    {
        std::vector<std::string> vec;
        boost::split_regex(vec, s, boost::regex(delimiter));
        return vec;
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

