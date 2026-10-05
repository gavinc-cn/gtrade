#include <gtest/gtest.h>
#include "instrument_scope.h"

TEST(InstrumentScopeTest, DiffScopeAddAndRemove) {
    const ScopeSet old_scope {{"okx", "BTC-USDT-SWAP", "SWAP"}, {"okx", "ETH-USDT-SWAP", "SWAP"}};
    const ScopeSet wanted {{"okx", "ETH-USDT-SWAP", "SWAP"}, {"okx", "SOL-USDT-SWAP", "SWAP"}};
    const ScopeChange change = DiffScope(old_scope, wanted);
    ASSERT_EQ(change.added.size(), 1u);
    EXPECT_EQ(change.added[0], ScopeKey("okx", "SOL-USDT-SWAP", "SWAP"));
    ASSERT_EQ(change.removed.size(), 1u);
    EXPECT_EQ(change.removed[0], ScopeKey("okx", "BTC-USDT-SWAP", "SWAP"));
}

// 同一 instId 的不同 inst_type 是两条独立范围条目：差集必须按三元组算，不能按 instId 归并
TEST(InstrumentScopeTest, DiffScopeTreatsInstTypeAsPartOfTheKey) {
    const ScopeSet old_scope {{"okx", "BTC-USDT", "SPOT"}};
    const ScopeSet wanted {{"okx", "BTC-USDT", "MARGIN"}};
    const ScopeChange change = DiffScope(old_scope, wanted);
    ASSERT_EQ(change.removed.size(), 1u);
    ASSERT_EQ(change.added.size(), 1u);
    EXPECT_EQ(change.removed[0], ScopeKey("okx", "BTC-USDT", "SPOT"));
    EXPECT_EQ(change.added[0], ScopeKey("okx", "BTC-USDT", "MARGIN"));
}

// 退订连坐护栏：行情订阅键不含 inst_type，同 instId 的多个范围条目共享一条订阅，
// 只有最后一个条目移除时才允许下发退订（"退 MARGIN 连坐退掉 SPOT"的成因）
// 契约：调用方**先把 key 从 scope 里 erase 掉**再问"还有没有兄弟"。
TEST(InstrumentScopeTest, HasOtherInstTypeGuardsSharedSubscription) {
    ScopeSet scope {{"okx", "BTC-USDT", "SPOT"}, {"okx", "BTC-USDT", "MARGIN"}, {"okx", "ETH-USDT", "SPOT"}};
    const ScopeKey spot {"okx", "BTC-USDT", "SPOT"};
    const ScopeKey margin {"okx", "BTC-USDT", "MARGIN"};
    // 兄弟条目（同 instId 的另一种类型）还在 → 不能退订
    scope.erase(spot);
    EXPECT_TRUE(HasOtherInstType(scope, spot));
    // 最后一个条目也移除 → 可以退订
    scope.erase(margin);
    EXPECT_FALSE(HasOtherInstType(scope, margin));
    // 只比 (market, inst_id)：别的 instId 不算兄弟
    scope.erase(ScopeKey("okx", "ETH-USDT", "SPOT"));
    EXPECT_FALSE(HasOtherInstType(scope, ScopeKey("okx", "ETH-USDT", "SPOT")));
    // 市场也参与比较：同 instId 不同市场不算兄弟
    const ScopeSet cross_market {{"okx", "BTC-USDT", "MARGIN"}};
    EXPECT_FALSE(HasOtherInstType(cross_market, ScopeKey("okx_dummy", "BTC-USDT", "SPOT")));
    // 空范围
    EXPECT_FALSE(HasOtherInstType(ScopeSet {}, spot));
}

TEST(InstrumentScopeTest, AddOwnerReturnsFirstFlag) {
    QuoteOwnerMap owners {};
    const QuoteSubKey key {"depth1", "okx", "BTC-USDT-SWAP"};
    EXPECT_TRUE(AddOwner(owners, key, "__scope__"));   // 首个 owner → 需要下发订阅
    EXPECT_FALSE(AddOwner(owners, key, "strat_1"));    // 已有 owner → 不重复订阅
    EXPECT_EQ(owners[key].size(), 2u);
}

// 同一 instId 的两种类型共用一个订阅键，但 k_scope_owner 在引用计数里只算一次 ——
// 这正是必须靠 HasOtherInstType 归并、而不能指望 RemoveOwner 兜住的原因
TEST(InstrumentScopeTest, ScopeOwnerCountsOnceForSharedSubscription) {
    QuoteOwnerMap owners {};
    const QuoteSubKey key {"depth1", "okx", "BTC-USDT"};
    EXPECT_TRUE(AddOwner(owners, key, "__scope__"));   // SPOT 条目先登记
    EXPECT_FALSE(AddOwner(owners, key, "__scope__"));  // MARGIN 条目：同一 owner，不重复计数
    EXPECT_EQ(owners[key].size(), 1u);
    EXPECT_TRUE(RemoveOwner(owners, key, "__scope__"));   // 移除一次就归零 → 不加护栏就会误退订
}

TEST(InstrumentScopeTest, RemoveOwnerKeepsWhenOthersRemain) {
    QuoteOwnerMap owners {};
    const QuoteSubKey key {"depth1", "okx", "BTC-USDT-SWAP"};
    AddOwner(owners, key, "__scope__");
    AddOwner(owners, key, "strat_1");
    EXPECT_FALSE(RemoveOwner(owners, key, "__scope__"));  // 策略仍在用 → 不下发退订
    EXPECT_TRUE(RemoveOwner(owners, key, "strat_1"));     // 最后一个 owner → 需要下发退订
    EXPECT_EQ(owners.count(key), 0u);
}

TEST(InstrumentScopeTest, RemoveAllOwnedByCollectsEmptiedKeys) {
    QuoteOwnerMap owners {};
    AddOwner(owners, {"depth1", "okx", "BTC-USDT-SWAP"}, "strat_1");
    AddOwner(owners, {"depth1", "okx", "ETH-USDT-SWAP"}, "strat_1");
    AddOwner(owners, {"depth1", "okx", "ETH-USDT-SWAP"}, "strat_2");
    const std::vector<QuoteSubKey> emptied = RemoveAllOwnedBy(owners, "strat_1");
    ASSERT_EQ(emptied.size(), 1u);
    EXPECT_EQ(emptied[0], QuoteSubKey("depth1", "okx", "BTC-USDT-SWAP"));
    EXPECT_EQ(owners.size(), 1u);   // ETH-USDT-SWAP 仍归 strat_2
}
