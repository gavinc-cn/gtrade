// 测试真实 OrderManager 的委托状态机（静态方法，无需实例化，不触碰共享内存）
// 迁移自旧复制品测试 test/unit/order_manager_test.cpp（本任务中删除）：
//   旧 CheckEntrustStatus(old, new) == true  ⇔  OrderManager::IsForwardStatus(old, new)
// 断言取值逐条拷贝自旧文件；如旧文件与生产 kForbiddenTransitions（order_manager.cpp:326-379）
// 不一致，以生产表为准并在任务汇报中记录差异。
#include <gtest/gtest.h>
#include "order_manager.h"
#include "dict.h"   // OrderStatus

namespace {

// 旧用例 1/4 合并：相同状态、终态（_4/_6/_8/_9）一律不前进——全量扫射（12 个状态值），独立于实现表
// 注：旧用例 5（_ConstexprVerification，对复制品自身 constexpr 表的 static_assert）无对应物——
//     真实表在 order_manager.cpp 匿名命名空间内外部不可见，由本文件扫射+移植用例替代其覆盖意图
TEST(OrderManagerStatusTest, SameStateNeverForward) {
    for (const char s : {OrderStatus::_0, OrderStatus::_1, OrderStatus::_2, OrderStatus::_3,
                         OrderStatus::_4, OrderStatus::_5, OrderStatus::_6, OrderStatus::_7,
                         OrderStatus::_8, OrderStatus::_9, OrderStatus::_I, OrderStatus::_A}) {
        EXPECT_FALSE(OrderManager::IsForwardStatus(s, s)) << "status=" << s;
    }
}

TEST(OrderManagerStatusTest, TerminalStatesNeverForward) {
    for (const char old_s : {OrderStatus::_4, OrderStatus::_6, OrderStatus::_8, OrderStatus::_9}) {
        for (const char new_s : {OrderStatus::_0, OrderStatus::_1, OrderStatus::_2, OrderStatus::_3,
                                 OrderStatus::_4, OrderStatus::_5, OrderStatus::_6, OrderStatus::_7,
                                 OrderStatus::_8, OrderStatus::_9, OrderStatus::_I, OrderStatus::_A}) {
            EXPECT_FALSE(OrderManager::IsForwardStatus(old_s, new_s))
                    << "old=" << old_s << " new=" << new_s;
        }
    }
}

// 旧用例 2（CompileTimeStateMachine_ValidTransitions）移植：21 条断言，取值逐条拷贝自旧文件
TEST(OrderManagerStatusTest, ValidTransitions) {
    // 从未报(0)可以转到任何状态
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_0, OrderStatus::_1));
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_0, OrderStatus::_2));
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_0, OrderStatus::_3));
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_0, OrderStatus::_4));
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_0, OrderStatus::_5));
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_0, OrderStatus::_9));

    // 从正报(1)可以转到已报(2)、部成(3)、全成(4)等
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_1, OrderStatus::_2));
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_1, OrderStatus::_3));
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_1, OrderStatus::_4));
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_1, OrderStatus::_5));

    // 从已报(2)可以转到部成(3)、全成(4)、已报待撤(5)等
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_2, OrderStatus::_3));
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_2, OrderStatus::_4));
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_2, OrderStatus::_5));
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_2, OrderStatus::_6));

    // 从部成(3)可以转到全成(4)、部成待撤(7)、部成部撤(8)
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_3, OrderStatus::_4));
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_3, OrderStatus::_7));
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_3, OrderStatus::_8));

    // 从已报待撤(5)可以转到场内撤单(6)
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_5, OrderStatus::_6));
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_5, OrderStatus::_3));

    // 从部成待撤(7)可以转到部成部撤(8)
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_7, OrderStatus::_8));
    EXPECT_TRUE(OrderManager::IsForwardStatus(OrderStatus::_7, OrderStatus::_4));
}

// 旧用例 3（CompileTimeStateMachine_InvalidTransitions）移植：17 条断言，取值逐条拷贝自旧文件
TEST(OrderManagerStatusTest, InvalidTransitions) {
    // 正报(1)不能回到未报(0)
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_1, OrderStatus::_0));

    // 已报(2)不能回到未报(0)和正报(1)
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_2, OrderStatus::_0));
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_2, OrderStatus::_1));

    // 部成(3)不能回到前期状态
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_3, OrderStatus::_0));
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_3, OrderStatus::_1));
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_3, OrderStatus::_2));
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_3, OrderStatus::_5));
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_3, OrderStatus::_6));

    // 已报待撤(5)不能回到未报(0)和正报(1)
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_5, OrderStatus::_0));
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_5, OrderStatus::_1));

    // 部成待撤(7)不能回到前期状态
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_7, OrderStatus::_0));
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_7, OrderStatus::_1));
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_7, OrderStatus::_2));
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_7, OrderStatus::_5));
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_7, OrderStatus::_6));

    // 冻结(I)不能回到未报(0)和正报(1)
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_I, OrderStatus::_0));
    EXPECT_FALSE(OrderManager::IsForwardStatus(OrderStatus::_I, OrderStatus::_1));
}

} // namespace
