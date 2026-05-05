
#ifdef __linux__
#include <cstring>
#include "i_strategy_engine_dump.h"
/*
* note: 本文件由脚本自动生成
*/

std::ostream& operator<<(std::ostream& os, const QuoteSub& st)
{
    os << "{" 
    << "channel:" << st.channel << ","
    << "market:" << st.market << ","
    << "inst_id:" << st.inst_id << ","
    << "strat_id:" << st.strat_id 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const TradeSub& st)
{
    os << "{" 
    << "market:" << st.market << ","
    << "account_id:" << st.account_id << ","
    << "inst_id:" << st.inst_id << ","
    << "strat_id:" << st.strat_id 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const KLineSub& st)
{
    os << "{" 
    << "market:" << st.market << ","
    << "instrument:" << st.instrument << ","
    << "coefficient:" << st.coefficient << ","
    << "scale:" << st.scale << ","
    << "strat_id:" << st.strat_id 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const OrderReq& st)
{
    os << "{" 
    << "market:" << st.market << ","
    << "account_id:" << st.account_id << ","
    << "portfolio:" << st.portfolio << ","
    << "inst_id:" << st.inst_id << ","
    << "inst_id_code:" << st.inst_id_code << ","
    << "policy_no:" << st.policy_no << ","
    << "private_no:" << st.private_no << ","
    << "entno:" << st.entno << ","
    << "bs_side:" << st.bs_side << ","
    << "pos_side:" << st.pos_side << ","
    << "oc_side:" << st.oc_side << ","
    << "price_type:" << st.price_type << ","
    << "trade_mode:" << st.trade_mode << ","
    << "price:" << st.price << ","
    << "amount:" << st.amount << ","
    << "expire_time:" << st.expire_time << ","
    << "ent_time:" << st.ent_time << ","
    << "quote_monotonic:" << st.quote_monotonic 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const WithdrawReq& st)
{
    os << "{" 
    << "market:" << st.market << ","
    << "account_id:" << st.account_id << ","
    << "instrument:" << st.instrument << ","
    << "entno:" << st.entno 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const WithdrawRsp& st)
{
    os << "{" 
    << "entno:" << st.entno << ","
    << "ex_time:" << st.ex_time << ","
    << "err_code:" << st.err_code << ","
    << "err_msg:" << st.err_msg 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const WebSocketOpenNotify& st)
{
    os << "{" 
    << "account_id:" << st.account_id 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const StrategyEnvStatus& st)
{
    os << "{" 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const StrategyInfo& st)
{
    os << "{" 
    << "id:" << st.id << ","
    << "strat_name:" << st.strat_name << ","
    << "strat_template:" << st.strat_template << ","
    << "status:" << st.status << ","
    << "param:" << st.param << ","
    << "indicator:" << st.indicator << ","
    << "create_time:" << st.create_time << ","
    << "update_time:" << st.update_time 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpAddStrategyReq& st)
{
    os << "{" 
    << "config_path:" << st.config_path 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpDeleteStrategyReq& st)
{
    os << "{" 
    << "strat_id:" << st.strat_id 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpRestartStrategyReq& st)
{
    os << "{" 
    << "strat_id:" << st.strat_id 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpStartStrategyReq& st)
{
    os << "{" 
    << "strat_id:" << st.strat_id 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpStopStrategyReq& st)
{
    os << "{" 
    << "strat_id:" << st.strat_id 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpQueryStrategiesByTemplateReq& st)
{
    os << "{" 
    << "template_name:" << st.template_name 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpGetTemplateConfigReq& st)
{
    os << "{" 
    << "template_name:" << st.template_name 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpStrategyOperationRsp& st)
{
    os << "{" 
    << "success:" << st.success 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpQueryRsp& st)
{
    os << "{" 
    << "response:" << st.response 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpSaveSnapshotRsp& st)
{
    os << "{" 
    << "success:" << st.success << ","
    << "snapshot_path:" << st.snapshot_path << ","
    << "error_msg:" << st.error_msg 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpWalStatsRsp& st)
{
    os << "{" 
    << "success:" << st.success << ","
    << "shm_write_pos:" << st.shm_write_pos << ","
    << "shm_confirmed_pos:" << st.shm_confirmed_pos << ","
    << "shm_unconfirmed_bytes:" << st.shm_unconfirmed_bytes << ","
    << "file_current_seq:" << st.file_current_seq << ","
    << "file_total_size_bytes:" << st.file_total_size_bytes << ","
    << "last_snapshot_seq:" << st.last_snapshot_seq << ","
    << "wal_file_size_mb:" << st.wal_file_size_mb << ","
    << "need_snapshot:" << st.need_snapshot 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const StratQryMarketInfoReq& st)
{
    os << "{" 
    << "strat_id:" << st.strat_id << ","
    << "market:" << st.market << ","
    << "instrument:" << st.instrument 
    << "}";
    return os;
}
            

#endif // __linux__