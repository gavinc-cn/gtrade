#pragma once

#include "zrtools/zrt_exceptions.h"

#define g_assert(expression, message) \
    if (!(expression)) { \
        SPDLOG_ERROR("Assertion failed: "#expression", {}", message); \
        throw zrt::RunTimeError("Assertion failed: "#expression", {}", message); \
    }

#define assert_eq(value1, value2) \
{ \
    std::stringstream oss; \
    oss << value1 << " == " << value2; \
    g_assert(value1 == value2, oss.str()) \
}

#define assert_str_eq(value1, value2) \
{ \
    assert_eq(std::string(value1), std::string(value2)) \
}

#define assert_gt(value1, value2) \
{ \
    std::stringstream oss; \
    oss << value1 << " > " << value2; \
    g_assert(value1 > value2, oss.str()) \
}

#define assert_lt(value1, value2) \
{ \
    std::stringstream oss; \
    oss << value1 << " < " << value2; \
    g_assert(value1 < value2, oss.str()) \
}

#define assert_ge(value1, value2) \
{ \
    std::stringstream oss; \
    oss << value1 << " >= " << value2; \
    g_assert(value1 >= value2, oss.str()) \
}

#define ZRT_EXPECT(logger, expression, message) \
if (!(expression)) { \
logger("Expect failed: "#expression", {}", message); \
}

#define ZRT_EXPECT_EQ(logger, value1, value2) \
do { \
std::stringstream oss; \
oss << value1 << " == " << value2; \
ZRT_EXPECT(logger, zrt::equal(value1, value2), oss.str()) \
} while(false)


