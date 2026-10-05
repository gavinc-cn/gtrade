// 打桩：unit_test 不链接 websocket/dummy_trade.cpp（其构造依赖完整运行环境），
// 这里仅提供 DummyOrderbook 反向引用到的 DummyTrade::PushDone 空实现。
// 注意：测试路径经 DoneCb 获取成交回报，运行时不会执行到本 stub。
// 依赖 test/CMakeLists.txt 的 include 顺序（websocket 先于 backtest）使本文件的
// #include "dummy_trade.h" 解析到 websocket 版；backtest/dummy_trade.h 存在同名全局类。
#include "dummy_trade.h"

void DummyTrade::PushDone(const double match_px, const double trade_vol, const Order& entrust) {
    (void)match_px;
    (void)trade_vol;
    (void)entrust;
}
