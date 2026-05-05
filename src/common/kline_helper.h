//
// Created by dell on 2025/7/24.
//

#pragma once

#include "pch.h"
#include "dict.h"

inline int64_t GetScaleMs(const char scale) {
    switch (scale) {
        case KLineScale::Sec:  return 1000;
        case KLineScale::Min:  return 1000 * 60;
        case KLineScale::Hour: return 1000 * 60 * 60;
        case KLineScale::Day:  return 1000 * 60 * 60 * 24;
        default:
            SPDLOG_ERROR("Unknown K-line scale: {}", scale);
            return 0;
    }
}

inline int64_t GetScaleNs(const char scale) {
    return GetScaleMs(scale) * zrt::kMega;
}