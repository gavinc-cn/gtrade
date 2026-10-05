//
// 同步消息 promise 兜底工具
//
// 背景：PostSyncMsg 在调用方线程上创建 promise，把 (msg_type, buffer, promise) 投递到目标
// 线程执行 handler，然后 future.get() 无限期阻塞。只要 handler 有任何一条返回路径没有兑现
// promise（新增提前 return 忘了 set_value、抛出的异常跳过尾部 set_value、消息没有注册 handler
// 被直接丢弃），调用方线程就永久挂死，而且没有任何日志，现场只表现为"某个线程不动了"。
//
// 该不变式（"每条路径都必须兑现 promise"）原本分散在二十多个 handler 函数体里靠人肉维护，
// 已在多处失守。这里把兜底动作收敛到框架侧一处：handler 返回后若仍未兑现，自动兑现一个空
// 响应并打印 ERROR 日志 —— 调用方永远拿得到结果，漏兑现者也主动暴露在日志里。
//

#pragma once

#include <chrono>
#include <future>
#include <memory>
#include <type_traits>
#include "spdlog/spdlog.h"

namespace zrt {
    // 判断 Buffer 是否具备 element_type（即 shared_ptr 族），用于构造"非空但内容为空"的响应
    template <typename Buffer, typename = void>
    struct HasElementType: std::false_type {
    };

    template <typename Buffer>
    struct HasElementType<Buffer, std::void_t<typename Buffer::element_type>>: std::true_type {
    };

    // 空响应：shared_ptr 型 Buffer 返回非空对象 —— 部分调用方（http_server / desktop_gateway）
    // 拿到响应后无条件解引用，返回 nullptr 会直接崩溃
    template <typename Buffer>
    Buffer MakeEmptyResponse() {
        if constexpr (HasElementType<Buffer>::value) {
            return std::make_shared<typename Buffer::element_type>();
        } else {
            return Buffer {};
        }
    }

    // handler 返回后仍未兑现 promise 时兜底兑现，避免调用方 future.get() 永久阻塞
    // 约束：同步 handler 必须在返回前兑现 promise。若 handler 打算返回后再异步 set_value，
    //       这里会先兜底兑现，那次异步 set_value 将抛 std::future_error（当前项目无此类 handler）
    template <typename Buffer>
    void EnsurePromiseFulfilled(const int msg_type, std::promise<Buffer>& promise_obj,
                                const std::shared_future<Buffer>& future_obj) {
        if (future_obj.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            return;
        }

        SPDLOG_ERROR("sync handler for msg_type({}) returned without fulfilling the promise, "
                     "auto-fulfill empty response", msg_type);
        try {
            promise_obj.set_value(MakeEmptyResponse<Buffer>());
        } catch (const std::exception& e) {
            // 兜底兑现失败也必须吞掉异常：异常从投递的 lambda 逃出会终止目标线程
            SPDLOG_ERROR("auto-fulfill promise for msg_type({}) failed: {}", msg_type, e.what());
        }
    }
}
