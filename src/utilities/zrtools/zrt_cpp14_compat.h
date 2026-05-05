//
// Created by dell on 2025/3/12.
//

#pragma once

#include <string_view>
#include <boost/utility/string_view.hpp>

#if __cplusplus >= 201703L
    #define INLINE_VAR inline
#else
    #define INLINE_VAR
#endif

#if defined(_WIN32) || defined(_MSC_VER)
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
#endif

namespace zrt {

#if __cplusplus >= 201703L
    using StrView = ::std::string_view;
#else
    using StrView = ::boost::string_view;
#endif

}