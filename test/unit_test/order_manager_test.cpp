//
// Test for OrderManager::CheckEntrustStatus compile-time state machine
//

#include "gtest/gtest.h"
#include "dict.h"

// Forward declaration - we only test the logic, not the full OrderManager
namespace {
    // 使用两个 uint64_t 表示 128 位的状态转换掩码
    struct StatusMask {
        uint64_t low;   // 表示 ASCII 0-63
        uint64_t high;  // 表示 ASCII 64-127

        constexpr StatusMask() : low(0), high(0) {}

        constexpr void set(const char c) {
            const size_t pos = static_cast<size_t>(c);
            if (pos < 64) {
                low |= (1ULL << pos);
            } else {
                high |= (1ULL << (pos - 64));
            }
        }

        constexpr bool test(const char c) const {
            const size_t pos = static_cast<size_t>(c);
            if (pos < 64) {
                return (low & (1ULL << pos)) != 0;
            } else {
                return (high & (1ULL << (pos - 64))) != 0;
            }
        }
    };

    // 编译期生成禁止转换表
    constexpr auto BuildForbiddenTransitions() {
        std::array<StatusMask, 128> transitions {};

        auto forbid = [&](const char from, const std::initializer_list<char> to_list) {
            for (const char to : to_list) {
                transitions[static_cast<size_t>(from)].set(to);
            }
        };

        forbid(OrderStatus::_0, {});
        forbid(OrderStatus::_1, {OrderStatus::_0});
        forbid(OrderStatus::_2, {OrderStatus::_0, OrderStatus::_1});
        forbid(OrderStatus::_3, {OrderStatus::_0, OrderStatus::_1, OrderStatus::_2,
                                  OrderStatus::_5, OrderStatus::_6});
        forbid(OrderStatus::_5, {OrderStatus::_0, OrderStatus::_1});
        forbid(OrderStatus::_7, {OrderStatus::_0, OrderStatus::_1, OrderStatus::_2,
                                  OrderStatus::_5, OrderStatus::_6});
        forbid(OrderStatus::_I, {OrderStatus::_0, OrderStatus::_1});

        return transitions;
    }

    constexpr auto kForbiddenTransitions = BuildForbiddenTransitions();

    bool CheckEntrustStatus(const char old_status, const char new_status) {
        // 终止状态判断
        if (old_status == new_status ||
            old_status == OrderStatus::_4 ||
            old_status == OrderStatus::_6 ||
            old_status == OrderStatus::_8 ||
            old_status == OrderStatus::_9) {
            return false;
        }

        return !kForbiddenTransitions[static_cast<size_t>(old_status)].test(new_status);
    }

} // anonymous namespace

TEST(OrderManagerTest, CompileTimeStateMachine_ValidTransitions) {
    // 测试有效的状态转换

    // 从未报(0)可以转到任何状态
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_0, OrderStatus::_1));
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_0, OrderStatus::_2));
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_0, OrderStatus::_3));
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_0, OrderStatus::_4));
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_0, OrderStatus::_5));
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_0, OrderStatus::_9));

    // 从正报(1)可以转到已报(2)、部成(3)、全成(4)等
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_1, OrderStatus::_2));
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_1, OrderStatus::_3));
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_1, OrderStatus::_4));
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_1, OrderStatus::_5));

    // 从已报(2)可以转到部成(3)、全成(4)、已报待撤(5)等
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_2, OrderStatus::_3));
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_2, OrderStatus::_4));
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_2, OrderStatus::_5));
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_2, OrderStatus::_6));

    // 从部成(3)可以转到全成(4)、部成待撤(7)、部成部撤(8)
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_3, OrderStatus::_4));
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_3, OrderStatus::_7));
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_3, OrderStatus::_8));

    // 从已报待撤(5)可以转到场内撤单(6)
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_5, OrderStatus::_6));
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_5, OrderStatus::_3));

    // 从部成待撤(7)可以转到部成部撤(8)
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_7, OrderStatus::_8));
    EXPECT_TRUE(CheckEntrustStatus(OrderStatus::_7, OrderStatus::_4));
}

TEST(OrderManagerTest, CompileTimeStateMachine_InvalidTransitions) {
    // 测试无效的状态转换（禁止倒退）

    // 正报(1)不能回到未报(0)
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_1, OrderStatus::_0));

    // 已报(2)不能回到未报(0)和正报(1)
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_2, OrderStatus::_0));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_2, OrderStatus::_1));

    // 部成(3)不能回到前期状态
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_3, OrderStatus::_0));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_3, OrderStatus::_1));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_3, OrderStatus::_2));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_3, OrderStatus::_5));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_3, OrderStatus::_6));

    // 已报待撤(5)不能回到未报(0)和正报(1)
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_5, OrderStatus::_0));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_5, OrderStatus::_1));

    // 部成待撤(7)不能回到前期状态
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_7, OrderStatus::_0));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_7, OrderStatus::_1));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_7, OrderStatus::_2));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_7, OrderStatus::_5));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_7, OrderStatus::_6));

    // 冻结(I)不能回到未报(0)和正报(1)
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_I, OrderStatus::_0));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_I, OrderStatus::_1));
}

TEST(OrderManagerTest, CompileTimeStateMachine_TerminalStates) {
    // 测试终止状态（不允许任何转换）

    // 全部成交(4)是终止状态
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_4, OrderStatus::_0));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_4, OrderStatus::_1));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_4, OrderStatus::_2));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_4, OrderStatus::_3));

    // 场内撤单(6)是终止状态
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_6, OrderStatus::_0));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_6, OrderStatus::_5));

    // 部成部撤(8)是终止状态
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_8, OrderStatus::_0));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_8, OrderStatus::_7));

    // 废单(9)是终止状态
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_9, OrderStatus::_0));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_9, OrderStatus::_1));
}

TEST(OrderManagerTest, CompileTimeStateMachine_SameState) {
    // 相同状态不允许转换
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_0, OrderStatus::_0));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_1, OrderStatus::_1));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_2, OrderStatus::_2));
    EXPECT_FALSE(CheckEntrustStatus(OrderStatus::_3, OrderStatus::_3));
}

TEST(OrderManagerTest, CompileTimeStateMachine_ConstexprVerification) {
    // 验证编译期常量表达式
    // 这些检查在编译期完成，如果失败会导致编译错误
    static_assert(kForbiddenTransitions.size() == 128, "Forbidden transitions table size must be 128");

    // 验证状态掩码的编译期特性
    constexpr StatusMask test_mask {};
    static_assert(test_mask.low == 0 && test_mask.high == 0, "Default StatusMask should be zero");

    // 验证编译期 set 和 test 操作
    constexpr auto test_set = []() {
        StatusMask mask {};
        mask.set('A');
        return mask;
    }();
    static_assert(test_set.test('A'), "Compile-time set and test should work");
}
