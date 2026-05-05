//
// Created by dell on 2025/3/11.
//

#pragma once

#include "pch.h"

std::string_view DictPriceType2Okx(const char input);
char DictPriceTypeFromOkx(const std::string_view input);

std::string_view DictStatus2Okx(const char input);
char DictStatusFromOkx(const std::string_view input);

std::string_view DictBsSide2Okx(const char input);
char DictBsSideFromOkx(const std::string_view input);

std::string_view DictPosSide2Okx(const char input);
char DictPosSideFromOkx(const std::string_view input);

std::string_view DictTradeMode2Okx(const char input);
char DictTradeModeFromOkx(const std::string_view input);

std::string_view DictInstType2Okx(const char input);
char DictInstTypeFromOkx(const std::string_view input);

std::string_view KLineScale2Okx(const char input);
char KLineScaleFromOkx(const std::string_view input);
