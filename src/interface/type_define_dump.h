
#pragma once
#include "type_define.h"
#ifdef __linux__
#include <iostream>
#include <unordered_map>
/*
* note: 本文件由脚本自动生成
*/

std::ostream& operator<<(std::ostream& os, const Account& st);
std::ostream& operator<<(std::ostream& os, const DBConfig& st);
std::ostream& operator<<(std::ostream& os, const ProxyConfig& st);
std::ostream& operator<<(std::ostream& os, const ExchangeUrlConfig& st);
std::ostream& operator<<(std::ostream& os, const GTradeConfig& st);
#endif // __linux__