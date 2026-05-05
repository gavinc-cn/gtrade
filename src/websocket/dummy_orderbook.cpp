//
// Created by dell on 2025/3/27.
//


#include "dummy_orderbook.h"
#include "err_code.h"
#include "dict.h"
#include "i_exchange_data.h"
#include "zrtools/zrt_time.h"
#include "dummy_trade.h"

void DummyOrderbook::AddEntrust(Order& entrust, const EntrustChangeCb& push_cb, const EntrustChangeCb& rsp_cb) {
    SPDLOG_INFO("");
    zrt::fill_field(entrust.status, OrderStatus::_2);
    zrt::fill_field(entrust.confirm_time, MyUTC().Epoch19());
    zrt::fill_field(entrust.update_time, entrust.confirm_time);

    if (zrt::equal(entrust.bs_side, TradeSide::Buy)) {
        auto px_iter = m_buy_entrusts.find(entrust.price);
        if (px_iter != m_buy_entrusts.end()) {
            px_iter->second.emplace_back(entrust);
            m_entno_index[entrust.entno] = std::make_pair(px_iter, --(px_iter->second.end()));
        }
        else {
            px_iter = m_buy_entrusts.emplace(entrust.price, std::list<Order>{entrust}).first;
            m_entno_index[entrust.entno] = std::make_pair(px_iter, --(px_iter->second.end()));
        }
        rsp_cb(entrust);
        push_cb(entrust);
        // 尝试即时撮合
        MatchOrders<true>(m_buy_entrusts, push_cb);
    }
    else if (zrt::equal(entrust.bs_side, TradeSide::Sell)) {
        auto px_iter = m_sell_entrusts.find(entrust.price);
        if (px_iter != m_sell_entrusts.end()) {
            px_iter->second.emplace_back(entrust);
            m_entno_index[entrust.entno] = std::make_pair(px_iter, --(px_iter->second.end()));
        }
        else {
            px_iter = m_sell_entrusts.emplace(entrust.price, std::list<Order>{entrust}).first;
            m_entno_index[entrust.entno] = std::make_pair(px_iter, --(px_iter->second.end()));
        }
        rsp_cb(entrust);
        push_cb(entrust);
        // 尝试即时撮合
        MatchOrders<false>(m_sell_entrusts, push_cb);
    }
    else {
        SPDLOG_ERROR("unexpected bs_side({})", entrust.bs_side);
    }
}

void DummyOrderbook::DelEntrust(int64_t entno, const EntrustChangeCb& push_cb, const WithdrawRspCb& rsp_cb) {
    SPDLOG_INFO("withdraw entrust({})", entno);
    WithdrawRsp withdraw_rsp {};
    zrt::fill_field(withdraw_rsp.entno, entno);
    zrt::fill_field(withdraw_rsp.ex_time, m_depth.ex_time);
    if (ZRT_UNLIKELY(!entno)) {
        const std::string err_msg = fmt::format("invalid entno({})", entno);
        zrt::fill_field(withdraw_rsp.err_code, ErrorCode::kInvalidEntno);
        zrt::fill_field(withdraw_rsp.err_msg, err_msg);
        rsp_cb(withdraw_rsp);
        SPDLOG_ERROR("{}", err_msg);
        return;
    }
    const auto iter = m_entno_index.find(entno);
    if (iter == m_entno_index.end()) {
        const std::string err_msg = fmt::format("entno({}) not found", entno);
        zrt::fill_field(withdraw_rsp.err_code, ErrorCode::kEntnoNotFound);
        zrt::fill_field(withdraw_rsp.err_msg, err_msg);
        rsp_cb(withdraw_rsp);
        SPDLOG_WARN("{}", err_msg);
        return;
    }
    Order& entrust = *(iter->second.second);
    zrt::fill_field(entrust.withdraw_time, MyUTC().Epoch19());
    zrt::fill_field(entrust.update_time, entrust.withdraw_time);
    zrt::fill_field(entrust.draw_amt, entrust.amount - entrust.filled);
    zrt::fill_field(entrust.remain, 0);
    if (zrt::equal(entrust.filled, 0)) {
        zrt::fill_field(entrust.status, OrderStatus::_6);
    } else {
        zrt::fill_field(entrust.status, OrderStatus::_8);
    }
    rsp_cb(withdraw_rsp);
    push_cb(entrust);
    m_entno_index[entno].first->second.erase(m_entno_index[entno].second);
    m_entno_index.erase(entno);
}

void DummyOrderbook::AddDepth(const Depth& depth, const EntrustChangeCb& cb) {
    m_depth = depth;
    // 深度更新触发全面撮合
    MatchOrders<true>(m_buy_entrusts, cb);
    MatchOrders<false>(m_sell_entrusts, cb);
}

// 撮合核心逻辑
template<bool IsBuy, typename EntMap>
void DummyOrderbook::MatchOrders(EntMap& ent_map, const EntrustChangeCb& cb) {
    if (zrt::is_empty(m_depth.bid_cnt) && zrt::is_empty(m_depth.ask_cnt)) {
        SPDLOG_ERROR("empty depth");
        return;
    }
    if (ent_map.empty()) {
        SPDLOG_DEBUG("order book is empty");
        return;
    }
    auto& quote_price = IsBuy ? m_depth.ask_price[0] : m_depth.bid_price[0];
    auto& quote_amt = IsBuy ? m_depth.ask_amount[0] : m_depth.bid_amount[0];
    auto& quote_lvl = IsBuy ? m_depth.ask_cnt : m_depth.bid_cnt;
    auto is_px_match = IsBuy ?
        [](const double ent_px, const double quote_px){return ent_px >= quote_px;} :
        [](const double ent_px, const double quote_px){return ent_px <= quote_px;};

    SPDLOG_TRACE("bid={}/{} ask={}/{} {} price={}",
        m_depth.bid_price[0], m_depth.bid_amount[0],
        m_depth.ask_price[0], m_depth.ask_amount[0],
        IsBuy ? "buy" : "sell", ent_map.begin()->second.front().price);

    for (auto px_iter = ent_map.begin(); px_iter != ent_map.end() && is_px_match(px_iter->first, quote_price) && quote_lvl > 0;) {
        // 现在行情只有1档, 假设挂单价格不会影响市场, 所以成交价以行情为准
        const double match_px = quote_price;
        std::list<Order>& same_price_ents = px_iter->second;
        while (!same_price_ents.empty() && quote_amt > 0) {
            auto& entrust = same_price_ents.front();
            const double trade_vol = std::min(entrust.amount, quote_amt);
            m_dummy_trade.PushDone(match_px, trade_vol, entrust);
            // 更新订单状态
            entrust.filled += trade_vol;
            entrust.remain = entrust.amount - entrust.filled;
            zrt::fill_field(entrust.update_time, m_depth.ex_time);
            zrt::fill_field(entrust.filled_time, m_depth.ex_time);
            quote_amt -= trade_vol;
            // 完全成交则移出队列
            if (zrt::equal(entrust.remain, 0)) {
                zrt::fill_field(entrust.status, OrderStatus::_4);
                cb(entrust);
                SPDLOG_INFO("entrust({}) filled", entrust.entno);
                m_entno_index.erase(entrust.entno);
                same_price_ents.pop_front();
            }
            else {
                zrt::fill_field(entrust.status, OrderStatus::_3);
                cb(entrust);
                quote_lvl = 0;
                // 部分成交, 盘口已经空了
                break;
            }
        }
        // 清理空队列
        px_iter = same_price_ents.empty() ? ent_map.erase(px_iter) : ++px_iter;
    }
}