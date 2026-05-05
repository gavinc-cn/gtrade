//
// Created by dell on 2025/3/15.
//

#pragma once

#include "i_exchange_data.h"

// todo 实现宏定义
#define DEFINE_PKEY(MyStruct, ...) \
inline auto GetPKey(const MyStruct& st) { \
    return std::make_tuple(...); \
}

inline auto GetPKey(const Balance& st) {
    return std::make_tuple(st.account_id, st.currency);
}

inline auto GetPKey(const Position& st) {
    return std::make_tuple(st.account_id, st.instrument, st.margin_mode, st.pos_side);
}

inline auto GetPKey(const KLine& st) {
    return std::make_tuple(st.market, st.instrument, st.coefficient, st.scale, st.ex_time);
}

inline auto GetPKey(const KLineRange& st) {
    return std::make_tuple(st.market, st.instrument, st.coefficient, st.scale, st.start_time, st.end_time);
}
