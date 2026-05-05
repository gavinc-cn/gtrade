//
// ZMQ 策略通信通道实现
//
// 实现 IStrategyChannel 接口，支持两种构造模式：
//
//   Engine 侧（ROUTER）：
//     构造时传入共享的 ROUTER socket 指针和 peer identity（= strat_id）。
//     发送时通过三帧格式路由到对应 Runner。
//     接收由 ZmqAcceptor::AcceptLoop 统一处理，不调用 ReadFrame()。
//     DispatchIncoming() 由 AcceptLoop 线程调用，心跳直接 atomic store，其余帧由调用方处理。
//
//   Runner 侧（DEALER）：
//     构造时创建独占 DEALER socket，设置 identity=strat_id，connect 到 engine_addr。
//     同时启动 zmq_socket_monitor + MonitorLoop 线程，感知 ZMQ_EVENT_CONNECTED。
//     主循环通过 PollNeedHandshake() 检测是否需要重发握手 + SyncReq。
//

#pragma once

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <zmq.hpp>
#include "i_strategy_channel.h"
#include "msg_id.h"
#include "spdlog/spdlog.h"

/**
 * ZMQ ROUTER/DEALER 实现的策略通信通道
 *
 * 线程安全：
 *   - WriteFrame()  加 m_write_mutex（ROUTER socket 被多个 channel 共享）
 *   - DispatchIncoming() 被 AcceptLoop 线程调用（写 m_last_heartbeat_us）
 *   - GetLastHeartbeatUs() 被 IsAlive() 读取（两者都是 atomic，无锁）
 *   - PollNeedHandshake() 被主循环调用，MonitorLoop 写（atomic exchange）
 */
class StrategyZmqChannel final : public IStrategyChannel {
public:
    // ── Engine 侧构造 ────────────────────────────────────────────────────────

    /**
     * Engine 侧构造：共享 ROUTER socket + peer identity。
     *
     * @param router_sock  ZmqAcceptor 拥有的 ROUTER socket（非 owning 指针）
     * @param peer_identity  Runner 的 ZMQ identity（= strat_id 字节串）
     */
    StrategyZmqChannel(zmq::socket_t* router_sock, std::string peer_identity)
        : m_router_sock(router_sock)
        , m_peer_identity(std::move(peer_identity))
    {
        // Engine 侧 IsConnected() 固定返回 true：ROUTER socket 无单条连接状态，
        // 存活检测完全依赖 IsAlive() 的心跳超时逻辑。
        m_connected.store(true, std::memory_order_release);
    }

    // ── Runner 侧构造 ────────────────────────────────────────────────────────

    /**
     * Runner 侧构造：独占 DEALER socket，connect 到 engine_addr，identity = strat_id。
     * 内部启动 zmq_socket_monitor + MonitorLoop 线程，感知连接/重连事件。
     *
     * @param ctx         ZMQ context（由调用方管理生命周期）
     * @param engine_addr Engine 地址（如 "tcp://192.168.1.10:19000"）
     * @param strat_id    策略 ID，作为 DEALER identity 发给 Engine（Engine 靠此路由回包）
     */
    StrategyZmqChannel(zmq::context_t& ctx,
                       const std::string& engine_addr,
                       const std::string& strat_id)
        : m_dealer_sock(std::make_unique<zmq::socket_t>(ctx, ZMQ_DEALER))
        , m_peer_identity(strat_id)
    {
        // 设置 ZMTP 协议层心跳（加速检测静默断连：NAT 超时、网线拔掉等）
        // 应用层心跳（kSubprocHeartbeat）在主循环中额外发送
        const int ivl_ms = 3000;
        const int timeout_ms = 9000;
        m_dealer_sock->set(zmq::sockopt::heartbeat_ivl,     ivl_ms);
        m_dealer_sock->set(zmq::sockopt::heartbeat_timeout, timeout_ms);

        // 设置 identity（= strat_id），Engine ROUTER 用此路由回包
        m_dealer_sock->set(zmq::sockopt::routing_id,
                           zmq::const_buffer(strat_id.data(), strat_id.size()));

        // 启动 socket monitor，监听 CONNECTED / RECONNECTED / DISCONNECTED 事件
        const std::string monitor_addr = "inproc://monitor-runner-" + strat_id;
        zmq_socket_monitor(m_dealer_sock->handle(), monitor_addr.c_str(),
                           ZMQ_EVENT_CONNECTED |
                           ZMQ_EVENT_DISCONNECTED);

        // 启动 MonitorLoop 线程
        m_monitor_running.store(true, std::memory_order_release);
        m_monitor_thread = std::thread(&StrategyZmqChannel::MonitorLoop, this,
                                       zmq::context_t{}, monitor_addr);

        // connect（立即返回，TCP 握手后台进行；DEALER 有内部队列，消息不会丢）
        m_dealer_sock->connect(engine_addr);
    }

    ~StrategyZmqChannel() override {
        Close();
    }

    // 禁止拷贝
    StrategyZmqChannel(const StrategyZmqChannel&) = delete;
    StrategyZmqChannel& operator=(const StrategyZmqChannel&) = delete;

    // ── IStrategyChannel 实现 ─────────────────────────────────────────────────

    bool IsConnected() const override {
        // Engine 侧：固定 true（ROUTER 无连接状态，由心跳超时判断存活）
        // Runner 侧：基于 monitor 事件（最后一次 CONNECTED/DISCONNECTED）
        return m_connected.load(std::memory_order_acquire);
    }

    void Close() override {
        // 停止 monitor 线程
        m_monitor_running.store(false, std::memory_order_release);
        if (m_monitor_thread.joinable()) {
            m_monitor_thread.join();
        }
        // 关闭 socket（Engine 侧 m_router_sock 非 owning，不 close）
        if (m_dealer_sock) {
            m_dealer_sock->close();
        }
    }

    /**
     * 发送带 payload 的帧。
     *
     * Engine 侧：通过 ROUTER socket 向 m_peer_identity 发 3 帧（identity/空/正文）。
     *            使用 dontwait 防止慢 Runner 阻塞引擎线程（HWM 满时返回 false）。
     * Runner 侧：通过 DEALER socket 发 2 帧（空分隔帧/正文）。
     */
    bool WriteFrame(const uint32_t msg_type, const void* data, const uint32_t len) override {
        return SendMsg(msg_type, data, len);
    }

    /// 发送无 payload 的帧（心跳、控制命令等）
    bool WriteFrame(const uint32_t msg_type) override {
        return SendMsg(msg_type, nullptr, 0);
    }

    /**
     * Runner 侧非阻塞读帧。
     *
     * DEALER 接收时：Frame 0 = 空分隔帧（丢弃），Frame 1 = 正文。
     * 正文格式：msg_type(4B) + payload_len(4B) + payload
     *
     * 注意：kSubprocHeartbeat 是 Runner→Engine 方向，Runner 侧不应收到此消息。
     *
     * @return true 表示读到一帧并已回调
     */
    bool ReadFrame(const FrameCallback& cb) override {
        if (!m_dealer_sock) return false;

        zmq::message_t empty;
        if (!m_dealer_sock->recv(empty, zmq::recv_flags::dontwait)) {
            return false;  // 无消息
        }

        zmq::message_t body;
        m_dealer_sock->recv(body, zmq::recv_flags::none);

        if (body.size() < 8) {
            SPDLOG_WARN("StrategyZmqChannel::ReadFrame: frame too short ({} bytes)", body.size());
            return false;
        }

        uint32_t msg_type = 0;
        uint32_t payload_len = 0;
        memcpy(&msg_type,    body.data(), 4);
        memcpy(&payload_len, static_cast<const char*>(body.data()) + 4, 4);

        const void* payload = static_cast<const char*>(body.data()) + 8;
        cb(msg_type, payload, payload_len);
        return true;
    }

    int64_t GetLastHeartbeatUs() const override {
        return m_last_heartbeat_us.load(std::memory_order_acquire);
    }

    bool PollNeedHandshake() override {
        // 原子 exchange(false)：每次重连只触发一次
        return m_need_handshake.exchange(false, std::memory_order_acq_rel);
    }

    // ── Engine 侧专用 ────────────────────────────────────────────────────────

    /**
     * 由 ZmqAcceptor 线程调用，将收到的帧分发给对应 channel。
     *
     * 心跳帧（kSubprocHeartbeat）：直接 atomic store 本地时间（steady_clock），
     *   不走 PostMsg，避免不必要的线程投递。心跳频率 500ms，atomic store 足够安全。
     * 其他帧：由调用方（AcceptLoop）处理（转发给 Proxy::OnIncomingFrame）。
     *
     * @return true 表示已消化（调用方无需再处理），false 表示需继续处理
     */
    bool DispatchIncoming(const uint32_t msg_type, const void* /*data*/, const uint32_t /*len*/) {
        if (msg_type == static_cast<uint32_t>(MsgId::kSubprocHeartbeat)) {
            // 记录 Engine 本地收到心跳的时间（不依赖 Runner 时钟，无 NTP 偏差风险）
            const int64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            m_last_heartbeat_us.store(now_us, std::memory_order_release);
            return true;  // 已消化，调用方不需再处理
        }
        return false;  // 需继续处理
    }

private:
    /**
     * 实际发送逻辑。
     *
     * Engine 侧：ROUTER 三帧（identity / 空 / 正文），dontwait 防阻塞。
     * Runner 侧：DEALER 两帧（空分隔 / 正文），正常发送（DEALER 有内部队列）。
     */
    bool SendMsg(const uint32_t msg_type, const void* data, const uint32_t len) {
        // 构造正文帧：msg_type(4B) + payload_len(4B) + payload
        zmq::message_t body(8 + len);
        const uint32_t header[2] = {msg_type, len};
        memcpy(body.data(), header, 8);
        if (len > 0 && data != nullptr) {
            memcpy(static_cast<char*>(body.data()) + 8, data, len);
        }

        try {
            if (m_router_sock) {
                // Engine 侧：ROUTER 多帧路由
                // 加锁：ROUTER socket 被所有 RemoteProxy 共享，发送必须串行
                std::lock_guard<std::mutex> lock(m_write_mutex);
                // dontwait：HWM 满时立即返回 EAGAIN，不阻塞持锁线程
                // 慢 Runner 触发 HWM 时只影响该 Runner，不拖慢其他策略的行情分发
                m_router_sock->send(
                    zmq::const_buffer(m_peer_identity.data(), m_peer_identity.size()),
                    zmq::send_flags::sndmore | zmq::send_flags::dontwait);
                m_router_sock->send(
                    zmq::const_buffer("", 0),
                    zmq::send_flags::sndmore | zmq::send_flags::dontwait);
                m_router_sock->send(body, zmq::send_flags::dontwait);
            } else if (m_dealer_sock) {
                // Runner 侧：DEALER 两帧
                m_dealer_sock->send(
                    zmq::const_buffer("", 0),
                    zmq::send_flags::sndmore);
                m_dealer_sock->send(body, zmq::send_flags::none);
            } else {
                return false;
            }
            return true;
        } catch (const zmq::error_t& e) {
            if (e.num() == EAGAIN) {
                // HWM 满（Engine 侧）或 socket 无接收方——丢弃，调用方递增 drop_count
                return false;
            }
            SPDLOG_ERROR("StrategyZmqChannel::SendMsg: zmq error {}: {}", e.num(), e.what());
            throw;
        }
    }

    /**
     * Monitor 线程：监听 ZMQ socket 连接事件。
     *
     * ZMQ_EVENT_CONNECTED：设置 m_need_handshake = true，
     *   主循环在下次迭代中检测到并发送握手 + SyncReq。
     * ZMQ_EVENT_DISCONNECTED：更新 m_connected 标志。
     *
     * @param monitor_ctx  独立的 ZMQ context（与主 socket 同一进程内）
     * @param monitor_addr inproc 地址
     */
    void MonitorLoop(zmq::context_t monitor_ctx, const std::string& monitor_addr) {
        zmq::socket_t mon(monitor_ctx, ZMQ_PAIR);
        mon.connect(monitor_addr);

        while (m_monitor_running.load(std::memory_order_acquire)) {
            zmq::message_t evt_msg;
            if (!mon.recv(evt_msg, zmq::recv_flags::dontwait)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            // 第二帧是地址，读取并丢弃
            zmq::message_t addr_msg;
            mon.recv(addr_msg, zmq::recv_flags::none);

            if (evt_msg.size() < sizeof(uint16_t)) continue;

            uint16_t event_id = 0;
            memcpy(&event_id, evt_msg.data(), sizeof(uint16_t));

            if (event_id == ZMQ_EVENT_CONNECTED) {
                m_connected.store(true, std::memory_order_release);
                // 触发握手：主循环检测到后发 kRemoteHandshake + kRemoteSyncReq
                m_need_handshake.store(true, std::memory_order_release);
                SPDLOG_INFO("StrategyZmqChannel: CONNECTED event, need handshake set");
            } else if (event_id == ZMQ_EVENT_DISCONNECTED) {
                m_connected.store(false, std::memory_order_release);
                SPDLOG_INFO("StrategyZmqChannel: DISCONNECTED");
            }
        }
    }

    // ── Engine 侧成员 ───────────────────────────────────────────────────────
    zmq::socket_t* m_router_sock{nullptr};  ///< Engine 侧（非 owning）
    std::mutex     m_write_mutex;           ///< ROUTER socket 共享，写需加锁

    // ── Runner 侧成员 ───────────────────────────────────────────────────────
    std::unique_ptr<zmq::socket_t> m_dealer_sock;  ///< Runner 侧（owning）
    std::thread        m_monitor_thread;
    std::atomic<bool>  m_monitor_running{false};

    // ── 共享成员 ────────────────────────────────────────────────────────────
    std::string        m_peer_identity;            ///< strat_id 字节串

    std::atomic<bool>    m_connected{false};
    std::atomic<int64_t> m_last_heartbeat_us{0};   ///< Engine 本地收到心跳的时间（steady_clock us）
    std::atomic<bool>    m_need_handshake{false};  ///< Runner 侧：monitor 线程写，主循环读
};
