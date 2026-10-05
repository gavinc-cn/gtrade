// 测试补查（web_server 断线重连后按游标补齐）的筛选逻辑
//
// 背景（方案 rev4 §5）：委托按 entno、成交按 tdno 是引擎本地生成的单调号，
// 客户端重连后用它们补齐断线期间的变化：
//   - `entno_gt` / `tdno_gt` 游标模式：发现离线期间**新增**的记录（升序，必须有上限）
//   - `by_entnos` 批量模式：刷新客户端**已知在途**委托的最新状态（含终态，缺号跳过）
// 这两个选择器是纯函数（`OrderManager::SelectAfterCursor` / `SelectByKeys`），
// 不需要构造 OrderManager 实例（构造会映射共享内存），故本文件直接测纯逻辑。
#include <gtest/gtest.h>

#include <unordered_map>
#include <vector>

#include "order_manager.h"

namespace {

// 用 int64_t 作为记录类型即可覆盖选择器的全部逻辑（真实类型为 Order / Trade）
using Rows = std::unordered_map<int64_t, int64_t>;

Rows MakeRows(const std::vector<int64_t>& keys) {
    Rows rows {};
    for (const int64_t k : keys) {
        rows[k] = k;  // 值 = 键，断言里拿到的元素即号本身
    }
    return rows;
}

// ① 只取游标之后的记录，且按号升序（插入顺序是乱序，验证排序生效）
TEST(OrderManagerQueryTest, SelectAfterCursorIsAscendingAndExclusive) {
    const Rows rows = MakeRows({30, 10, 50, 20, 40});
    const std::vector<int64_t> got = OrderManager::SelectAfterCursor(rows, 20, 0);

    ASSERT_EQ(got.size(), 3u);
    EXPECT_EQ(got[0], 30);
    EXPECT_EQ(got[1], 40);
    EXPECT_EQ(got[2], 50);
    // 游标本身被排除（严格大于）
    for (const int64_t v : got) {
        EXPECT_GT(v, 20);
    }
}

// ② limit 生效：截断到前 limit 条（升序后的前 limit 条，不是任意 limit 条）
TEST(OrderManagerQueryTest, SelectAfterCursorRespectsLimit) {
    const Rows rows = MakeRows({30, 10, 50, 20, 40});
    const std::vector<int64_t> got = OrderManager::SelectAfterCursor(rows, 0, 2);

    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0], 10);
    EXPECT_EQ(got[1], 20);
}

// ③ limit==0 视为不限；游标超过所有号时返回空（"到底了"的判据）
TEST(OrderManagerQueryTest, SelectAfterCursorEdgeCases) {
    const Rows rows = MakeRows({10, 20, 30});
    EXPECT_EQ(OrderManager::SelectAfterCursor(rows, 0, 0).size(), 3u);
    EXPECT_TRUE(OrderManager::SelectAfterCursor(rows, 30, 0).empty());
    EXPECT_TRUE(OrderManager::SelectAfterCursor(Rows {}, 0, 0).empty());
}

// ④ 分批拉取：用上一批最后一条继续拉，不重不漏（客户端补查的实际用法）
TEST(OrderManagerQueryTest, CursorPagingCoversEveryRowExactlyOnce) {
    const Rows rows = MakeRows({5, 1, 4, 2, 3, 6, 7});
    std::vector<int64_t> collected {};
    int64_t cursor = 0;
    for (int page = 0; page < 10; ++page) {
        const std::vector<int64_t> batch = OrderManager::SelectAfterCursor(rows, cursor, 3);
        if (batch.empty()) {
            break;
        }
        collected.insert(collected.end(), batch.begin(), batch.end());
        cursor = batch.back();
    }
    ASSERT_EQ(collected.size(), rows.size());
    for (size_t i = 0; i < collected.size(); ++i) {
        EXPECT_EQ(collected[i], static_cast<int64_t>(i + 1)) << "index=" << i;
    }
}

// ⑤ by_entnos：按请求顺序返回，缺失的号跳过（客户端可能带着已过期的号）
TEST(OrderManagerQueryTest, SelectByKeysKeepsRequestOrderAndSkipsMissing) {
    const Rows rows = MakeRows({10, 20, 30});
    const int64_t query[] = {30, 999, 10};

    const std::vector<int64_t> got = OrderManager::SelectByKeys(rows, query, 3);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0], 30);   // 与请求顺序一致（客户端按此覆盖式合并）
    EXPECT_EQ(got[1], 10);
}

// ⑥ by_entnos：空列表 / 空指针 / 全缺失都不崩
TEST(OrderManagerQueryTest, SelectByKeysEdgeCases) {
    const Rows rows = MakeRows({10});
    const int64_t query[] = {10};
    EXPECT_TRUE(OrderManager::SelectByKeys(rows, query, 0).empty());
    EXPECT_TRUE(OrderManager::SelectByKeys(rows, nullptr, 3).empty());

    const int64_t missing[] = {1, 2, 3};
    EXPECT_TRUE(OrderManager::SelectByKeys(rows, missing, 3).empty());
}

}  // namespace
