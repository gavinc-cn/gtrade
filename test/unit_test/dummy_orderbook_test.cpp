#include "pch.h"
#include <gtest/gtest.h>
#include <map>
#include <list>
#include <vector>
#include <cmath>

namespace {

struct TestOrder {
    int64_t entno {};
    char bs_side {};
    double price {};
    double amount {};
    double filled {};
    double remain {};
    char status {};
};

class TestOrderbook {
public:
    std::map<double, std::list<TestOrder>, std::greater<>> m_buy_orders;
    std::map<double, std::list<TestOrder>> m_sell_orders;

    double m_bid_price {};
    double m_bid_amount {};
    double m_ask_price {};
    double m_ask_amount {};
    int m_bid_cnt {};
    int m_ask_cnt {};

    struct FillInfo {
        int64_t entno {};
        double match_px {};
        double match_qty {};
        char new_status {};
    };
    std::vector<FillInfo> m_fills;

    void SetDepth(double bid_px, double bid_amt, double ask_px, double ask_amt) {
        m_bid_price = bid_px;
        m_bid_amount = bid_amt;
        m_ask_price = ask_px;
        m_ask_amount = ask_amt;
        m_bid_cnt = (bid_amt > 0) ? 1 : 0;
        m_ask_cnt = (ask_amt > 0) ? 1 : 0;
    }

    void AddBuyOrder(int64_t entno, double price, double amount) {
        TestOrder order;
        order.entno = entno;
        order.bs_side = 'B';
        order.price = price;
        order.amount = amount;
        order.remain = amount;
        m_buy_orders[price].push_back(order);
    }

    void AddSellOrder(int64_t entno, double price, double amount) {
        TestOrder order;
        order.entno = entno;
        order.bs_side = 'S';
        order.price = price;
        order.amount = amount;
        order.remain = amount;
        m_sell_orders[price].push_back(order);
    }

    void Match() {
        MatchBuySide();
        MatchSellSide();
    }

private:
    static bool Equal(double a, double b) {
        return std::fabs(a - b) < 1e-9;
    }

    void MatchBuySide() {
        if (m_bid_cnt == 0 && m_ask_cnt == 0) return;
        if (m_buy_orders.empty()) return;

        double& quote_price = m_ask_price;
        double& quote_amt = m_ask_amount;
        int& quote_lvl = m_ask_cnt;

        for (auto px_iter = m_buy_orders.begin();
             px_iter != m_buy_orders.end() && px_iter->first >= quote_price && quote_lvl > 0;) {
            const double match_px = quote_price;
            std::list<TestOrder>& same_price_ents = px_iter->second;
            while (!same_price_ents.empty() && quote_amt > 0) {
                auto& entrust = same_price_ents.front();
                const double trade_vol = std::min(entrust.amount, quote_amt);
                entrust.filled += trade_vol;
                entrust.remain = entrust.amount - entrust.filled;
                quote_amt -= trade_vol;
                if (Equal(entrust.remain, 0.0)) {
                    entrust.status = '4';
                    m_fills.push_back({entrust.entno, match_px, trade_vol, '4'});
                    same_price_ents.pop_front();
                } else {
                    entrust.status = '3';
                    m_fills.push_back({entrust.entno, match_px, trade_vol, '3'});
                    quote_lvl = 0;
                    break;
                }
            }
            px_iter = same_price_ents.empty() ? m_buy_orders.erase(px_iter) : ++px_iter;
        }
    }

    void MatchSellSide() {
        if (m_bid_cnt == 0 && m_ask_cnt == 0) return;
        if (m_sell_orders.empty()) return;

        double& quote_price = m_bid_price;
        double& quote_amt = m_bid_amount;
        int& quote_lvl = m_bid_cnt;

        for (auto px_iter = m_sell_orders.begin();
             px_iter != m_sell_orders.end() && px_iter->first <= quote_price && quote_lvl > 0;) {
            const double match_px = quote_price;
            std::list<TestOrder>& same_price_ents = px_iter->second;
            while (!same_price_ents.empty() && quote_amt > 0) {
                auto& entrust = same_price_ents.front();
                const double trade_vol = std::min(entrust.amount, quote_amt);
                entrust.filled += trade_vol;
                entrust.remain = entrust.amount - entrust.filled;
                quote_amt -= trade_vol;
                if (Equal(entrust.remain, 0.0)) {
                    entrust.status = '4';
                    m_fills.push_back({entrust.entno, match_px, trade_vol, '4'});
                    same_price_ents.pop_front();
                } else {
                    entrust.status = '3';
                    m_fills.push_back({entrust.entno, match_px, trade_vol, '3'});
                    quote_lvl = 0;
                    break;
                }
            }
            px_iter = same_price_ents.empty() ? m_sell_orders.erase(px_iter) : ++px_iter;
        }
    }
};

} // namespace

TEST(DummyOrderbookTest, BuyFillsAtAskPrice) {
    TestOrderbook ob;
    ob.SetDepth(0, 0, 100.5, 5.0);
    ob.AddBuyOrder(1, 101.0, 1.0);
    ob.Match();
    ASSERT_EQ(ob.m_fills.size(), 1u);
    EXPECT_EQ(ob.m_fills[0].entno, 1);
    EXPECT_DOUBLE_EQ(ob.m_fills[0].match_px, 100.5);
    EXPECT_DOUBLE_EQ(ob.m_fills[0].match_qty, 1.0);
    EXPECT_EQ(ob.m_fills[0].new_status, '4');
}

TEST(DummyOrderbookTest, SellFillsAtBidPrice) {
    TestOrderbook ob;
    ob.SetDepth(100.0, 5.0, 0, 0);
    ob.AddSellOrder(1, 99.0, 1.0);
    ob.Match();
    ASSERT_EQ(ob.m_fills.size(), 1u);
    EXPECT_EQ(ob.m_fills[0].entno, 1);
    EXPECT_DOUBLE_EQ(ob.m_fills[0].match_px, 100.0);
    EXPECT_DOUBLE_EQ(ob.m_fills[0].match_qty, 1.0);
    EXPECT_EQ(ob.m_fills[0].new_status, '4');
}

TEST(DummyOrderbookTest, BuyNoMatchWhenBelowAsk) {
    TestOrderbook ob;
    ob.SetDepth(0, 0, 100.0, 5.0);
    ob.AddBuyOrder(1, 99.0, 1.0);
    ob.Match();
    EXPECT_TRUE(ob.m_fills.empty());
}

TEST(DummyOrderbookTest, SellNoMatchWhenAboveBid) {
    TestOrderbook ob;
    ob.SetDepth(100.0, 5.0, 0, 0);
    ob.AddSellOrder(1, 101.0, 1.0);
    ob.Match();
    EXPECT_TRUE(ob.m_fills.empty());
}

TEST(DummyOrderbookTest, PartialFill) {
    TestOrderbook ob;
    ob.SetDepth(0, 0, 100.0, 1.0);
    ob.AddBuyOrder(1, 101.0, 2.0);
    ob.Match();
    ASSERT_EQ(ob.m_fills.size(), 1u);
    EXPECT_EQ(ob.m_fills[0].entno, 1);
    EXPECT_DOUBLE_EQ(ob.m_fills[0].match_qty, 1.0);
    EXPECT_EQ(ob.m_fills[0].new_status, '3');
    EXPECT_FALSE(ob.m_buy_orders.empty());
}

TEST(DummyOrderbookTest, FullFill) {
    TestOrderbook ob;
    ob.SetDepth(0, 0, 100.0, 2.0);
    ob.AddBuyOrder(1, 101.0, 1.0);
    ob.Match();
    ASSERT_EQ(ob.m_fills.size(), 1u);
    EXPECT_EQ(ob.m_fills[0].new_status, '4');
    EXPECT_TRUE(ob.m_buy_orders.empty());
}

TEST(DummyOrderbookTest, PricePriority_BuyHighPriceFillsFirst) {
    TestOrderbook ob;
    ob.SetDepth(0, 0, 100.5, 1.0);
    ob.AddBuyOrder(1, 100.0, 1.0);
    ob.AddBuyOrder(2, 101.0, 1.0);
    ob.Match();
    ASSERT_EQ(ob.m_fills.size(), 1u);
    EXPECT_EQ(ob.m_fills[0].entno, 2);
}

TEST(DummyOrderbookTest, PricePriority_SellLowPriceFillsFirst) {
    TestOrderbook ob;
    ob.SetDepth(100.5, 1.0, 0, 0);
    ob.AddSellOrder(1, 100.0, 1.0);
    ob.AddSellOrder(2, 101.0, 1.0);
    ob.Match();
    ASSERT_EQ(ob.m_fills.size(), 1u);
    EXPECT_EQ(ob.m_fills[0].entno, 1);
}

TEST(DummyOrderbookTest, TimePriority_SamePrice_FIFO) {
    TestOrderbook ob;
    ob.SetDepth(0, 0, 100.0, 5.0);
    ob.AddBuyOrder(1, 101.0, 1.0);
    ob.AddBuyOrder(2, 101.0, 1.0);
    ob.Match();
    ASSERT_EQ(ob.m_fills.size(), 2u);
    EXPECT_EQ(ob.m_fills[0].entno, 1);
    EXPECT_EQ(ob.m_fills[1].entno, 2);
}

TEST(DummyOrderbookTest, MultipleFillsFromMultipleDepth) {
    TestOrderbook ob;
    ob.SetDepth(0, 0, 100.0, 2.0);
    ob.AddBuyOrder(1, 101.0, 5.0);
    ob.Match();
    ASSERT_EQ(ob.m_fills.size(), 1u);
    EXPECT_EQ(ob.m_fills[0].new_status, '3');
    EXPECT_DOUBLE_EQ(ob.m_fills[0].match_qty, 2.0);

    ob.SetDepth(0, 0, 100.0, 3.0);
    ob.Match();
    ASSERT_EQ(ob.m_fills.size(), 2u);
    EXPECT_EQ(ob.m_fills[1].new_status, '4');
    EXPECT_DOUBLE_EQ(ob.m_fills[1].match_qty, 3.0);
    EXPECT_TRUE(ob.m_buy_orders.empty());
}

TEST(DummyOrderbookTest, EmptyDepth_NoFill) {
    TestOrderbook ob;
    ob.AddBuyOrder(1, 101.0, 1.0);
    ob.Match();
    EXPECT_TRUE(ob.m_fills.empty());
}
