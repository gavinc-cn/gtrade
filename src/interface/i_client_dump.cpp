
#ifdef __linux__
#include <cstring>
#include "i_client_dump.h"
/*
* note: 本文件由脚本自动生成
*/

std::ostream& operator<<(std::ostream& os, const QryReqBase& st)
{
    os << "{" 
    << "market:" << st.market << ","
    << "account_id:" << st.account_id << ","
    << "retry_time:" << st.retry_time 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const MarketInfoQryReq& st)
{
    os << "{" 
    << dynamic_cast<const QryReqBase&>(st) << "," 
    << "inst_type:" << st.inst_type << ","
    << "instrument:" << st.instrument 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const DepthQryReq& st)
{
    os << "{" 
    << dynamic_cast<const QryReqBase&>(st) << "," 
    << "req_id:" << st.req_id << ","
    << "strat_id:" << st.strat_id << ","
    << "instrument:" << st.instrument << ","
    << "level:" << st.level << ","
    << "start_time:" << st.start_time << ","
    << "end_time:" << st.end_time 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const KLineQryReq& st)
{
    os << "{" 
    << dynamic_cast<const QryReqBase&>(st) << "," 
    << "req_id:" << st.req_id << ","
    << "strat_id:" << st.strat_id << ","
    << "instrument:" << st.instrument << ","
    << "coefficient:" << st.coefficient << ","
    << "scale:" << st.scale << ","
    << "start_time:" << st.start_time << ","
    << "end_time:" << st.end_time 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const BalanceQryReq& st)
{
    os << "{" 
    << dynamic_cast<const QryReqBase&>(st) << "," 
    << "currency:" << st.currency 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HoldQryReq& st)
{
    os << "{" 
    << dynamic_cast<const QryReqBase&>(st) << "," 
    << "currency:" << st.currency 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const DoneQryReq& st)
{
    os << "{" 
    << dynamic_cast<const QryReqBase&>(st) << "," 
    << "instrument:" << st.instrument << ","
    << "entno:" << st.entno << ","
    << "ex_entno:" << st.ex_entno 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const EntrustQryReq& st)
{
    os << "{" 
    << dynamic_cast<const QryReqBase&>(st) << "," 
    << "inst_type:" << st.inst_type << ","
    << "instrument:" << st.instrument << ","
    << "entno:" << st.entno << ","
    << "ex_entno:" << st.ex_entno << ","
    << "strat_id:" << st.strat_id << ","
    << "private_no:" << st.private_no 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const EntrustQryByPrivateNoReq& st)
{
    os << "{" 
    << dynamic_cast<const QryReqBase&>(st) << "," 
    << "private_no:" << st.private_no 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const OpenEntrustsQryReq& st)
{
    os << "{" 
    << dynamic_cast<const QryReqBase&>(st) << "," 
    << "inst_type:" << st.inst_type << ","
    << "instrument:" << st.instrument << ","
    << "price_type:" << st.price_type << ","
    << "status:" << st.status 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HisEntrustsQryReq& st)
{
    os << "{" 
    << dynamic_cast<const QryReqBase&>(st) << "," 
    << "inst_type:" << st.inst_type << ","
    << "instrument:" << st.instrument << ","
    << "price_type:" << st.price_type << ","
    << "status:" << st.status << ","
    << "start_time:" << st.start_time << ","
    << "end_time:" << st.end_time 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const NotifyMessageReq& st)
{
    os << "{" 
    << "channel:" << st.channel << ","
    << "subject:" << st.subject << ","
    << "content:" << st.content << ","
    << "is_async:" << st.is_async 
    << "}";
    return os;
}
            

#endif // __linux__