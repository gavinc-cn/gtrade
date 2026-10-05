// 测试真实 TimerManagerDummy（src/strategy_engine/timer_manager_dummy.h）
// 迁移自旧复制品测试（原 test/unit/timer_manager_dummy.cpp 在测试内重实现了 TimerWheel，已删除）。
// 驱动方式：测试子类 TestableTimerManager 用 using 开放 protected 处理器做同步驱动；
//          时钟经公有 UpdateTime(ms) 推进；
//          触发事件经注册进 ServiceMap 的捕获型 FakeStrategyEngine 异步接收（cv 等待）。
// 锁定的是真实行为，含与旧复制品的已知分歧点（见各用例注释）。
#include <gtest/gtest.h>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>
#include "timer_manager_dummy.h"
#include "service_map.h"
#include "string_keys.h"

namespace {

// 测试用 setter_id（对应生产中的策略 id）
constexpr char kTestSetterId[] = "ut_timer_mgr";

// 捕获型假引擎：接收 TimerManagerDummy 发来的 kTimerEvent 并记录。
// 线程模式照搬生产入口（gtrade.cpp / strategy_runner/main.cpp）：
// EnginePool 添加 BoostAsioThread 共享线程 + EngineStart。
class FakeStrategyEngine final : public MyHandler {
public:
    FakeStrategyEngine() {
        SetThread(zrt::EnginePool::GetInstance().GetSharedThread());
        // 只关心 kTimerEvent，直接注册处理器
        // （MyHandler 的未注册消息回落是 InstallDefaultHandler 回调，
        //   与 ITimerManager 体系里的 OnDefaultMsg 虚函数无关）
        InstallHandler(MsgId::kTimerEvent, [this](const int msg_id, const BufPtr buffer) {
            const auto& push = buffer->RefData<TimerEventPush>();
            {
                std::lock_guard<std::mutex> lk(m_mu);
                m_events.push_back(push);
            }
            m_cv.notify_all();
        });
    }

    bool Init() override { return true; }
    bool Start() override { return true; }

    // 清空已捕获事件（ServiceMap 是进程级单例，假引擎跨用例复用，每个用例开始前复位）
    void Reset() {
        std::lock_guard<std::mutex> lk(m_mu);
        m_events.clear();
    }

    std::vector<TimerEventPush> Events() {
        std::lock_guard<std::mutex> lk(m_mu);
        return m_events;
    }

    // 等待事件数达到 n，超时返回 false（防止异步派发卡死测试）
    bool WaitEvents(const size_t n, const int timeout_ms = 2000) {
        std::unique_lock<std::mutex> lk(m_mu);
        return m_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                             [&] { return m_events.size() >= n; });
    }

private:
    std::mutex m_mu {};
    std::condition_variable m_cv {};
    std::vector<TimerEventPush> m_events {};
};

// 测试子类：using 开放 protected 处理器做同步驱动（生产改动仅 private→protected 一词）
class TestableTimerManager final : public TimerManagerDummy {
public:
    using TimerManagerDummy::TimerManagerDummy;
    using TimerManagerDummy::OnSetTimer;
    using TimerManagerDummy::OnKillTimer;
    using TimerManagerDummy::OnClearAllTimer;
};

class TimerManagerDummyTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // EnginePool 是进程级单例：TimerManagerDummy/FakeStrategyEngine 构造时
        // 经 GetSharedThread() 取线程，且 PostMsg 依赖线程已启动消费队列
        auto& pool = zrt::EnginePool::GetInstance();
        if (pool.GetSharedThread() == nullptr) {
            pool.AddSharedEngine<BoostAsioThread>(2);
        }
        if (!pool.IsStarted()) {
            pool.EngineStart();
        }
        // ServiceMap 也是进程级单例：注册捕获型假引擎；
        // emplace 失败（已被注册）则复用已有实例，但须同为 FakeStrategyEngine
        auto& sm = ServiceMap::GetInstance();
        auto fake = std::make_unique<FakeStrategyEngine>();
        auto* const fake_ptr = fake.get();
        if (sm.emplace(k_StrategyEngine, std::move(fake)).second) {
            s_fake = fake_ptr;
        } else {
            s_fake = dynamic_cast<FakeStrategyEngine*>(sm.at(k_StrategyEngine).get());
        }
        ASSERT_NE(s_fake, nullptr) << "k_StrategyEngine 已被其他套件注册为不同类型";
    }

    void SetUp() override {
        s_fake->Reset();
        m_mgr = std::make_unique<TestableTimerManager>(ServiceMap::GetInstance(), GTradeConfig {});
        ASSERT_TRUE(m_mgr->Init());  // 绑定 m_strategy_engine + 注册消息处理器
    }

    // 以下打包方式照抄生产调用点（StrategyBase::SetTimer/KillTimer/ClearAllTimer，
    // strategy_base.cpp:174-197），区别是同步直驱 protected 处理器而非 PostMsg。
    // service_name 必须填 k_StrategyEngine，否则 OnTimerEventPush 只记错误日志不投递
    // （timer_manager_dummy.cpp:55-60）
    void SetTimer(const int timer_id, const int delay_ms, const bool repeat) {
        SetTimerReq req {};
        zrt::fill_field(req.service_name, k_StrategyEngine);
        zrt::fill_field(req.setter_id, kTestSetterId);
        zrt::fill_field(req.timer_id, timer_id);
        zrt::fill_field(req.delay_ms, delay_ms);
        zrt::fill_field(req.repeat, repeat);
        m_mgr->OnSetTimer(MsgId::kSetTimer, std::make_shared<TBuffer>(req));
    }

    void KillTimer(const int timer_id) {
        TimerKey req {};
        zrt::fill_field(req.service_name, k_StrategyEngine);
        zrt::fill_field(req.setter_id, kTestSetterId);
        zrt::fill_field(req.timer_id, timer_id);
        m_mgr->OnKillTimer(MsgId::kKillTimer, std::make_shared<TBuffer>(req));
    }

    void ClearAllTimer() {
        TimerKey req {};
        zrt::fill_field(req.service_name, k_StrategyEngine);
        zrt::fill_field(req.setter_id, kTestSetterId);
        m_mgr->OnClearAllTimer(MsgId::kClearAllTimer, std::make_shared<TBuffer>(req));
    }

    static inline FakeStrategyEngine* s_fake {};
    std::unique_ptr<TestableTimerManager> m_mgr {};
};

// 未到 target 不触发
TEST_F(TimerManagerDummyTest, SetTimer_NoFireBeforeTarget) {
    SetTimer(1, 1000, false);
    m_mgr->UpdateTime(999);
    EXPECT_FALSE(s_fake->WaitEvents(1, 200)) << "目标时刻前不应有事件";
    EXPECT_TRUE(s_fake->Events().empty());
}

// 到达 target 触发；push.target_ms 取触发时的 m_now_ms（timer_manager_dummy.cpp:53）
TEST_F(TimerManagerDummyTest, SetTimer_FiresAtTarget) {
    SetTimer(1, 1000, false);
    m_mgr->UpdateTime(1000);
    ASSERT_TRUE(s_fake->WaitEvents(1));
    const auto events = s_fake->Events();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].timer_id, 1);
    EXPECT_EQ(events[0].target_ms, 1000);
    EXPECT_STREQ(events[0].service_name, k_StrategyEngine);
    EXPECT_STREQ(events[0].setter_id, kTestSetterId);
}

// KillTimer 后不再触发
TEST_F(TimerManagerDummyTest, KillTimer_NoFire) {
    SetTimer(1, 1000, false);
    KillTimer(1);
    m_mgr->UpdateTime(2000);
    EXPECT_FALSE(s_fake->WaitEvents(1, 200));
    EXPECT_TRUE(s_fake->Events().empty());
}

// ClearAllTimer 清空 (service_name, setter_id) 下所有定时器
TEST_F(TimerManagerDummyTest, ClearAllRemovesAll) {
    SetTimer(1, 1000, false);
    SetTimer(2, 500, false);
    ClearAllTimer();
    m_mgr->UpdateTime(2000);
    EXPECT_FALSE(s_fake->WaitEvents(1, 200));
    EXPECT_TRUE(s_fake->Events().empty());
}

// 跨推进按到期先后触发。
// 注意：受分歧点 3（一次性定时器触发后不移除）影响，第二趟 UpdateTime(2000) 会连带重触发 id=2；
// 单趟内触发顺序为 BMIC 主键序（分歧点 4），故第二趟 id=1 先于 id=2。
TEST_F(TimerManagerDummyTest, MultipleTimers_FireByDeadlineAcrossUpdates) {
    SetTimer(1, 2000, false);
    SetTimer(2, 1000, false);
    m_mgr->UpdateTime(1000);
    ASSERT_TRUE(s_fake->WaitEvents(1));
    {
        const auto events = s_fake->Events();
        ASSERT_EQ(events.size(), 1u);
        EXPECT_EQ(events[0].timer_id, 2);    // 仅 id=2 到期
        EXPECT_EQ(events[0].target_ms, 1000);
    }
    m_mgr->UpdateTime(2000);
    ASSERT_TRUE(s_fake->WaitEvents(3));
    const auto events = s_fake->Events();
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[1].timer_id, 1);        // id=1 到期触发
    EXPECT_EQ(events[1].target_ms, 2000);
    EXPECT_EQ(events[2].timer_id, 2);        // 分歧点 3：id=2 未移除，被再次触发
    EXPECT_EQ(events[2].target_ms, 2000);
}

// 同一趟 UpdateTime 内所有到期定时器都触发。
// 真实行为分歧点 4：单趟内触发顺序为 BMIC 主键 (service_name, setter_id, timer_id) 序，
// 而非到期先后——id=1（target=1000）先于 id=2（target=500）触发。
TEST_F(TimerManagerDummyTest, SameUpdatePass_FiresAllDue) {
    SetTimer(1, 1000, false);
    SetTimer(2, 500, false);
    m_mgr->UpdateTime(1500);
    ASSERT_TRUE(s_fake->WaitEvents(2));
    const auto events = s_fake->Events();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].timer_id, 1);
    EXPECT_EQ(events[1].timer_id, 2);
}

// 重复定时器。
// 真实行为核对（timer_manager_dummy.cpp:67-77 ResetTimer）：携带新 target 的 tmp 是死代码，
// 实际 Update 进容器的是旧 timer_info——即重复定时器触发后并不重排（疑似缺陷，已记录到任务汇报）。
// 净效果：首次到期后，每趟 UpdateTime 恰好再触发一次（分歧点 1"每趟最多一次"成立）。
TEST_F(TimerManagerDummyTest, RepeatTimer_OneFirePerUpdate_NoReschedule) {
    SetTimer(1, 100, true);
    m_mgr->UpdateTime(100);
    ASSERT_TRUE(s_fake->WaitEvents(1));
    m_mgr->UpdateTime(250);
    ASSERT_TRUE(s_fake->WaitEvents(2));
    m_mgr->UpdateTime(349);
    ASSERT_TRUE(s_fake->WaitEvents(3));
    m_mgr->UpdateTime(350);
    ASSERT_TRUE(s_fake->WaitEvents(4));
    const auto events = s_fake->Events();
    ASSERT_EQ(events.size(), 4u);
    // 每趟恰触发一次，push.target_ms 恒等于该趟的 now
    EXPECT_EQ(events[0].target_ms, 100);
    EXPECT_EQ(events[1].target_ms, 250);
    EXPECT_EQ(events[2].target_ms, 349);
    EXPECT_EQ(events[3].target_ms, 350);
    for (const auto& e : events) {
        EXPECT_EQ(e.timer_id, 1);
    }
}

// 疑似缺陷锁定（分歧点 3）：一次性定时器触发后不从容器移除，下趟 UpdateTime 会再次触发。
// 若后续修复为"触发即移除"，本用例应改为共 1 事件。
TEST_F(TimerManagerDummyTest, OneShotTimer_NotRemovedAfterFire) {
    SetTimer(1, 100, false);
    m_mgr->UpdateTime(100);
    ASSERT_TRUE(s_fake->WaitEvents(1));
    m_mgr->UpdateTime(200);
    ASSERT_TRUE(s_fake->WaitEvents(2));    // 再次触发（疑似缺陷锁定）
    const auto events = s_fake->Events();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].target_ms, 100);
    EXPECT_EQ(events[1].target_ms, 200);
}

} // namespace
