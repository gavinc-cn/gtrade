//
// Created by dell on 2025/3/11.
//


#include "pch.h"
#include <unordered_map>
#include "dict.h"
#include "dict_mapping.h"

std::string_view DictPriceType2Okx(const char input) {
    static const std::unordered_map<char,std::string_view> tmp {
                {PriceType::Limit, "limit"},
                {PriceType::Market, "market"},
                {PriceType::MakerOnly, "post_only"},
                {PriceType::Fok, "fok"},
                {PriceType::Fak, "ioc"},
            };
    const auto iter = tmp.find(input);
    return iter == tmp.end() ? "" : iter->second;
}

char DictPriceTypeFromOkx(const std::string_view input) {
    static const std::unordered_map<std::string_view, char> tmp {
                    {"limit", PriceType::Limit},
                    {"market", PriceType::Market},
                    {"post_only", PriceType::MakerOnly},
                    {"fok", PriceType::Fok},
                    {"ioc", PriceType::Fak},
                };
    const auto iter = tmp.find(input);
    return iter == tmp.end() ? '\0' : iter->second;
}

std::string_view DictStatus2Okx(const char input) {
    static const std::unordered_map<char,std::string_view> tmp {
        {   OrderStatus::_2, "live"},
           {OrderStatus::_3, "partially_filled"},
           {OrderStatus::_4, "filled"},
           {OrderStatus::_6, "canceled"},
    };
    const auto iter = tmp.find(input);
    return iter == tmp.end() ? "" : iter->second;
}

char DictStatusFromOkx(const std::string_view input) {
    static const std::unordered_map<std::string_view, char> tmp {
                {"live",             OrderStatus::_2},
                {"partially_filled", OrderStatus::_3},
                {"filled",           OrderStatus::_4},
                {"canceled",         OrderStatus::_6},
            };
    const auto iter = tmp.find(input);
    return iter == tmp.end() ? '\0' : iter->second;
}

std::string_view DictBsSide2Okx(const char input) {
    static const std::unordered_map<char,std::string_view> tmp {
                {TradeSide::Buy, "buy"},
                {TradeSide::Sell, "sell"},
            };
    const auto iter = tmp.find(input);
    return iter == tmp.end() ? "" : iter->second;
}

char DictBsSideFromOkx(const std::string_view input) {
    static const std::unordered_map<std::string_view, char> tmp {
                    {"buy", TradeSide::Buy},
                    {"sell", TradeSide::Sell},
                };
    const auto iter = tmp.find(input);
    return iter == tmp.end() ? '\0' : iter->second;
}

std::string_view DictPosSide2Okx(const char input) {
    static const std::unordered_map<char,std::string_view> tmp {
                {PosSide::Long, "long"},
                {PosSide::Short, "short"},
                {PosSide::Net, "net"},
            };
    const auto iter = tmp.find(input);
    return iter == tmp.end() ? "" : iter->second;
}

char DictPosSideFromOkx(const std::string_view input) {
    static const std::unordered_map<std::string_view, char> tmp {
                    {"long", PosSide::Long},
                    {"short", PosSide::Short},
                    {"net", PosSide::Net},
                };
    const auto iter = tmp.find(input);
    return iter == tmp.end() ? '\0' : iter->second;
}

std::string_view DictTradeMode2Okx(const char input) {
    static const std::unordered_map<char,std::string_view> tmp {
                {TradeMode::Isolated, "isolated"},
                {TradeMode::Cross, "cross"},
                {TradeMode::Cash, "cash"},
                {TradeMode::SpotIsolated, "spot_isolated"},
            };
    const auto iter = tmp.find(input);
    return iter == tmp.end() ? "" : iter->second;
}

char DictTradeModeFromOkx(const std::string_view input) {
    static const std::unordered_map<std::string_view, char> tmp {
                        {"isolated", TradeMode::Isolated},
                        {"cross", TradeMode::Cross},
                        {"cash", TradeMode::Cash},
                        {"spot_isolated", TradeMode::SpotIsolated},
                    };
    const auto iter = tmp.find(input);
    return iter == tmp.end() ? '\0' : iter->second;
}

std::string_view DictInstType2Okx(const char input) {
    static const std::unordered_map<char,std::string_view> tmp {
            {InstType::Spot, "SPOT"},
               {InstType::Margin, "MARGIN"},
               {InstType::Swap, "SWAP"},
               {InstType::Futures, "FUTURES"},
               {InstType::Option, "OPTION"},
        };
    const auto iter = tmp.find(input);
    return iter == tmp.end() ? "" : iter->second;
}

char DictInstTypeFromOkx(const std::string_view input) {
    static const std::unordered_map<std::string_view, char> tmp {
                    {"SPOT", InstType::Spot},
                    {"MARGIN", InstType::Margin},
                    {"SWAP", InstType::Swap},
                    {"FUTURES", InstType::Futures},
                    {"OPTION", InstType::Option},
                };
    const auto iter = tmp.find(input);
    return iter == tmp.end() ? '\0' : iter->second;
}

std::string_view KLineScale2Okx(const char input) {
    static const std::unordered_map<char,std::string_view> tmp {
                    {KLineScale::Min, "m"},
                    {KLineScale::Hour, "H"},
                };
    const auto iter = tmp.find(input);
    return iter == tmp.end() ? "" : iter->second;
}

char KLineScaleFromOkx(const std::string_view input) {
    static const std::unordered_map<std::string_view, char> tmp {
                        {"m", KLineScale::Min},
                        {"H", KLineScale::Hour},
                    };
    const auto iter = tmp.find(input);
    return iter == tmp.end() ? '\0' : iter->second;
}

