//
// Created by dell on 2025/2/18.
//

#pragma once

#include "zrtools/zrt_fill.h"
#include "rapidjson/rapidjson.h"


namespace zrt {

	template<typename T>
    inline void fill_field(T& dst, const rapidjson::Value& src) {
        zrt::fill_field(dst, src.GetString());
    }

}