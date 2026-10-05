
#ifdef __linux__
#include <cstring>
#include "db_structures_dump.h"
/*
* note: 本文件由脚本自动生成
*/

std::ostream& operator<<(std::ostream& os, const Balance& st)
{
    os << "{" 
    << "ex_time:" << st.ex_time << ","
    << "local_time:" << st.local_time << ","
    << "datetime:" << st.datetime << ","
    << "market:" << st.market << ","
    << "account_id:" << st.account_id << ","
    << "currency:" << st.currency << ","
    << "available:" << st.available << ","
    << "frozen:" << st.frozen << ","
    << "total:" << st.total 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const KLine& st)
{
    os << "{" 
    << "ex_time:" << st.ex_time << ","
    << "local_time:" << st.local_time << ","
    << "datetime:" << st.datetime << ","
    << "market:" << st.market << ","
    << "instrument:" << st.instrument << ","
    << "coefficient:" << st.coefficient << ","
    << "scale:" << st.scale << ","
    << "open:" << st.open << ","
    << "high:" << st.high << ","
    << "low:" << st.low << ","
    << "close:" << st.close << ","
    << "volume:" << st.volume 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const Order& st)
{
    os << "{" 
    << "entno:" << st.entno << ","
    << "status:" << st.status << ","
    << "fmt_time:" << st.fmt_time << ","
    << "market:" << st.market << ","
    << "account_id:" << st.account_id << ","
    << "portfolio:" << st.portfolio << ","
    << "inst_type:" << st.inst_type << ","
    << "inst_id:" << st.inst_id << ","
    << "inst_id_code:" << st.inst_id_code << ","
    << "policy_no:" << st.policy_no << ","
    << "private_no:" << st.private_no << ","
    << "bs_side:" << st.bs_side << ","
    << "pos_side:" << st.pos_side << ","
    << "oc_side:" << st.oc_side << ","
    << "trade_mode:" << st.trade_mode << ","
    << "price_type:" << st.price_type << ","
    << "price:" << st.price << ","
    << "amount:" << st.amount << ","
    << "ent_time:" << st.ent_time << ","
    << "expire_time:" << st.expire_time << ","
    << "ex_entno:" << st.ex_entno << ","
    << "filled_px:" << st.filled_px << ","
    << "filled:" << st.filled << ","
    << "remain:" << st.remain << ","
    << "confirm_time:" << st.confirm_time << ","
    << "filled_time:" << st.filled_time << ","
    << "update_time:" << st.update_time << ","
    << "source:" << st.source << ","
    << "err_code:" << st.err_code << ","
    << "err_msg:" << st.err_msg << ","
    << "drawno:" << st.drawno << ","
    << "draw_amt:" << st.draw_amt << ","
    << "withdraw_time:" << st.withdraw_time << ","
    << "status_id:" << st.status_id << ","
    << "status_gid:" << st.status_gid << ","
    << "trd_px:" << st.trd_px << ","
    << "trd_qty:" << st.trd_qty << ","
    << "quote_monotonic:" << st.quote_monotonic 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const Position& st)
{
    os << "{" 
    << "market:" << st.market << ","
    << "account_id:" << st.account_id << ","
    << "inst_type:" << st.inst_type << ","
    << "instrument:" << st.instrument << ","
    << "pos_side:" << st.pos_side << ","
    << "portfolio:" << st.portfolio << ","
    << "margin_mode:" << st.margin_mode << ","
    << "avg_px:" << st.avg_px << ","
    << "available:" << st.available << ","
    << "ex_time:" << st.ex_time << ","
    << "local_time:" << st.local_time << ","
    << "datetime:" << st.datetime << ","
    << "upl:" << st.upl << ","
    << "upl_ratio:" << st.upl_ratio << ","
    << "notional_usd:" << st.notional_usd << ","
    << "total_cost:" << st.total_cost << ","
    << "realized_pnl:" << st.realized_pnl << ","
    << "fee_paid:" << st.fee_paid << ","
    << "pos_source:" << st.pos_source 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const StrategyLog& st)
{
    os << "{" 
    << "strat_id:" << st.strat_id << ","
    << "log_level:" << st.log_level << ","
    << "log_time:" << st.log_time << ","
    << "seq:" << st.seq << ","
    << "content:" << st.content 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const Trade& st)
{
    os << "{" 
    << "tdno:" << st.tdno << ","
    << "market:" << st.market << ","
    << "account_id:" << st.account_id << ","
    << "portfolio:" << st.portfolio << ","
    << "instrument:" << st.instrument << ","
    << "strat_id:" << st.strat_id << ","
    << "private_no:" << st.private_no << ","
    << "ordno:" << st.ordno << ","
    << "td_side:" << st.td_side << ","
    << "pos_side:" << st.pos_side << ","
    << "px_type:" << st.px_type << ","
    << "td_px:" << st.td_px << ","
    << "td_qty:" << st.td_qty << ","
    << "td_val:" << st.td_val << ","
    << "filled_time:" << st.filled_time << ","
    << "ord_status_id:" << st.ord_status_id << ","
    << "margin_mode:" << st.margin_mode 
    << "}";
    return os;
}
            

#endif // __linux__