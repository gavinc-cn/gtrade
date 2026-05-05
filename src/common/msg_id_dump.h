
#pragma once
#include "msg_id.h"
#ifdef __linux__
#include <iostream>
#include <unordered_map>
/*
* note: 本文件由脚本自动生成
*/

std::string GetEmName_MsgId(int k);
MsgId GetEmVal_MsgId(const std::string& k);
std::ostream& operator<<(std::ostream& os, const MsgId& st);
#endif // __linux__