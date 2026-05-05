//
// 远程策略代理
//
// RemoteProxy 是 IStrategyProxy 的跨机器实现。
// 策略运行在独立的策略服务器（gtrade_strategy_runner --mode remote）上，
// Engine 与 Runner 通过 ZMQ ROUTER/DEALER 通信。
//
// 与 InProcessProxy / OutProcessProxy 的关键差异：
//   - Proxy 在 Runner 连接到达时动态创建（Connection-create 模型），Engine 不预加载配置
//   - StrategyInfo 来自握手 payload（RemoteHandshakePayload），无需持有策略 YAML
//   - IsAlive() 依赖 Engine 本地 steady_clock 心跳超时，无 NTP 依赖
//   - 断线重连后通过 kRemoteSyncResp 全量对账（State Reconciliation），而非 Replay
//
// 生命周期：
//   StrategyEngine::OnRemoteStrategyConnected（引擎线程）
//     → 创建 RemoteProxy → 插入 m_strategy_proxy_map
//   断线重连：OnReconnected(new_channel)，幂等，可多次调用
//

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include "i_strategy_proxy.h"
#include "i_strategy_channel.h"
#include "msg_id.h"
#include "type_define.h"
#include "utilities/tbuffer.h"
#include "spdlog/spdlog.h"

/// RemoteProxy 配置参数
struct RemoteProxyConfig {
    /// 心跳超时阈值（ms）：超过此时间未收到 Runner 心跳则 IsAlive() 返回 false
    int heartbeat_timeout_ms{5000};
};

/**
 * 远程策略代理
 *
 * 持有：
 *   - m_strat_info：来自握手 payload，无需 YAML
 *   - m_channel：当前活跃的 IStrategyChannel（断线重连时替换）
 *   - m_channel_mutex：保护 m_channel 替换（OnReconnected 写，IsAlive/PostData 读）
 */
class RemoteProxy final : public IStrategyProxy {
public:
    /**
     * 构造 RemoteProxy（由 StrategyEngine::OnRemoteStrategyConnected 在引擎线程调用）。
     *
     * @param hs           握手 payload（Runner 连接时发送的 RemoteHandshakePayload）
     * @param strat_engine StrategyEngine 的 MyHandler 指针（用于处理 Channel B 消息）
     * @param channel      已建立连接的 IStrategyChannel
     * @param config       可选配置参数
     */
    RemoteProxy(const RemoteHandshakePayload& hs,
                MyHandler* strat_engine,
                std::shared_ptr<IStrategyChannel> channel,
                RemoteProxyConfig config = {}):
    m_config(config),
    m_strat_engine(strat_engine),
    m_channel(std::move(channel))
    {
        // 从握手 payload 填充 StrategyInfo（无需 YAML，Engine 不持有 Runner 的配置文件）
        zrt::fill_field(m_strat_info.id,             hs.strat_id);
        zrt::fill_field(m_strat_info.strat_name,     hs.strat_id);
        zrt::fill_field(m_strat_info.strat_template, hs.strat_template_id);
        zrt::fill_field(m_strat_info.status,         StrategyEnvStatus::Stopped);
        zrt::fill_field(m_strat_info.indicator,      "{}");
        zrt::fill_field(m_strat_info.param,          "{}");

        // 保存 account_id / instrument（用于日志和调试）
        m_account_id = hs.account_id;
        m_instrument = hs.instrument;
        m_strat_id   = hs.strat_id;
    }

    ~RemoteProxy() override = default;

    RemoteProxy(const RemoteProxy&) = delete;
    RemoteProxy& operator=(const RemoteProxy&) = delete;

    // ── IStrategyProxy 实现 ─────────────────────────────────────────────────

    /**
     * Remote 模式下 Engine 不持有策略 YAML，Init() 为空实现（直接返回 true）。
     * 策略初始化在 Runner 端（gtrade_strategy_runner --mode remote）完成。
     */
    bool Init(const YAML::Node& /*strat_yml*/) override { return true; }

    /**
     * 启动策略：通过 Channel C 发送 kSubprocStratStart 命令给 Runner。
     */
    bool Start() override {
        std::lock_guard<std::mutex> lock(m_channel_mutex);
        if (!m_channel) return false;
        const bool ok = m_channel->WriteFrame(
            static_cast<uint32_t>(MsgId::kSubprocStratStart));
        if (ok) {
            zrt::fill_field(m_strat_info.status, StrategyEnvStatus::Running);
            SPDLOG_INFO("RemoteProxy::Start: strat={} Start sent", m_strat_id);
        }
        return ok;
    }

    /**
     * 停止策略：通过 Channel C 发送 kSubprocStratStop 命令给 Runner。
     */
    void Stop() override {
        std::lock_guard<std::mutex> lock(m_channel_mutex);
        if (!m_channel) return;
        m_channel->WriteFrame(static_cast<uint32_t>(MsgId::kSubprocStratStop));
        zrt::fill_field(m_strat_info.status, StrategyEnvStatus::Stopped);
        SPDLOG_INFO("RemoteProxy::Stop: strat={} Stop sent", m_strat_id);
    }

    /**
     * 暂停策略：通过 Channel C 发送 kSubprocStratPause 命令。
     */
    bool Pause() override {
        std::lock_guard<std::mutex> lock(m_channel_mutex);
        if (!m_channel) return false;
        const bool ok = m_channel->WriteFrame(
            static_cast<uint32_t>(MsgId::kSubprocStratPause));
        if (ok) {
            zrt::fill_field(m_strat_info.status, StrategyEnvStatus::Paused);
        }
        return ok;
    }

    /**
     * 恢复策略：通过 Channel C 发送 kSubprocStratResume 命令。
     */
    bool Resume() override {
        std::lock_guard<std::mutex> lock(m_channel_mutex);
        if (!m_channel) return false;
        const bool ok = m_channel->WriteFrame(
            static_cast<uint32_t>(MsgId::kSubprocStratResume));
        if (ok) {
            zrt::fill_field(m_strat_info.status, StrategyEnvStatus::Running);
        }
        return ok;
    }

    /**
     * 向 Runner 投递数据消息（Channel A：行情、委托回报等）。
     *
     * HWM 满时 WriteFrame 返回 false，递增 m_drop_count 并打印告警（每 1000 次一次）。
     */
    void PostData(const int msg_type, const BufPtr& buffer) override {
        std::lock_guard<std::mutex> lock(m_channel_mutex);
        if (!m_channel || m_is_deleting.load(std::memory_order_acquire)) return;

        const void*    data = buffer ? buffer->Data()    : nullptr;
        const uint32_t len  = buffer ? static_cast<uint32_t>(buffer->GetSize()) : 0u;

        if (!m_channel->WriteFrame(static_cast<uint32_t>(msg_type), data, len)) {
            const uint64_t drop = ++m_drop_count;
            if (drop % 1000 == 1) {
                SPDLOG_WARN("RemoteProxy: strat={} Channel A drop_count={} (HWM or channel closed)",
                            m_strat_id, drop);
            }
        }
    }

    const std::string& GetStratId() const override { return m_strat_id; }

    /**
     * 返回策略元信息（来自握手 payload，不依赖 YAML）。
     */
    const StrategyInfo& GetStrategyInfo() const override { return m_strat_info; }

    /**
     * 检查 Runner 是否存活（基于 Engine 本地 steady_clock 心跳超时）。
     *
     * Engine 侧 StrategyZmqChannel::IsConnected() 固定返回 true（ROUTER 无连接状态），
     * 因此存活检测完全依赖心跳超时。
     *
     * 使用 Engine 本地 steady_clock 时间戳（DispatchIncoming 中写入），
     * 不依赖 Runner 发送的时间，无 NTP 偏差风险。
     */
    bool IsAlive() const override {
        std::lock_guard<std::mutex> lock(m_channel_mutex);
        if (!m_channel) return false;

        const int64_t last_hb = m_channel->GetLastHeartbeatUs();
        if (last_hb == 0) {
            // 尚未收到首个心跳（Runner 刚连接），视为存活
            return true;
        }

        // 比较 Engine 本地 steady_clock 时间差
        const int64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        return (now_us - last_hb) < static_cast<int64_t>(m_config.heartbeat_timeout_ms) * 1000;
    }

    /**
     * 标记为删除状态，阻止后续 PostData 向 Runner 发送消息。
     */
    void MarkAsDeleting() override {
        m_is_deleting.store(true, std::memory_order_release);
        // 通知 Runner 停止（尽力而为，忽略发送结果）
        std::lock_guard<std::mutex> lock(m_channel_mutex);
        if (m_channel) {
            m_channel->WriteFrame(static_cast<uint32_t>(MsgId::kSubprocStratStop));
        }
    }

    // ── 远程模式专用方法 ─────────────────────────────────────────────────────

    /**
     * 断线重连：替换 channel（由 StrategyEngine::OnRemoteStrategyConnected 在引擎线程调用）。
     *
     * 幂等设计：多次调用只是替换 channel，以最后一次 kRemoteSyncResp 为准。
     * 替换后旧 channel 析构（RAII）。
     */
    void OnReconnected(std::shared_ptr<IStrategyChannel> new_channel) {
        std::lock_guard<std::mutex> lock(m_channel_mutex);
        m_channel = std::move(new_channel);
        SPDLOG_INFO("RemoteProxy::OnReconnected: strat={} channel replaced", m_strat_id);
    }

    /**
     * 处理来自 Runner 的 Channel B 帧（由 StrategyEngine::OnRemoteChannelB 在引擎线程调用）。
     *
     * kRemoteSyncReq 由 Engine 查 OrderManager 回应 kRemoteSyncResp。
     * 其余帧（PlaceOrderReq、SetTimer 等）重新 PostMsg 给引擎线程的现有 handler 处理，
     * 与 OutProcessProxy::PollChannelB 转发逻辑完全一致（复用引擎线程 handler）。
     *
     * @param msg_type    消息类型
     * @param data        payload 数据指针
     * @param len         payload 长度
     */
    void OnIncomingFrame(const uint32_t msg_type, const void* data, const uint32_t len) {
        if (msg_type == static_cast<uint32_t>(MsgId::kRemoteSyncReq)) {
            // SyncReq：需要查 OrderManager，通过 PostMsg 到引擎线程处理
            // buf 内容：strat_id（变长字符串）
            auto buf = std::make_shared<TBuffer>(
                static_cast<const char*>(m_strat_id.data()),
                static_cast<unsigned int>(m_strat_id.size()));
            m_strat_engine->PostMsg(static_cast<int>(MsgId::kRemoteSyncReq), buf);
            return;
        }

        // 其他 Channel B 消息（PlaceOrderReq / SetTimer 等）：
        // 包装为 TBuffer 直接转发给引擎线程（与 OutProcessProxy::PollChannelB 完全一致）
        auto buf = std::make_shared<TBuffer>(
            static_cast<const char*>(data),
            static_cast<unsigned int>(len));
        m_strat_engine->PostMsg(static_cast<int>(msg_type), buf);
    }

    const std::string& GetAccountId() const { return m_account_id; }
    const std::string& GetInstrument() const { return m_instrument; }

private:
    std::string        m_strat_id;
    std::string        m_account_id;   ///< 来自握手 payload（用于日志/调试）
    std::string        m_instrument;   ///< 来自握手 payload
    StrategyInfo       m_strat_info;   ///< 来自握手 payload 填充
    RemoteProxyConfig  m_config;
    MyHandler*         m_strat_engine{nullptr};

    mutable std::mutex                 m_channel_mutex;  ///< 保护 m_channel 的读写
    std::shared_ptr<IStrategyChannel>  m_channel;

    std::atomic<bool>     m_is_deleting{false};
    std::atomic<uint64_t> m_drop_count{0};   ///< Channel A 丢弃消息计数
};
