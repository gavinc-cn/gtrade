//
// Created by dell on 2025/3/12.
//

#pragma once

#include <string_view>
#include <boost/utility/string_view.hpp>

namespace zrt {

#if __cplusplus >= 201703L
    using StrView = ::std::string_view;
#else
    using StrView = ::boost::string_view;
#endif

}