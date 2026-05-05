
#pragma once
#include "db_structures.h"
#ifdef __linux__
#include <iostream>
#include <unordered_map>
/*
* note: 本文件由脚本自动生成
*/

std::ostream& operator<<(std::ostream& os, const Balance& st);
std::ostream& operator<<(std::ostream& os, const KLine& st);
std::ostream& operator<<(std::ostream& os, const Order& st);
std::ostream& operator<<(std::ostream& os, const Position& st);
std::ostream& operator<<(std::ostream& os, const StrategyLog& st);
std::ostream& operator<<(std::ostream& os, const Trade& st);
#endif // __linux__