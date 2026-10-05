//
// Created by dell on 2025/3/27.
//

#pragma once

#include "pch.h"
#include <map>
#include <deque>
#include <mutex>
#include "i_strategy_engine.h"
#include "i_exchange_data.h"
#include "strategy_engine.h"

class DummyTrade;

class DummyOrderbook {
    using EntrustChangeCb = std::function<void(const Order&)>;
    using WithdrawRspCb = std::function<void(const WithdrawRsp&)>;
    using DoneCb = std::function<void(const Trade&)>;

public:
    // 生产路径：成交回报经 DummyTrade::PushDone 处理
    explicit DummyOrderbook(DummyTrade& dummy_trade):
        m_dummy_trade(&dummy_trade)
    {
    }
    // 测试路径：成交回报直接进 done_cb，不依赖 DummyTrade / ServiceMap
    explicit DummyOrderbook(DoneCb done_cb):
        m_done_cb(std::move(done_cb))
    {
    }
    void AddEntrust(Order& entrust, const EntrustChangeCb& push_cb, const EntrustChangeCb& rsp_cb);
    void DelEntrust(int64_t entno, const EntrustChangeCb& push_cb, const WithdrawRspCb& rsp_cb);
    void AddDepth(const Depth& depth, const EntrustChangeCb& cb);

private:
    // 撮合核心逻辑
    template<bool IsBuy, typename EntMap>
    void MatchOrders(EntMap& ent_map, const EntrustChangeCb& cb);

    Depth m_depth {};
    // 买单簿按价格降序排列（高价优先）<price,<Entrust>>
    std::map<double, std::list<Order>, std::greater<>> m_buy_entrusts {};
    // 卖单簿按价格升序排列（低价优先）<price,<Entrust>>
    std::map<double, std::list<Order>> m_sell_entrusts {};
    // <entno,<px_iter,ent_iter>>
    std::unordered_map<int64_t,std::pair<
        std::map<double,std::list<Order>>::iterator,
        std::list<Order>::iterator>
    > m_entno_index;
    DummyTrade* m_dummy_trade {};
    DoneCb m_done_cb {};
};
