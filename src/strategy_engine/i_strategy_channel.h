//
// 策略通信通道抽象接口
//
// IStrategyChannel 封装 Engine 与 Runner 之间的单条通信链路。
// 通过此抽象，RemoteProxy 和 StrategyEngine 与具体通信实现（ZMQ / gRPC / 其他）解耦。
//
// 当前实现：StrategyZmqChannel（ZMQ ROUTER/DEALER）
// 未来扩展：只需新建实现类，无需修改 RemoteProxy 或 StrategyEngine。
//

#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

// ── 跨网络传输结构体（#pragma pack 确保跨平台内存布局一致）──────────────────

/// 握手帧 payload
/// Runner 连接时发送，Engine 用此填充 StrategyInfo（无需持有策略 YAML）
#pragma pack(push, 1)
struct RemoteHandshakePayload {
    char strat_id[64];           ///< 策略实例 ID（如 "strat_demo_BTC"），NUL 结尾
    char strat_template_id[64];  ///< 策略模板 ID（如 "StratDemo"），NUL 结尾
    char account_id[64];         ///< 账户 ID，NUL 结尾
    char instrument[64];         ///< 交易品种（如 "BTC-USDT-SWAP"），NUL 结尾
};
#pragma pack(pop)

/// kRemoteSyncResp payload 中的单条订单记录
#pragma pack(push, 1)
struct RemoteSyncRespItem {
    char    client_order_id[32]; ///< 客户端订单 ID（NUL 结尾字符串）
    char    ex_order_id[32];     ///< 交易所订单 ID（未确认时为全 '\0'）
    int32_t status;              ///< OrderStatus 枚举值
    double  filled_qty;          ///< 已成交数量
    double  avg_price;           ///< 平均成交价
    double  remain_qty;          ///< 剩余数量
};
#pragma pack(pop)

/// kRemoteSyncResp payload 头部
/// 完整帧 = sizeof(RemoteSyncResp) + item_count * sizeof(RemoteSyncRespItem)
/// SyncResp 返回 Engine 侧该策略的全量活跃订单（不过滤 SyncReq 的 pending_ids），
/// 使 strategy->OnReconnected() 可以发现孤儿订单（已发单但未写入 checkpoint）。
#pragma pack(push, 1)
struct RemoteSyncResp {
    uint32_t          item_count; ///< 订单数量
    RemoteSyncRespItem items[0];  ///< GCC/Clang 零长数组扩展；sizeof(RemoteSyncResp) == 4
};
#pragma pack(pop)

// ── 通道接口 ─────────────────────────────────────────────────────────────────

/**
 * 策略通信通道抽象接口
 *
 * Engine 侧（StrategyZmqChannel Engine 构造函数）：
 *   - IsConnected() 固定返回 true（ROUTER socket 无连接状态，存活检测依赖心跳）
 *   - DispatchIncoming() 由 ZmqAcceptor 线程调用，心跳直接消化，其余帧由调用方路由
 *   - WriteFrame() 通过 ROUTER socket 向对应 peer 发送帧（dontwait，HWM 满时丢弃）
 *   - ReadFrame() 不使用（Engine 侧由 AcceptLoop 统一接收）
 *
 * Runner 侧（StrategyZmqChannel Runner 构造函数）：
 *   - IsConnected() 基于 socket monitor 事件
 *   - ReadFrame() 非阻塞读，有帧则回调
 *   - PollNeedHandshake() 检测是否需要发握手+SyncReq（每次重连触发一次）
 */
class IStrategyChannel {
public:
    virtual ~IStrategyChannel() = default;

    /// 帧回调类型：msg_type / 数据指针 / 数据长度
    using FrameCallback = std::function<void(uint32_t msg_type,
                                             const void* data,
                                             uint32_t len)>;

    /// 通道是否已连接（Engine 侧固定 true，Runner 侧基于 monitor 事件）
    virtual bool IsConnected() const = 0;

    /// 关闭通道（stop + join 内部线程）
    virtual void Close() = 0;

    /// 发送带 payload 的帧
    /// @return false 表示发送失败（HWM 满或 socket 已关闭），调用方可递增 drop_count
    virtual bool WriteFrame(uint32_t msg_type, const void* data, uint32_t len) = 0;

    /// 发送无 payload 的控制帧（心跳、Start/Stop/Pause/Resume）
    virtual bool WriteFrame(uint32_t msg_type) = 0;

    /// 非阻塞读：有帧则回调一次并返回 true，无帧返回 false（Runner 侧主循环使用）
    virtual bool ReadFrame(const FrameCallback& cb) = 0;

    /// 最后一次收到心跳时 Engine 本地 steady_clock 的微秒时间戳（0 表示尚未收到）
    /// 由 DispatchIncoming() 在 AcceptLoop 线程写入，由 RemoteProxy::IsAlive() 读取。
    virtual int64_t GetLastHeartbeatUs() const = 0;

    /// Runner 侧专用：检测是否需要（重新）发送握手 + SyncReq
    /// 由 zmq_socket_monitor ZMQ_EVENT_CONNECTED / RECONNECTED 事件驱动置位，
    /// 调用后原子清零（每次重连只触发一次），主循环用此方法轮询。
    virtual bool PollNeedHandshake() = 0;
};
