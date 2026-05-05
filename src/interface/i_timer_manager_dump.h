
#pragma once
#include "i_timer_manager.h"
#ifdef __linux__
#include <iostream>
#include <unordered_map>
/*
* note: 本文件由脚本自动生成
*/

std::ostream& operator<<(std::ostream& os, const TimerKey& st);
std::ostream& operator<<(std::ostream& os, const SetTimerReq& st);
std::ostream& operator<<(std::ostream& os, const TimerInfo& st);
std::ostream& operator<<(std::ostream& os, const DummyTimerInfo& st);
std::ostream& operator<<(std::ostream& os, const TimerEventPush& st);
std::ostream& operator<<(std::ostream& os, const EpochPush& st);
#endif // __linux__