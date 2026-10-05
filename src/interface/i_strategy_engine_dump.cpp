
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
            
std::ostream& operator<<(std::ostream& os, const HttpPlaceOrderReq& st)
{
    os << "{" 
    << "account_id:" << st.account_id << ","
    << "market:" << st.market << ","
    << "inst_id:" << st.inst_id << ","
    << "portfolio:" << st.portfolio << ","
    << "td_mode:" << st.td_mode << ","
    << "side:" << st.side << ","
    << "ord_type:" << st.ord_type << ","
    << "px:" << st.px << ","
    << "sz:" << st.sz << ","
    << "ent_time:" << st.ent_time 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpPlaceOrderRsp& st)
{
    os << "{" 
    << "success:" << st.success << ","
    << "order_id:" << st.order_id << ","
    << "error_msg:" << st.error_msg 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpCancelOrderReq& st)
{
    os << "{" 
    << "account_id:" << st.account_id << ","
    << "order_id:" << st.order_id 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpCancelOrderRsp& st)
{
    os << "{" 
    << "success:" << st.success << ","
    << "error_msg:" << st.error_msg 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpGetDepthReq& st)
{
    os << "{" 
    << "inst_id:" << st.inst_id << ","
    << "market:" << st.market 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpGetDepthRsp& st)
{
    os << "{" 
    << "success:" << st.success << ","
    << "inst_id:" << st.inst_id << ","
    << "market:" << st.market << ","
    << "ask_cnt:" << st.ask_cnt << ","
    << "ask_price:[";
    for (auto i=std::begin(st.ask_price); i!=std::end(st.ask_price); i++)
    {
        decltype(*i) empty{};
        if (memcmp(i, &empty, sizeof(*i)) == 0) continue;
        os << *i;
        if (std::next(i) != std::end(st.ask_price))
        {
            os << ", ";
        }
    }
    os << "]" << ","
    << "ask_amount:[";
    for (auto i=std::begin(st.ask_amount); i!=std::end(st.ask_amount); i++)
    {
        decltype(*i) empty{};
        if (memcmp(i, &empty, sizeof(*i)) == 0) continue;
        os << *i;
        if (std::next(i) != std::end(st.ask_amount))
        {
            os << ", ";
        }
    }
    os << "]" << ","
    << "bid_cnt:" << st.bid_cnt << ","
    << "bid_price:[";
    for (auto i=std::begin(st.bid_price); i!=std::end(st.bid_price); i++)
    {
        decltype(*i) empty{};
        if (memcmp(i, &empty, sizeof(*i)) == 0) continue;
        os << *i;
        if (std::next(i) != std::end(st.bid_price))
        {
            os << ", ";
        }
    }
    os << "]" << ","
    << "bid_amount:[";
    for (auto i=std::begin(st.bid_amount); i!=std::end(st.bid_amount); i++)
    {
        decltype(*i) empty{};
        if (memcmp(i, &empty, sizeof(*i)) == 0) continue;
        os << *i;
        if (std::next(i) != std::end(st.bid_amount))
        {
            os << ", ";
        }
    }
    os << "]" << ","
    << "timestamp:" << st.timestamp << ","
    << "error_msg:" << st.error_msg 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const InstrumentScopeItem& st)
{
    os << "{" 
    << "market:" << st.market << ","
    << "inst_id:" << st.inst_id << ","
    << "inst_type:" << st.inst_type 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const InstrumentInfoItem& st)
{
    os << "{" 
    << "market:" << st.market << ","
    << "inst_id:" << st.inst_id << ","
    << "inst_type:" << st.inst_type 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const ScopeOwnerItem& st)
{
    os << "{" 
    << "market:" << st.market << ","
    << "inst_id:" << st.inst_id << ","
    << "inst_type:" << st.inst_type << ","
    << "owner:" << st.owner 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpSetInstrumentScopeReq& st)
{
    os << "{" 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const HttpSetInstrumentScopeRsp& st)
{
    os << "{" 
    << "}";
    return os;
}
            

#endif // __linux__