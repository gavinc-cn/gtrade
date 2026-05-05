
#ifdef __linux__
#include <cstring>
#include "i_exchange_data_dump.h"
/*
* note: 本文件由脚本自动生成
*/

std::ostream& operator<<(std::ostream& os, const Base& st)
{
    os << "{" 
    << "ex_time:" << st.ex_time << ","
    << "local_time:" << st.local_time << ","
    << "datetime:" << st.datetime 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const Ohlcv& st)
{
    os << "{" 
    << dynamic_cast<const Base&>(st) << "," 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const MarketInfo& st)
{
    os << "{" 
    << dynamic_cast<const Base&>(st) << "," 
    << "market:" << st.market << ","
    << "inst_type:" << st.inst_type << ","
    << "instrument:" << st.instrument << ","
    << "inst_id_code:" << st.inst_id_code << ","
    << "base_ccy:" << st.base_ccy << ","
    << "quote_ccy:" << st.quote_ccy << ","
    << "settle_ccy:" << st.settle_ccy << ","
    << "contract_val:" << st.contract_val << ","
    << "contract_multi:" << st.contract_multi << ","
    << "contract_val_ccy:" << st.contract_val_ccy << ","
    << "list_time:" << st.list_time << ","
    << "exp_time:" << st.exp_time << ","
    << "lever:" << st.lever << ","
    << "price_unit:" << st.price_unit << ","
    << "amt_unit:" << st.amt_unit << ","
    << "min_amt:" << st.min_amt << ","
    << "inst_state:" << st.inst_state 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const Ticker& st)
{
    os << "{" 
    << dynamic_cast<const Base&>(st) << "," 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const KLineRange& st)
{
    os << "{" 
    << "req_id:" << st.req_id << ","
    << "strat_id:" << st.strat_id << ","
    << "market:" << st.market << ","
    << "instrument:" << st.instrument << ","
    << "coefficient:" << st.coefficient << ","
    << "scale:" << st.scale << ","
    << "start_time:" << st.start_time << ","
    << "end_time:" << st.end_time << ","
    << "count:" << st.count << ","
    << "klines:" << st.klines 
    << "}";
    return os;
}
            
std::ostream& operator<<(std::ostream& os, const Depth& st)
{
    os << "{" 
    << dynamic_cast<const Base&>(st) << "," 
    << "market:" << st.market << ","
    << "symbol:" << st.symbol << ","
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
    << "seq_id:" << st.seq_id << ","
    << "monotonic:" << st.monotonic 
    << "}";
    return os;
}
            

#endif // __linux__