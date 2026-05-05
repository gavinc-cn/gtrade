//
// Timer wheel logic tests — mirrors TimerManagerDummy's core scheduling behavior.
//

#include "gtest/gtest.h"
#include <vector>
#include <algorithm>
#include <cstdint>

namespace {

struct TestTimer {
    int timer_id;
    int64_t target_ns;
    int delay_ms;
    bool repeat;
    bool fired{false};
};

class TimerWheel {
public:
    struct FireEvent {
        int timer_id;
        int64_t fire_ns;
    };

    void AddTimer(int timer_id, int delay_ms, bool repeat) {
        TestTimer t{};
        t.timer_id = timer_id;
        t.delay_ms = delay_ms;
        t.target_ns = static_cast<int64_t>(delay_ms) * 1'000'000LL;
        t.repeat = repeat;
        t.fired = false;
        m_timers.push_back(t);
    }

    void KillTimer(int timer_id) {
        m_timers.erase(
            std::remove_if(m_timers.begin(), m_timers.end(),
                           [timer_id](const TestTimer& t) { return t.timer_id == timer_id; }),
            m_timers.end());
    }

    void ClearAll() { m_timers.clear(); }

    std::vector<FireEvent> AdvanceTo(int64_t ns) {
        std::vector<FireEvent> events;
        bool changed = true;
        while (changed) {
            changed = false;
            std::vector<size_t> fire_order;
            int64_t earliest = ns + 1;
            for (size_t i = 0; i < m_timers.size(); ++i) {
                if (m_timers[i].target_ns <= ns) {
                    if (m_timers[i].target_ns < earliest) {
                        earliest = m_timers[i].target_ns;
                        fire_order.clear();
                        fire_order.push_back(i);
                    } else if (m_timers[i].target_ns == earliest) {
                        fire_order.push_back(i);
                    }
                }
            }
            for (size_t idx : fire_order) {
                auto& t = m_timers[idx];
                events.push_back({t.timer_id, t.target_ns});
                t.fired = true;
                changed = true;
                if (t.repeat) {
                    t.target_ns += static_cast<int64_t>(t.delay_ms) * 1'000'000LL;
                    t.fired = false;
                }
            }
            if (!fire_order.empty()) {
                std::vector<size_t> to_remove;
                for (size_t idx : fire_order) {
                    if (!m_timers[idx].repeat) {
                        to_remove.push_back(idx);
                    }
                }
                for (auto it = to_remove.rbegin(); it != to_remove.rend(); ++it) {
                    m_timers.erase(m_timers.begin() + static_cast<std::ptrdiff_t>(*it));
                }
            }
        }
        return events;
    }

    size_t TimerCount() const { return m_timers.size(); }

private:
    std::vector<TestTimer> m_timers;
};

} // anonymous namespace

TEST(TimerWheelTest, SingleShotTimer_FiresOnce) {
    TimerWheel wheel;
    wheel.AddTimer(1, 1000, false);

    auto events = wheel.AdvanceTo(1'000'000'000LL);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].timer_id, 1);

    events = wheel.AdvanceTo(2'000'000'000LL);
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(wheel.TimerCount(), 0u);
}

TEST(TimerWheelTest, RepeatTimer_FiresMultiple) {
    TimerWheel wheel;
    wheel.AddTimer(1, 500, true);

    auto events = wheel.AdvanceTo(500'000'000LL);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].timer_id, 1);

    events = wheel.AdvanceTo(1'000'000'000LL);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].timer_id, 1);

    events = wheel.AdvanceTo(1'500'000'000LL);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].timer_id, 1);
}

TEST(TimerWheelTest, KillTimer_NoFire) {
    TimerWheel wheel;
    wheel.AddTimer(1, 1000, false);
    wheel.KillTimer(1);

    auto events = wheel.AdvanceTo(1'000'000'000LL);
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(wheel.TimerCount(), 0u);
}

TEST(TimerWheelTest, MultipleTimers_FireInOrder) {
    TimerWheel wheel;
    wheel.AddTimer(1, 2000, false);
    wheel.AddTimer(2, 1000, false);

    auto events = wheel.AdvanceTo(3'000'000'000LL);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].timer_id, 2);
    EXPECT_EQ(events[1].timer_id, 1);
}

TEST(TimerWheelTest, NoEarlyFire) {
    TimerWheel wheel;
    wheel.AddTimer(1, 1000, false);

    auto events = wheel.AdvanceTo(500'000'000LL);
    EXPECT_TRUE(events.empty());

    events = wheel.AdvanceTo(1'000'000'000LL);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].timer_id, 1);
}

TEST(TimerWheelTest, ClearAllRemovesAll) {
    TimerWheel wheel;
    wheel.AddTimer(1, 1000, false);
    wheel.AddTimer(2, 2000, false);
    wheel.AddTimer(3, 3000, false);
    wheel.ClearAll();

    auto events = wheel.AdvanceTo(5'000'000'000LL);
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(wheel.TimerCount(), 0u);
}

TEST(TimerWheelTest, RepeatTimerReschedulesAfterFire) {
    TimerWheel wheel;
    wheel.AddTimer(1, 1000, true);

    auto events = wheel.AdvanceTo(1'000'000'000LL);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].timer_id, 1);

    events = wheel.AdvanceTo(2'000'000'000LL);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].timer_id, 1);

    wheel.KillTimer(1);

    events = wheel.AdvanceTo(3'000'000'000LL);
    EXPECT_TRUE(events.empty());
}

TEST(TimerWheelTest, RepeatTimerFiresAllPendingAtOnce) {
    TimerWheel wheel;
    wheel.AddTimer(1, 100, true);

    auto events = wheel.AdvanceTo(500'000'000LL);
    ASSERT_EQ(events.size(), 5u);
    for (size_t i = 0; i < events.size(); ++i) {
        EXPECT_EQ(events[i].timer_id, 1);
        EXPECT_EQ(events[i].fire_ns, static_cast<int64_t>((i + 1) * 100'000'000LL));
    }
}

TEST(TimerWheelTest, InterleavedMultipleRepeatTimers) {
    TimerWheel wheel;
    wheel.AddTimer(1, 300, true);
    wheel.AddTimer(2, 200, true);

    auto events = wheel.AdvanceTo(600'000'000LL);
    std::vector<int> ids;
    for (const auto& e : events) {
        ids.push_back(e.timer_id);
    }

    auto count_id = [&ids](int id) {
        return static_cast<int>(std::count(ids.begin(), ids.end(), id));
    };
    EXPECT_EQ(count_id(1), 2);
    EXPECT_EQ(count_id(2), 3);
}

TEST(TimerWheelTest, KillOneTimerOthersUnaffected) {
    TimerWheel wheel;
    wheel.AddTimer(1, 500, false);
    wheel.AddTimer(2, 500, false);

    wheel.KillTimer(1);

    auto events = wheel.AdvanceTo(500'000'000LL);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].timer_id, 2);
}
