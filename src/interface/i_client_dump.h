
#pragma once
#include "i_client.h"
#ifdef __linux__
#include <iostream>
#include <unordered_map>
/*
* note: 本文件由脚本自动生成
*/

std::ostream& operator<<(std::ostream& os, const QryReqBase& st);
std::ostream& operator<<(std::ostream& os, const MarketInfoQryReq& st);
std::ostream& operator<<(std::ostream& os, const DepthQryReq& st);
std::ostream& operator<<(std::ostream& os, const KLineQryReq& st);
std::ostream& operator<<(std::ostream& os, const BalanceQryReq& st);
std::ostream& operator<<(std::ostream& os, const HoldQryReq& st);
std::ostream& operator<<(std::ostream& os, const DoneQryReq& st);
std::ostream& operator<<(std::ostream& os, const EntrustQryReq& st);
std::ostream& operator<<(std::ostream& os, const EntrustQryByPrivateNoReq& st);
std::ostream& operator<<(std::ostream& os, const OpenEntrustsQryReq& st);
std::ostream& operator<<(std::ostream& os, const HisEntrustsQryReq& st);
std::ostream& operator<<(std::ostream& os, const NotifyMessageReq& st);
#endif // __linux__