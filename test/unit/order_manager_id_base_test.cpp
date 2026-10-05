// 测试号段基数（启动高水位）—— 静态方法，无需实例化，不触碰共享内存
//
// 背景（方案 rev4 §8）：entno / tdno 的默认基数是"进程启动时刻(秒) × 1e9"，
// 因此"同一秒内重启"会让新旧号段重叠 —— 订单 REPLACE 互相覆盖、成交 INSERT 主键冲突；
// 浏览器侧按 `entno_gt` / `tdno_gt` 补查也会因此漏数据。
// OrderManager::SetIdBase 用"已见最大号 + 1"抬高基数来消除该窗口，本文件验证：
//   ① 初始基数落在时间基数附近；② 高水位高于时间基数时以高水位为准；
//   ③ 基数只抬高不降低（重复调用安全）；④ entno / tdno 两个基数互不干扰；
//   ⑤ 生成号严格单调递增。
//
// 注：GetMaxOrderNo / GetMaxTradeNo 依赖实例（构造会触碰共享内存），不在本文件覆盖。
#include <gtest/gtest.h>

#include "my_utc.h"
#include "order_manager.h"

namespace {

// 当前时间基数：与 OrderManager 内部使用的公式一致（启动时刻(秒) × 1e9）
int64_t TimeBase() {
    return MyUTC().Epoch10() * zrt::kGiga;
}

// ① 初始基数：静态初始化早于测试执行，故应 >0 且不超过"现在"的时间基数
TEST(OrderManagerIdBaseTest, InitialBaseIsTimeBased) {
    EXPECT_GT(OrderManager::GetOrderIdBase(), 0);
    EXPECT_GT(OrderManager::GetTradeIdBase(), 0);
    EXPECT_LE(OrderManager::GetOrderIdBase(), TimeBase());
    EXPECT_LE(OrderManager::GetTradeIdBase(), TimeBase());
}

// ② 高水位高于时间基数时，基数抬到 高水位+1，新号必然大于高水位
TEST(OrderManagerIdBaseTest, HighWaterWinsOverTimeBase) {
    const int64_t high = TimeBase() + 10'000'000;
    OrderManager::SetIdBase(high, high);

    EXPECT_EQ(OrderManager::GetOrderIdBase(), high + 1);
    EXPECT_EQ(OrderManager::GetTradeIdBase(), high + 1);
    // 生成的号必须严格大于历史最大号（这是"不与历史号重叠"的直接判据）
    EXPECT_GT(OrderManager::CreateOrderId(), high);
    EXPECT_GT(OrderManager::CreateTradeId(), high);
}

// ③ 基数只抬高不降低：传 0 / 传更小的值都不应把基数拉回去
TEST(OrderManagerIdBaseTest, BaseNeverGoesBackwards) {
    const int64_t high = TimeBase() + 20'000'000;
    OrderManager::SetIdBase(high, high);
    const int64_t base_order = OrderManager::GetOrderIdBase();
    const int64_t base_trade = OrderManager::GetTradeIdBase();

    OrderManager::SetIdBase(0, 0);            // 模拟"DB 不可用，拿不到高水位"
    OrderManager::SetIdBase(1, 1);            // 模拟"历史表几乎为空"
    OrderManager::SetIdBase(high - 1000, high - 1000);  // 更小的高水位

    EXPECT_EQ(OrderManager::GetOrderIdBase(), base_order);
    EXPECT_EQ(OrderManager::GetTradeIdBase(), base_trade);
}

// ④ entno / tdno 两个基数互相独立
TEST(OrderManagerIdBaseTest, OrderAndTradeBasesAreIndependent) {
    const int64_t high_order = TimeBase() + 30'000'000;
    const int64_t high_trade = TimeBase() + 40'000'000;
    OrderManager::SetIdBase(high_order, high_trade);

    EXPECT_EQ(OrderManager::GetOrderIdBase(), high_order + 1);
    EXPECT_EQ(OrderManager::GetTradeIdBase(), high_trade + 1);
}

// ⑤ 生成号严格单调递增（同一基数下连续取号）
TEST(OrderManagerIdBaseTest, GeneratedIdsAreStrictlyIncreasing) {
    const int64_t first_order = OrderManager::CreateOrderId();
    const int64_t second_order = OrderManager::CreateOrderId();
    const int64_t first_trade = OrderManager::CreateTradeId();
    const int64_t second_trade = OrderManager::CreateTradeId();

    EXPECT_GT(second_order, first_order);
    EXPECT_GT(second_trade, first_trade);
    // 同一时刻取的委托号与成交号互不影响（各自独立自增）
    EXPECT_GT(OrderManager::CreateOrderId(), second_order);
    EXPECT_GT(OrderManager::CreateTradeId(), second_trade);
}

}  // namespace
