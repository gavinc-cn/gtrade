//
// Created by dell on 2025/3/14.
//

#pragma once

#if defined(__GNUC__) || defined(__clang__)
#define ZRT_LIKELY(x) __builtin_expect(!!(x), 1)
#define ZRT_UNLIKELY(x) __builtin_expect(!!(x), 0)
#elif defined(_MSC_VER)
#define ZRT_LIKELY(x) (x)
#define ZRT_UNLIKELY(x) (x)
#else
#define ZRT_LIKELY(x) (x)
#define ZRT_UNLIKELY(x) (x)
#endif


// 需要自己实现cls()和~cls()
#define ZRT_DECLARE_SINGLETON(cls) \
public: \
    static cls& GetInstance() {static cls instance; return instance;} \
    cls(const cls&) = delete; \
    cls& operator=(const cls&) = delete; \
private: \
    cls(); \
    ~cls()


#define ZRT_VAR2STR(var) #var