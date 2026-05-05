
#pragma once
#include "i_strategy_engine.h"
#ifdef __linux__
#include <iostream>
#include <unordered_map>
/*
* note: 本文件由脚本自动生成
*/

std::ostream& operator<<(std::ostream& os, const QuoteSub& st);
std::ostream& operator<<(std::ostream& os, const TradeSub& st);
std::ostream& operator<<(std::ostream& os, const KLineSub& st);
std::ostream& operator<<(std::ostream& os, const OrderReq& st);
std::ostream& operator<<(std::ostream& os, const WithdrawReq& st);
std::ostream& operator<<(std::ostream& os, const WithdrawRsp& st);
std::ostream& operator<<(std::ostream& os, const WebSocketOpenNotify& st);
std::ostream& operator<<(std::ostream& os, const StrategyEnvStatus& st);
std::ostream& operator<<(std::ostream& os, const StrategyInfo& st);
std::ostream& operator<<(std::ostream& os, const HttpAddStrategyReq& st);
std::ostream& operator<<(std::ostream& os, const HttpDeleteStrategyReq& st);
std::ostream& operator<<(std::ostream& os, const HttpRestartStrategyReq& st);
std::ostream& operator<<(std::ostream& os, const HttpStartStrategyReq& st);
std::ostream& operator<<(std::ostream& os, const HttpStopStrategyReq& st);
std::ostream& operator<<(std::ostream& os, const HttpQueryStrategiesByTemplateReq& st);
std::ostream& operator<<(std::ostream& os, const HttpGetTemplateConfigReq& st);
std::ostream& operator<<(std::ostream& os, const HttpStrategyOperationRsp& st);
std::ostream& operator<<(std::ostream& os, const HttpQueryRsp& st);
std::ostream& operator<<(std::ostream& os, const HttpSaveSnapshotRsp& st);
std::ostream& operator<<(std::ostream& os, const HttpWalStatsRsp& st);
std::ostream& operator<<(std::ostream& os, const StratQryMarketInfoReq& st);
#endif // __linux__