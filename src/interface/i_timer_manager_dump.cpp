
#ifdef __linux__
#include <cstring>
#include "i_timer_manager_dump.h"
/*
* note: 本文件由脚本自动生成
*/

std::ostream& operator<<(std::ostream& os, const TimerKey& st)
{
    os << "{" 
    << "service_name:" << st.service_name << ","
    << "setter_id:" << st.setter_id << ","
    << "timer_id:" << st.timer_id 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const SetTimerReq& st)
{
    os << "{" 
    << dynamic_cast<const TimerKey&>(st) << "," 
    << "delay_ms:" << st.delay_ms << ","
    << "repeat:" << st.repeat 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const TimerInfo& st)
{
    os << "{" 
    << dynamic_cast<const TimerKey&>(st) << "," 
    << "timer:" << st.timer << ","
    << "delay_ms:" << st.delay_ms << ","
    << "repeat:" << st.repeat 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const DummyTimerInfo& st)
{
    os << "{" 
    << "service_name:" << st.service_name << ","
    << "setter_id:" << st.setter_id << ","
    << "timer_id:" << st.timer_id << ","
    << "delay_ms:" << st.delay_ms << ","
    << "target_ms:" << st.target_ms << ","
    << "repeat:" << st.repeat << ","
    << "paused:" << st.paused 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const TimerEventPush& st)
{
    os << "{" 
    << dynamic_cast<const TimerKey&>(st) << "," 
    << "target_ms:" << st.target_ms 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const EpochPush& st)
{
    os << "{" 
    << "second:" << st.second << ","
    << "nano:" << st.nano 
    << "}";
    return os;
}
            

#endif // __linux__