#pragma once

#include "zrtools/zrt_string.h"
#include "zrtools/zrt_compare.h"
#include "zrtools/fmt_helper.h"

#include <random>
#include <tuple>
#include <fstream>
#include <string>
#include <boost/functional/hash.hpp>
#include <boost/algorithm/hex.hpp>
#include <boost/uuid/detail/md5.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#define DECLARE_KEY(key) static const char K_##key[] = #key
#define DECLARE_K(key)   static const char k_##key[] = #key

#define ZYIR_FIELD_FILTER_CTN(req, rsp) if (!zrt::is_empty(req) && !zrt::equal(req, rsp)) continue
#define ZYIR_FIELD_FILTER_RET(req, rsp) if (!zrt::is_empty(req) && !zrt::equal(req, rsp)) return

#define ZRT_STR(x) #x
#define ZRT_QUOTE(x) ZRT_STR(x)

// #ifdef __GNUC__
// #define ZRT_INTERNAL_GCC_IGNORE_PRAGMA(w) GCC diagnostic ignored #w
// #else
// #define ZRT_INTERNAL_GCC_IGNORE_PRAGMA(w) ""
// #endif
//
// #define ZRT_GCC_WARN_IGNORE_BEGIN(warn_opt) \
//     _Pragma("GCC diagnostic push") \
//     _Pragma(ZRT_QUOTE(ZRT_INTERNAL_GCC_IGNORE_PRAGMA(warn_opt)))
//
// #define ZRT_GCC_WARN_IGNORE_END _Pragma("GCC diagnostic pop")

namespace zrt {
    template<typename T>
    inline T get_random(T low, T high) {
        thread_local std::default_random_engine generator(std::random_device{}());
        thread_local std::uniform_int_distribution<T> distribution(low, high);
        return distribution(generator);
    }

    template<typename T>
    inline T get_random() {
        return get_random(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
    }

    inline std::string get_uuid() {
        thread_local boost::uuids::random_generator gen {};
        return boost::uuids::to_string(gen());
    }

    static std::string get_md5(const char * const buffer, size_t buffer_size)
    {
        std::string str_md5 {};
        boost::uuids::detail::md5 boost_md5;
        boost_md5.process_bytes(buffer, buffer_size);
        boost::uuids::detail::md5::digest_type digest;
        boost_md5.get_digest(digest);
        const auto char_digest = reinterpret_cast<const char*>(&digest);
        boost::algorithm::hex(char_digest,char_digest+sizeof(boost::uuids::detail::md5::digest_type), std::back_inserter(str_md5));
        return str_md5;
    }

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
    inline double safe_div(double a, double b, double default_val = NAN) {
        return b == 0 ? default_val: a / b;
    }
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

    // template <typename T>
    // inline const T& min(const T& a, const T& b) {
    //     return std::min(a, b);
    // }
    // // 取最小值，如果其中一个是NAN，就返回另一个
    // template <>
    // inline const double& min(const double& a, const double& b) {
    //     if (std::isnan(a)) {
    //         return std::min(b, a);
    //     } else {
    //         return std::min(a, b);
    //     }
    // }
    // // 取最小值，如果其中一个是NAN，就返回另一个
    // template <>
    // inline const float& min(const float& a, const float& b) {
    //     if (std::isnan(a)) {
    //         return std::min(b, a);
    //     } else {
    //         return std::min(a, b);
    //     }
    // }
    //
    // template <typename T>
    // inline const T& max(const T& a, const T& b) {
    //     return std::max(a, b);
    // }
    // // 取最大值，如果其中一个是NAN，就返回另一个
    // template <>
    // inline const double& max(const double& a, const double& b) {
    //     if (std::isnan(a)) {
    //         return std::max(b, a);
    //     } else {
    //         return std::max(a, b);
    //     }
    // }
    // // 取最大值，如果其中一个是NAN，就返回另一个
    // template <>
    // inline const float& max(const float& a, const float& b) {
    //     if (std::isnan(a)) {
    //         return std::max(b, a);
    //     } else {
    //         return std::max(a, b);
    //     }
    // }



    template<typename Key, typename Val>
    inline bool get_map_val(const std::unordered_map<Key,Val>& map, const Key& key, Val& val) {
        if (map.count(key)) {
            val = map.at(key);
            return true;
        } else {
            SPDLOG_ERROR("key({}) not found", key);
            return false;
        }
    }

    template<typename Val, typename Key, typename Map>
    Val GetMapVal(const Map& map, const Key& key, const Val& default_v=Val{}) {
        auto iter = map.find(key);
        if (iter != map.end()) {
            return iter->second;
        } else {
            return default_v;
        }
    }

    template<typename Val>
    inline Val GetUmapVal(const std::unordered_map<std::string,Val>& map, const std::string& key, const Val& default_v=Val{}) {
        return GetMapVal<Val,std::string>(map, key, default_v);
    }

    template<typename Val, typename Key>
    inline Val GetUmapVal(const std::unordered_map<Key,Val>& map, const Key& key, const Val& default_v=Val{}) {
        return GetMapVal<Val,Key>(map, key, default_v);
    }

    template<typename Map, typename Key, typename Val>
    bool TryGetMapVal(const Map& map, const Key& key, Val& val) {
        auto iter = map.find(key);
        if (iter != map.end()) {
            val = iter->second;
            return true;
        } else {
            return false;
        }
    }

#define ZRT_GET_MAP_VAL(map,key,val) do {                           \
    if (map.count(key)) {                                           \
        val = map.at(key);                                          \
    } else {                                                        \
        SPDLOG_ERROR("key({}) not found in map({})", key, #map);    \
    }                                                               \
} while(0)

#ifdef __linux__
    inline std::string get_hostname() {
        char hostname[256] {};
        if (!gethostname(hostname, sizeof(hostname))) {
            return hostname;
        } else {
            return {};
        }
    }
#endif // __linux__

    template<typename T>
    bool equal_any_of(const T& target) {
        return false;
    }

    // 判断target是否等于其后的任意一个参数, 等于返回true, 否则false
    // 递归情况：检查target是否等于第一个参数，或者递归检查剩余的参数
    template<typename T, typename U, typename... Args>
    bool equal_any_of(const T& target, const U& first, const Args&... args) {
        return zrt::equal(target, first) || zrt::equal_any_of(target, args...);
    }

    template<typename T>
    bool all_equal(const T& target) {
        return true;
    }

    // 判断target是否等于其后的所有参数, 等于返回true, 否则false
    // 递归情况：检查target是否等于第一个参数，或者递归检查剩余的参数
    template<typename T, typename U, typename... Args>
    bool all_equal(const T& target, const U& first, const Args&... args) {
        return zrt::equal(target, first) && zrt::all_equal(target, args...);
    }

    // 自定义哈希函数对象
    struct TupleHasher {
        template <typename... Args>
        size_t operator()(const ::std::tuple<Args...>& t) const {
            return hash_impl(t, ::std::index_sequence_for<Args...>{});
        }

    private:
        template <typename Tuple, size_t... Is>
        static size_t hash_impl(const Tuple& t, ::std::index_sequence<Is...>) {
            size_t seed = 0;
            // 使用折叠表达式展开参数包 (C++17 特性，但兼容 C++14 的 Boost 实现)
            using expander = int[];
            (void)expander{0, (boost::hash_combine(seed, ::std::get<Is>(t)), 0)...};
            return seed;
        }
    };

    // 自定义哈希函数对象
    struct PairHasher {
        template <typename... Args>
        size_t operator()(const ::std::pair<Args...>& t) const {
            return hash_impl(t, ::std::index_sequence_for<Args...>{});
        }

    private:
        template <typename Tuple, size_t... Is>
        static size_t hash_impl(const Tuple& t, ::std::index_sequence<Is...>) {
            size_t seed = 0;
            // 使用折叠表达式展开参数包 (C++17 特性，但兼容 C++14 的 Boost 实现)
            using expander = int[];
            (void)expander{0, (boost::hash_combine(seed, ::std::get<Is>(t)), 0)...};
            return seed;
        }
    };

    template<typename T>
    using PrimeKey = decltype(GetPKey(std::declval<T>()));

    template<typename T>
    using TupleHashMap = std::unordered_map<PrimeKey<T>,T,zrt::TupleHasher>;


    struct HeapRange {
        void* start {};
        void* end {};
        size_t size {};
    };

    // 计算当前进程堆内存地址范围
    static HeapRange get_heap_range() {
        HeapRange range = {nullptr, nullptr, 0};

        std::ifstream maps("/proc/self/maps");
        std::string line;

        while (std::getline(maps, line)) {
            if (line.find("[heap]") != std::string::npos) {
                std::istringstream iss(line);
                std::string addr_range;
                iss >> addr_range;

                size_t dash_pos = addr_range.find('-');
                if (dash_pos != std::string::npos) {
                    std::string start_str = addr_range.substr(0, dash_pos);
                    std::string end_str = addr_range.substr(dash_pos + 1);

                    range.start = (void*)std::stoull(start_str, nullptr, 16);
                    range.end = (void*)std::stoull(end_str, nullptr, 16);
                    range.size = (char*)range.end - (char*)range.start;
                }
                break;
            }
        }
        return range;
    }

    template<typename Func>
    void func_with_retry(Func&& func, const int retry_time, const std::string& func_name="func" ) {
        if (retry_time <= 0) {
            throw std::invalid_argument(zrt::safe_fmt("func_with_retry: retry_time must be > 0, got {}", retry_time));
        }
        for (int i=0; i<retry_time; ++i) {
            try {
                return func();
            } catch (const std::exception& e) {
                const std::string err_msg = zrt::safe_fmt("{} failed, tried {} times, error: {}", func_name, i + 1, e.what());
                if (i == retry_time - 1) {
                    SPDLOG_ERROR("{}", err_msg);
                    throw std::runtime_error(err_msg);
                }
                SPDLOG_WARN("{}", err_msg);
            }
        }
    }
}