//
// Created by dell on 2025/3/10.
//

#pragma once

#include "pch.h"
#include <array>

#define DECLARE_STR_TYPE(name, size) \
constexpr size_t k##name##Sz = size; \
using name##Cs = char[size]; \
using name##As = std::array<char,size>

struct CharCs {
    operator char() const {return m_data;}
    operator std::string() const {return std::string(1, m_data);}
    const char& operator[](const size_t idx) const {return m_data;}
    char& operator[](const size_t idx) {return m_data;}
    const char& get() const {return m_data;}
    char& get() {return m_data;}
    char m_data;
};

template <>
struct fmt::formatter<CharCs> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(const CharCs& obj, FormatContext& ctx) {
        if (obj.get() == '\0') {
            return ctx.out();
        }
        return fmt::format_to(ctx.out(), "{}", obj.get());
    }
};

namespace zrt {
    inline void fill_field(char& dst, const CharCs& src) {
        dst = src;
    }

    inline void fill_field(CharCs& dst, const char& src) {
        dst.get() = src;
    }
}

// 字符类型
// DECLARE_STR_TYPE(Char, 1);
// 时间字符串
DECLARE_STR_TYPE(DateTime, 32);
// 策略id
DECLARE_STR_TYPE(StrategyId, 64);
// 策略编号
DECLARE_STR_TYPE(PolicyNo, 32);
// 策略名称
DECLARE_STR_TYPE(StrategyName, 64);
// 策略参数
DECLARE_STR_TYPE(StrategyParam, 1024);
// 策略指标
DECLARE_STR_TYPE(StrategyIndicator, 1024);
// 组合
DECLARE_STR_TYPE(Portfolio, 32);
// 市场id
DECLARE_STR_TYPE(Market, 16);
// 标的名称
DECLARE_STR_TYPE(Instrument, 32);
// 标的类型
// DECLARE_STR_TYPE(InstType, 16);
// 币种
DECLARE_STR_TYPE(Currency, 8);
// 账户id
DECLARE_STR_TYPE(AccountId, 32);
// websocket频道
DECLARE_STR_TYPE(Channel, 16);
// 私有号
DECLARE_STR_TYPE(PrivateNo, 64);
// 错误码
DECLARE_STR_TYPE(ErrCode, 8);
// 错误消息
DECLARE_STR_TYPE(ErrMsg, 128);
// 服务id
DECLARE_STR_TYPE(ServiceId, 32);
// 定时器设置者id(目前与策略id一致)
DECLARE_STR_TYPE(SetterId, 64);
// Slack 频道名称
DECLARE_STR_TYPE(NoticeChannel, 32);
// Slack 消息主题
DECLARE_STR_TYPE(NoticeSubject, 256);
// Slack 消息内容
DECLARE_STR_TYPE(NoticeContent, 2048);

// HTTP策略管理请求结构
DECLARE_STR_TYPE(ConfigPath, 256);
DECLARE_STR_TYPE(TemplateName, 64);
DECLARE_STR_TYPE(HttpResponse, 8192);
// 策略日志内容
DECLARE_STR_TYPE(LogContent, 512);