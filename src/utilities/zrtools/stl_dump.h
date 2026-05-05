#pragma once

#include <cstdarg>
#include <sstream>
#include <ostream>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <list>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <queue>

#define ZRT_PROCESS_SINGLE_CONTAINER                    \
    os << "[";                                      \
    for (auto i=input.begin(); i!=input.end(); i++) \
    {                                               \
        os << *i;                                   \
        if (std::next(i) != input.end())            \
        {                                           \
            os << ", ";                             \
        }                                           \
    }                                               \
    os << "]";                                      \
    return os;


#define ZRT_PROCESS_DOUBLE_CONTAINER                    \
    os << "{";                                      \
    for (auto i=input.begin(); i!=input.end(); i++) \
    {                                               \
        os << i->first << ":" << i->second;         \
        if (std::next(i) != input.end())            \
        {                                           \
            os << ", ";                             \
        }                                           \
    }                                               \
    os << "}";                                      \
    return os;

#define ZRT_PROCESS_CONTAINER(left_bracket, right_bracket) do { \
    os << #left_bracket; \
    bool first = true; \
    for (const auto& ele : cntr) { \
        if (!first) os << ", "; \
        os << ele; \
        first = false; \
    } \
    os << #right_bracket; \
    return os; \
} while(false)

#define ZRT_DECLARE_PRINTER_SINGLE(container) \
    template <typename T> \
    std::ostream& operator<<(std::ostream& os, const std::container<T>& input) \
    { \
        ZRT_PROCESS_SINGLE_CONTAINER \
    }

#define ZRT_DECLARE_PRINTER_DOUBLE(container) \
    template<typename T1, typename T2> \
    std::ostream& operator<<(std::ostream& os, const std::container<T1, T2>& input) \
    { \
        ZRT_PROCESS_DOUBLE_CONTAINER \
    }

ZRT_DECLARE_PRINTER_SINGLE(vector)
ZRT_DECLARE_PRINTER_SINGLE(set)
ZRT_DECLARE_PRINTER_SINGLE(multiset)
// ZRT_DECLARE_PRINTER_SINGLE(unordered_set)
ZRT_DECLARE_PRINTER_SINGLE(unordered_multiset)


template<typename T1, typename T2>
std::ostream& operator<<(std::ostream& os, const std::pair<T1, T2>& input)
{
    os << input.first << ":" << input.second;
    return os;
}

ZRT_DECLARE_PRINTER_DOUBLE(map)
ZRT_DECLARE_PRINTER_DOUBLE(multimap)
ZRT_DECLARE_PRINTER_DOUBLE(unordered_map)
// ZRT_DECLARE_PRINTER_DOUBLE(unordered_multimap)

namespace zrt {
    template<typename Tuple, size_t N>
    struct tuple_print
    {
        static void print(const Tuple& t, std::ostream& os)
        {
            tuple_print<Tuple, N-1>::print(t, os);
            os << ", " << std::get<N-1>(t);
        }
    };

    template<typename Tuple>
    struct tuple_print<Tuple, 1>
    {
        static void print(const Tuple& t, std::ostream& os)
        {
            os << std::get<0>(t);
        }
    };
}

template<typename... Args>
std::ostream& operator<<(std::ostream& os, const std::tuple<Args...>& t)
{
    os << "(";
    zrt::tuple_print<decltype(t), sizeof...(Args)>::print(t, os);
    os << ")";
    return os;
}

#define ZRT_DECLARE_PRINTER_CONTAINER(container, left_bracket, right_bracket) \
template<typename... Args> \
std::ostream& operator<<(std::ostream& os, const std::container<Args...>& cntr) { \
ZRT_PROCESS_CONTAINER(left_bracket, right_bracket); \
}

ZRT_DECLARE_PRINTER_CONTAINER(unordered_set, {, })
ZRT_DECLARE_PRINTER_CONTAINER(unordered_multimap, {, })

namespace zrt
{
    template <typename T>
    inline std::string to_str(const T& input)
    {
        std::stringstream ss;
        ss << std::setprecision(8) << input;
        return ss.str();
    }

    inline void print()
    {
        std::cout << std::endl;
    }

    template<typename T>
    std::ostream& print(std::ostream &os, const T& t)
    {
        return os << t << std::endl;
    }
    template<typename T, typename... Args>
    std::ostream& print(std::ostream& os, const T& t, const Args&... rest)
    {
        os << t << " ";
        return print(os, rest...);
    }

    template<typename... Args>
    void print(const Args&... rest)
    {
        print(std::cout, rest...);
    }
}
