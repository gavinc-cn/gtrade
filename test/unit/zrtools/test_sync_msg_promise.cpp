//
// io_pool 同步消息单元测试（返回值版接口）
//
// 背景：PostSyncMsg 在调用方线程上创建 promise，把 (msg_type, buffer) 投递到目标线程执行
// handler，然后 future.get() 无限期阻塞。历史上 handler 直接持有 promise，任何一条返回路径
// 漏掉 set_value 都会让调用方线程永久挂死（现场没有日志，只表现为"某个线程不动了"）。
//
// 现行设计：SyncCallback 为返回值语义（handler 直接 return 响应），promise 兑现收归框架
// OnSyncMessage：正常路径由框架 set_value；handler 返回 nullptr 时兜底替换为非空空响应；
// handler 抛异常时记录后吞掉，由 EnsurePromiseFulfilled 兜底兑现空响应。本文件覆盖各路径。
//

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

#include "tbuffer.h"
#include "zrtools/io_pool_v2/boost_asio_thread.h"
#include "zrtools/io_pool_v2/i_handler.h"
#include "zrtools/io_pool_v2/io_service.h"
#include "zrtools/io_pool_v2/sync_thread.h"

namespace {
    // 与 interface/type_define.h 中的 MyHandler 等价（此处不引 type_define.h，避免依赖生成的消息 ID 头）
    using BufPtr = TBufferPtr;
    using Callback = std::function<void(int, BufPtr)>;
    using SyncCallback = std::function<BufPtr(int, BufPtr)>;
    using TestHandler = IHandler<BufPtr, Callback, SyncCallback>;

    constexpr int kMsgFulfill = 9001;       // handler 正常返回响应
    constexpr int kMsgNull = 9002;          // handler 返回 nullptr
    constexpr int kMsgThrow = 9003;         // handler 抛异常
    constexpr int kMsgUnregistered = 9004;  // 未注册任何 handler 的消息

    struct TestPayload {
        int value {};
    };

    BufPtr MakePayloadRsp(const int value) {
        auto rsp = std::make_shared<TBuffer>();
        rsp->Append(TestPayload {value});
        return rsp;
    }

    // 测试用服务：IHandler 的 Init/Start 是纯虚接口，这里只需返回 true
    class SyncTestService: public TestHandler {
    public:
        bool Init() override { return true; }
        bool Start() override { return true; }

        void InstallTestHandlers() {
            InstallSyncHandler(kMsgFulfill, [](int, const BufPtr) {
                return MakePayloadRsp(12345);
            });
            InstallSyncHandler(kMsgNull, [](int, const BufPtr) -> BufPtr {
                // 返回 nullptr：模拟 handler 异常路径返回空指针
                return nullptr;
            });
            InstallSyncHandler(kMsgThrow, [](int, const BufPtr) -> BufPtr {
                throw std::runtime_error("handler exception for test");
            });
        }
    };

    // 有界等待：把同步调用放到独立线程执行并限时等待。
    // 兜底失效（回归）时调用方会永久阻塞，这里把"挂死"转成用例失败，而不是让整个测试进程挂起；
    // 超时后只能分离线程（它已经卡死，join 会再次挂住用例）。
    template <typename Fn>
    bool RunWithTimeout(Fn&& fn, const std::chrono::milliseconds timeout) {
        std::atomic<bool> done {false};
        std::thread worker([&fn, &done] {
            fn();
            done = true;
        });

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!done && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!done) {
            worker.detach();
            return false;
        }
        worker.join();
        return true;
    }
}

// 跨线程投递路径（BoostAsioThread：真实工作线程 + io_service 队列）
class SyncMsgPromiseAsioTest: public ::testing::Test {
protected:
    void SetUp() override {
        m_service.SetThread(&m_thread);
        ASSERT_TRUE(m_thread.ThreadStart());
        m_service.InstallTestHandlers();
    }

    void TearDown() override {
        m_thread.ThreadStop();
    }

    SyncTestService m_service {};
    BoostAsioThread m_thread {};
};

// 正常返回：handler 的返回值必须原样送达调用方
TEST_F(SyncMsgPromiseAsioTest, NormalHandlerResponseIsDelivered) {
    BufPtr rsp {};
    const bool finished = RunWithTimeout(
        [&] { m_service.PostSyncMsg(kMsgFulfill, std::make_shared<TBuffer>(), rsp); }, std::chrono::seconds(5));

    ASSERT_TRUE(finished) << "正常 handler 不应阻塞调用方";
    ASSERT_TRUE(rsp);
    ASSERT_EQ(rsp->GetSize(), sizeof(TestPayload));
    EXPECT_EQ(rsp->Data<TestPayload>().value, 12345);
}

// handler 返回 nullptr：框架兜底替换为非空空响应，调用方不挂死、不拿到空指针
TEST_F(SyncMsgPromiseAsioTest, NullReturningHandlerDoesNotHangCallerOrLeakNullptr) {
    BufPtr rsp {};
    const bool finished = RunWithTimeout(
        [&] { m_service.PostSyncMsg(kMsgNull, std::make_shared<TBuffer>(), rsp); }, std::chrono::seconds(5));

    ASSERT_TRUE(finished) << "handler 返回 nullptr 时调用方不应拿到空指针或挂死";
    ASSERT_TRUE(rsp) << "兜底响应不应为 nullptr（调用方会直接解引用）";
    EXPECT_EQ(rsp->GetSize(), 0U);
}

// handler 抛异常：调用方拿到兜底空响应，且异常不得逃出 io_context 杀死处理线程
TEST_F(SyncMsgPromiseAsioTest, ThrowingHandlerDoesNotHangCallerAndThreadSurvives) {
    BufPtr rsp {};
    const bool finished = RunWithTimeout(
        [&] { m_service.PostSyncMsg(kMsgThrow, std::make_shared<TBuffer>(), rsp); }, std::chrono::seconds(5));

    ASSERT_TRUE(finished) << "handler 抛异常时调用方永久阻塞（兜底失效）";
    ASSERT_TRUE(rsp);

    // 后续正常消息仍能拿到响应，说明处理线程没有被异常终止
    BufPtr rsp_after {};
    const bool finished_after = RunWithTimeout(
        [&] { m_service.PostSyncMsg(kMsgFulfill, std::make_shared<TBuffer>(), rsp_after); }, std::chrono::seconds(5));
    ASSERT_TRUE(finished_after) << "handler 抛出的异常逃出 io_context，处理线程已死";
    ASSERT_TRUE(rsp_after);
    EXPECT_EQ(rsp_after->Data<TestPayload>().value, 12345);
}

// 消息未注册 handler 且服务未装默认同步 handler：也要兜底兑现非空空响应
TEST_F(SyncMsgPromiseAsioTest, UnregisteredMsgDoesNotHangCaller) {
    BufPtr rsp {};
    const bool finished = RunWithTimeout(
        [&] { m_service.PostSyncMsg(kMsgUnregistered, std::make_shared<TBuffer>(), rsp); }, std::chrono::seconds(5));

    ASSERT_TRUE(finished) << "消息未注册 handler 时调用方永久阻塞（兜底失效）";
    EXPECT_TRUE(rsp);
}

// 同线程直执路径（SyncThread::IsInThread() 恒为 true）走的是 PostSyncMsg 的另一条分支
TEST(SyncMsgPromiseSyncThreadTest, NullReturningHandlerDoesNotHangInSameThreadPath) {
    SyncTestService service {};
    SyncThread thread {};
    service.SetThread(&thread);
    service.InstallTestHandlers();

    BufPtr rsp {};
    const bool finished = RunWithTimeout(
        [&] { service.PostSyncMsg(kMsgNull, std::make_shared<TBuffer>(), rsp); }, std::chrono::seconds(5));

    ASSERT_TRUE(finished) << "同线程直执路径下 handler 返回 nullptr 导致挂死或空指针";
    EXPECT_TRUE(rsp);
}

// IOService 路径（QueryProcessor 用 BoostAsioSrv/SyncIoSrv，即 IOService）
class SyncTestIoService: public IOService<BufPtr, Callback, SyncCallback, SyncThread> {
public:
    bool Init() override { return true; }
};

TEST(SyncMsgPromiseIoServiceTest, NullReturningHandlerDoesNotHangCaller) {
    SyncTestIoService service {};
    service.InstallSyncHandler()(kMsgNull, [](int, const BufPtr) -> BufPtr {
        return nullptr;
    });

    BufPtr rsp {};
    const bool finished = RunWithTimeout(
        [&] { service.PostSyncMsg(kMsgNull, std::make_shared<TBuffer>(), rsp); }, std::chrono::seconds(5));

    ASSERT_TRUE(finished) << "IOService 路径下 handler 返回 nullptr 导致挂死";
    EXPECT_TRUE(rsp);
}

// IOService 链式注册路径（QueryProcessor 形态）同样按返回值语义送达
TEST(SyncMsgPromiseIoServiceTest, NormalHandlerResponseIsDelivered) {
    SyncTestIoService service {};
    service.InstallSyncHandler()(kMsgFulfill, [](int, const BufPtr) {
        return MakePayloadRsp(12345);
    });

    BufPtr rsp {};
    const bool finished = RunWithTimeout(
        [&] { service.PostSyncMsg(kMsgFulfill, std::make_shared<TBuffer>(), rsp); }, std::chrono::seconds(5));

    ASSERT_TRUE(finished) << "IOService 路径下返回值版 handler 不应阻塞调用方";
    ASSERT_TRUE(rsp);
    EXPECT_EQ(rsp->Data<TestPayload>().value, 12345);
}
