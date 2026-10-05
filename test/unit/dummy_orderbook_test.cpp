// 测试真实 DummyOrderbook（src/websocket/dummy_orderbook.h）的撮合一逻辑
// 迁移自旧复制品测试（原文件在测试内重实现了撮合逻辑，已同名替换）。
// 已知真实语义（与旧复制品一致处直接沿用断言；不一致处以真实代码为准）：
//   - 撮合只用 depth level-0；quote_amt 在本趟撮合内随成交消耗（quote_amt -= trade_vol，
//     部成即置 quote_lvl=0 停止本趟）；AddDepth 整体替换 m_depth，趟间不累计
//   - AddEntrust 立即撮合，先 rsp_cb 后 push_cb（status '_2'）
//   - 成交经构造传入的 DoneCb 回报（测试通道，字段见 dummy_orderbook.cpp 接缝处）
// 已知隐患（锁定不修，见 P1 计划 Task 4）：trade_vol=min(entrust.amount, quote_amt) 用 amount
//   而非 remain——部成订单在下趟深度量大于其 remain 时理论超成，本套件刻意不覆盖该场景
#include <gtest/gtest.h>
#include <vector>
#include "dummy_orderbook.h"
#include "dict.h"   // OrderStatus / TradeSide

namespace {

// 构造单档行情（cnt 作为流动性标志：>0 才参与撮合；ex_time 固定便于断言 filled_time）
Depth MakeDepth(const double bid_px, const double bid_amt,
                const double ask_px, const double ask_amt) {
    Depth depth {};
    depth.bid_cnt = (bid_amt > 0) ? 1 : 0;
    depth.ask_cnt = (ask_amt > 0) ? 1 : 0;
    depth.bid_price[0] = bid_px;
    depth.bid_amount[0] = bid_amt;
    depth.ask_price[0] = ask_px;
    depth.ask_amount[0] = ask_amt;
    depth.ex_time = 1700000000000;
    return depth;
}

Order MakeOrder(const int64_t entno, const char side, const double px, const double amt) {
    Order order {};
    order.entno = entno;
    zrt::fill_field(order.bs_side, side);
    order.price = px;
    order.amount = amt;
    order.filled = 0;
    order.remain = amt;
    return order;
}

// 测试夹具：rsp/push 回调统一收集委托状态流，成交经 DoneCb 收集
struct BookFixture {
    std::vector<Order> entrust_events {};
    std::vector<Trade> trades {};
    DummyOrderbook book;

    BookFixture(): book([this](const Trade& t) { trades.push_back(t); }) {}

    void Add(Order& order) {
        book.AddEntrust(order,
                        [this](const Order& o) { entrust_events.push_back(o); },
                        [this](const Order& o) { entrust_events.push_back(o); });
    }
    void SetDepth(const Depth& d) {
        book.AddDepth(d, [this](const Order& o) { entrust_events.push_back(o); });
    }
    char LastStatus() const {
        return entrust_events.empty() ? '\0' : entrust_events.back().status;
    }
};

} // namespace

TEST(DummyOrderbookTest, BuyFillsAtAskPrice) {
    BookFixture f;
    f.SetDepth(MakeDepth(100.0, 5, 100.5, 5));
    Order order = MakeOrder(1, TradeSide::Buy, 101.0, 1.0);
    f.Add(order);
    ASSERT_EQ(f.trades.size(), 1u);
    EXPECT_DOUBLE_EQ(f.trades[0].td_px, 100.5);      // 按卖一价成交
    EXPECT_DOUBLE_EQ(f.trades[0].td_qty, 1.0);
    EXPECT_EQ(f.trades[0].td_side, TradeSide::Buy);
    EXPECT_EQ(f.trades[0].filled_time, 1700000000000);  // 来自 depth.ex_time（MakeDepth 固定值）
    EXPECT_EQ(f.LastStatus(), OrderStatus::_4);      // 全部成交
}

TEST(DummyOrderbookTest, SellFillsAtBidPrice) {
    BookFixture f;
    f.SetDepth(MakeDepth(100.0, 5, 100.5, 5));
    Order order = MakeOrder(2, TradeSide::Sell, 99.0, 1.0);
    f.Add(order);
    ASSERT_EQ(f.trades.size(), 1u);
    EXPECT_DOUBLE_EQ(f.trades[0].td_px, 100.0);      // 按买一价成交
    EXPECT_DOUBLE_EQ(f.trades[0].td_qty, 1.0);
    EXPECT_EQ(f.trades[0].td_side, TradeSide::Sell);
    EXPECT_EQ(f.LastStatus(), OrderStatus::_4);
}

TEST(DummyOrderbookTest, BuyNoMatchWhenBelowAsk) {
    BookFixture f;
    f.SetDepth(MakeDepth(0, 0, 100.0, 5));
    Order order = MakeOrder(3, TradeSide::Buy, 99.0, 1.0);   // 买价低于卖一
    f.Add(order);
    EXPECT_TRUE(f.trades.empty());
    EXPECT_EQ(f.LastStatus(), OrderStatus::_2);      // 仅已报
}

TEST(DummyOrderbookTest, SellNoMatchWhenAboveBid) {
    BookFixture f;
    f.SetDepth(MakeDepth(100.0, 5, 0, 0));
    Order order = MakeOrder(4, TradeSide::Sell, 101.0, 1.0); // 卖价高于买一
    f.Add(order);
    EXPECT_TRUE(f.trades.empty());
    EXPECT_EQ(f.LastStatus(), OrderStatus::_2);
}

TEST(DummyOrderbookTest, PartialFillThenRemainStaysInBook) {
    BookFixture f;
    f.SetDepth(MakeDepth(0, 0, 100.5, 1.0));         // 卖一只有 1.0
    Order order = MakeOrder(5, TradeSide::Buy, 101.0, 2.0);
    f.Add(order);
    ASSERT_EQ(f.trades.size(), 1u);
    EXPECT_DOUBLE_EQ(f.trades[0].td_qty, 1.0);
    EXPECT_EQ(f.LastStatus(), OrderStatus::_3);      // 部成
    // 余量仍在簿内：再来一档深度后继续成交
    f.SetDepth(MakeDepth(0, 0, 100.5, 1.0));
    ASSERT_EQ(f.trades.size(), 2u);
    EXPECT_DOUBLE_EQ(f.trades[1].td_qty, 1.0);
    EXPECT_EQ(f.LastStatus(), OrderStatus::_4);      // 累计 2.0 全成
}

TEST(DummyOrderbookTest, FullFillRemovesOrderFromBook) {
    BookFixture f;
    f.SetDepth(MakeDepth(0, 0, 100.5, 2.0));
    Order order = MakeOrder(6, TradeSide::Buy, 101.0, 1.0);
    f.Add(order);
    ASSERT_EQ(f.trades.size(), 1u);
    EXPECT_EQ(f.LastStatus(), OrderStatus::_4);
    // 订单已出簿：后续深度不再产生成交
    f.SetDepth(MakeDepth(0, 0, 100.5, 2.0));
    EXPECT_EQ(f.trades.size(), 1u);
}

TEST(DummyOrderbookTest, PricePriority_BuyHighPriceFillsFirst) {
    BookFixture f;
    // 空簿先后挂两笔买单（此时无深度不撮合）
    Order low = MakeOrder(1, TradeSide::Buy, 100.0, 1.0);
    Order high = MakeOrder(2, TradeSide::Buy, 101.0, 1.0);
    f.Add(low);
    f.Add(high);
    ASSERT_TRUE(f.trades.empty());
    // 深度到达，两笔价格都满足：价高者（entno=2）先成交
    // 注：quote_amt 单趟内随成交消耗，深度量 2.0 才能覆盖两笔各 1.0
    f.SetDepth(MakeDepth(0, 0, 99.5, 2.0));
    ASSERT_EQ(f.trades.size(), 2u);
    EXPECT_EQ(f.trades[0].ordno, 2);
    EXPECT_EQ(f.trades[1].ordno, 1);
}

TEST(DummyOrderbookTest, PricePriority_SellLowPriceFillsFirst) {
    BookFixture f;
    Order high = MakeOrder(1, TradeSide::Sell, 101.0, 1.0);
    Order low = MakeOrder(2, TradeSide::Sell, 100.0, 1.0);
    f.Add(high);
    f.Add(low);
    ASSERT_TRUE(f.trades.empty());
    // 买一 101.5 两笔都满足：价低者（entno=2）先成交（深度量 2.0 覆盖两笔）
    f.SetDepth(MakeDepth(101.5, 2.0, 0, 0));
    ASSERT_EQ(f.trades.size(), 2u);
    EXPECT_EQ(f.trades[0].ordno, 2);
    EXPECT_EQ(f.trades[1].ordno, 1);
}

TEST(DummyOrderbookTest, TimePriority_SamePriceFIFO) {
    BookFixture f;
    Order first = MakeOrder(1, TradeSide::Buy, 100.5, 1.0);
    Order second = MakeOrder(2, TradeSide::Buy, 100.5, 1.0);
    f.Add(first);
    f.Add(second);
    ASSERT_TRUE(f.trades.empty());
    f.SetDepth(MakeDepth(0, 0, 99.5, 2.0));
    ASSERT_EQ(f.trades.size(), 2u);
    EXPECT_EQ(f.trades[0].ordno, 1);                 // 同价先挂先成
    EXPECT_EQ(f.trades[1].ordno, 2);
}

TEST(DummyOrderbookTest, MultipleFillsFromMultipleDepth) {
    BookFixture f;
    Order order = MakeOrder(7, TradeSide::Buy, 101.0, 5.0);
    f.Add(order);
    f.SetDepth(MakeDepth(0, 0, 100.5, 2.0));         // 第一档：部成 2.0
    ASSERT_EQ(f.trades.size(), 1u);
    EXPECT_DOUBLE_EQ(f.trades[0].td_qty, 2.0);
    EXPECT_EQ(f.LastStatus(), OrderStatus::_3);
    f.SetDepth(MakeDepth(0, 0, 100.5, 3.0));         // 第二档：补足全成
    ASSERT_EQ(f.trades.size(), 2u);
    EXPECT_DOUBLE_EQ(f.trades[1].td_qty, 3.0);
    EXPECT_EQ(f.LastStatus(), OrderStatus::_4);
}

TEST(DummyOrderbookTest, EmptyDepth_NoFill) {
    BookFixture f;
    Order order = MakeOrder(8, TradeSide::Buy, 101.0, 1.0);
    f.Add(order);                                     // 从未设置深度（cnt 全 0）
    EXPECT_TRUE(f.trades.empty());
    f.SetDepth(MakeDepth(0, 0, 0, 0));                // 显式空深度
    EXPECT_TRUE(f.trades.empty());
    EXPECT_EQ(f.LastStatus(), OrderStatus::_2);
}
