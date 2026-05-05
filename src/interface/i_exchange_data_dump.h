
#pragma once
#include "i_exchange_data.h"
#ifdef __linux__
#include <iostream>
#include <unordered_map>
/*
* note: 本文件由脚本自动生成
*/

std::ostream& operator<<(std::ostream& os, const Base& st);
std::ostream& operator<<(std::ostream& os, const Ohlcv& st);
std::ostream& operator<<(std::ostream& os, const MarketInfo& st);
std::ostream& operator<<(std::ostream& os, const Ticker& st);
std::ostream& operator<<(std::ostream& os, const KLineRange& st);
std::ostream& operator<<(std::ostream& os, const Depth& st);
#endif // __linux__