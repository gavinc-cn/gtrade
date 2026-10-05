#include <gtest/gtest.h>
#include "i_strategy_engine.h"
#include "zrtools/zrt_misc.h"

// 行情订阅主键 GetPKey(QuoteSub) 的回归测试：
// 键必须是「内容键」——同内容的不同 QuoteSub 实例要互相命中（erase 生效、insert 去重），
// 而不是按内存地址比较（历史缺陷：char[] 直接进 make_tuple 会衰减成 const char*）。

namespace {

// 构造内容为 (channel, market, inst_id) 的 QuoteSub；strat_id 留空
QuoteSub MakeQuoteSub(const std::string& channel, const std::string& market, const std::string& inst_id) {
    QuoteSub sub {};
    zrt::fill_field(sub.channel, channel);
    zrt::fill_field(sub.market, market);
    zrt::fill_field(sub.inst_id, inst_id);
    return sub;
}

}  // namespace

// 用例 1：同内容、不同实例的 key 必须能命中既有条目（m_sub_map.erase 场景）
TEST(QuoteSubKeyTest, EraseHitsWithSameContentDifferentInstance) {
    const QuoteSub a = MakeQuoteSub("depth1", "okx", "BTC-USDT-SWAP");
    const QuoteSub b = MakeQuoteSub("depth1", "okx", "BTC-USDT-SWAP");
    zrt::TupleHashMap<QuoteSub> m {};
    m[GetPKey(a)] = a;
    ASSERT_EQ(m.size(), 1u);
    m.erase(GetPKey(b));
    EXPECT_EQ(m.size(), 0u);  // 修复前这里是 1：地址键匹配不到，erase 静默失效
}

// 用例 2：同内容 insert 只保留一条（去重），不同内容才是新条目
TEST(QuoteSubKeyTest, InsertDeduplicatesSameContent) {
    const QuoteSub a = MakeQuoteSub("depth1", "okx", "BTC-USDT-SWAP");
    const QuoteSub b = MakeQuoteSub("depth1", "okx", "BTC-USDT-SWAP");
    const QuoteSub c = MakeQuoteSub("depth1", "okx", "ETH-USDT-SWAP");
    zrt::TupleHashMap<QuoteSub> m {};
    m[GetPKey(a)] = a;
    m[GetPKey(b)] = b;
    ASSERT_EQ(m.size(), 1u);
    m[GetPKey(c)] = c;
    EXPECT_EQ(m.size(), 2u);  // 不同 inst_id 仍各占一条
    EXPECT_STREQ(m[GetPKey(a)].inst_id, "BTC-USDT-SWAP");
}
