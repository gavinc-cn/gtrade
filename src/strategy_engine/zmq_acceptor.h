//
// ZMQ 连接接收器
//
// ZmqAcceptor 绑定 ZMQ ROUTER socket，在独立线程（AcceptLoop）中接收所有 Runner 的帧。
//
// 线程安全设计：
//   AcceptLoop 不直接读写 m_strategy_proxy_map。
//   所有涉及 Proxy 的操作都通过 PostMsg 委托给 StrategyEngine 线程（引擎线程），
//   从而利用引擎线程的单线程模型保证 m_strategy_proxy_map 的线程安全。
//
//   AcceptLoop → PostMsg(kRemoteStrategyConnected, peer_id + RemoteHandshakePayload)
//             → PostMsg(kRemoteChannelB, peer_id + msg_type + payload)
//
// StrategyEngine 引擎线程在 OnRemoteStrategyConnected / OnRemoteChannelB handler 中安全处理。
//

#pragma once

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <zmq.hpp>
#include "i_strategy_channel.h"
#include "msg_id.h"
#include "type_define.h"
#include "utilities/tbuffer.h"
#include "spdlog/spdlog.h"

/// ZmqAcceptor 内部事件帧格式：kRemoteStrategyConnected
/// buf = [peer_id_len(4B)][peer_id(peer_id_len B)][RemoteHandshakePayload]
struct RemoteConnectedBuf {
    uint32_t peer_id_len;
    // 紧随其后：peer_id 字节串（不含 NUL）
    // 紧随其后：RemoteHandshakePayload
};

/// ZmqAcceptor 内部事件帧格式：kRemoteChannelB
/// buf = [peer_id_len(4B)][peer_id(peer_id_len B)][msg_type(4B)][payload_len(4B)][payload...]
struct RemoteChannelBBuf {
    uint32_t peer_id_len;
    // 紧随其后：peer_id + msg_type(4B) + payload_len(4B) + payload
};

/**
 * ZMQ ROUTER 连接接收器
 *
 * 持有唯一一个 ROUTER socket，绑定到 bind_port，在独立线程中接收所有 Runner 的帧，
 * 通过 PostMsg 委托给 StrategyEngine 引擎线程处理。
 *
 * 生命周期：由 StrategyEngine 持有 unique_ptr<ZmqAcceptor>，
 *   Init() → Start() 后进入 AcceptLoop，Stop() 停止线程。
 */
class ZmqAcceptor {
public:
    /**
     * 构造 ZmqAcceptor。
     *
     * @param ctx          ZMQ context（由调用方管理生命周期）
     * @param bind_port    监听端口（如 19000）
     * @param strat_engine StrategyEngine 的 MyHandler 指针，用于 PostMsg 委托
     */
    ZmqAcceptor(zmq::context_t& ctx,
                const int bind_port,
                MyHandler* const strat_engine)
        : m_ctx(ctx)
        , m_router_sock(ctx, ZMQ_ROUTER)
        , m_bind_port(bind_port)
        , m_strat_engine(strat_engine)
    {
        // 设置 SNDHWM（防止慢 Runner 缓冲积压过多消息）
        // dontwait send 配合此设置：超 HWM 时立即返回 EAGAIN 而非阻塞
        m_router_sock.set(zmq::sockopt::sndhwm, 2000);
        // ROUTER_MANDATORY：发送到不存在的 peer 时返回错误（而非静默丢弃）
        m_router_sock.set(zmq::sockopt::router_mandatory, 1);
        // 绑定
        const std::string addr = "tcp://*:" + std::to_string(bind_port);
        m_router_sock.bind(addr);
        SPDLOG_INFO("ZmqAcceptor: ROUTER socket bound to {}", addr);
    }

    ~ZmqAcceptor() {
        Stop();
    }

    ZmqAcceptor(const ZmqAcceptor&) = delete;
    ZmqAcceptor& operator=(const ZmqAcceptor&) = delete;

    /// 启动 AcceptLoop 线程
    void Start() {
        m_running.store(true, std::memory_order_release);
        m_accept_thread = std::thread(&ZmqAcceptor::AcceptLoop, this);
        SPDLOG_INFO("ZmqAcceptor: AcceptLoop started on port {}", m_bind_port);
    }

    /// 停止 AcceptLoop 线程（阻塞等待退出）
    void Stop() {
        m_running.store(false, std::memory_order_release);
        if (m_accept_thread.joinable()) {
            m_accept_thread.join();
        }
    }

    /// 获取 ROUTER socket 引用（RemoteProxy 发送时需要）
    zmq::socket_t& GetRouterSocket() { return m_router_sock; }

    // ── 内部帧序列化/反序列化 helpers ───────────────────────────────────────

    /// 构造 kRemoteStrategyConnected 的 BufPtr
    /// buf = [peer_id_len(4B)][peer_id][RemoteHandshakePayload]
    static BufPtr MakeHandshakeBuf(const std::string& peer_id,
                                   const void* payload,
                                   const uint32_t /*payload_len*/) {
        const uint32_t id_len = static_cast<uint32_t>(peer_id.size());
        const size_t total = sizeof(uint32_t) + id_len + sizeof(RemoteHandshakePayload);
        auto buf = std::make_shared<TBuffer>(static_cast<unsigned int>(total));
        char* p = buf->MutableData();
        memcpy(p, &id_len, sizeof(uint32_t));
        p += sizeof(uint32_t);
        memcpy(p, peer_id.data(), id_len);
        p += id_len;
        memcpy(p, payload, sizeof(RemoteHandshakePayload));
        return buf;
    }

    /// 解析 kRemoteStrategyConnected 的 BufPtr
    static std::pair<std::string, RemoteHandshakePayload>
    ParseHandshakeBuf(const BufPtr& buf) {
        const char* p = buf->Data();
        uint32_t id_len = 0;
        memcpy(&id_len, p, sizeof(uint32_t));
        p += sizeof(uint32_t);

        std::string peer_id(p, id_len);
        p += id_len;

        RemoteHandshakePayload hs{};
        memcpy(&hs, p, sizeof(RemoteHandshakePayload));
        return {std::move(peer_id), hs};
    }

    /// 构造 kRemoteChannelB 的 BufPtr
    /// buf = [peer_id_len(4B)][peer_id][msg_type(4B)][payload_len(4B)][payload]
    static BufPtr MakeIncomingBuf(const std::string& peer_id,
                                  const uint32_t msg_type,
                                  const void* payload,
                                  const uint32_t payload_len) {
        const uint32_t id_len = static_cast<uint32_t>(peer_id.size());
        const size_t total = sizeof(uint32_t) + id_len + sizeof(uint32_t) + sizeof(uint32_t) + payload_len;
        auto buf = std::make_shared<TBuffer>(static_cast<unsigned int>(total));
        char* p = buf->MutableData();
        memcpy(p, &id_len, sizeof(uint32_t));
        p += sizeof(uint32_t);
        memcpy(p, peer_id.data(), id_len);
        p += id_len;
        memcpy(p, &msg_type, sizeof(uint32_t));
        p += sizeof(uint32_t);
        memcpy(p, &payload_len, sizeof(uint32_t));
        p += sizeof(uint32_t);
        if (payload_len > 0 && payload != nullptr) {
            memcpy(p, payload, payload_len);
        }
        return buf;
    }

    /// 解析 kRemoteChannelB 的 BufPtr
    struct IncomingFrame {
        std::string peer_id;
        uint32_t    msg_type;
        uint32_t    payload_len;
        const char* payload;  ///< 指向 buf->Data() 内部，调用方须保持 buf 存活
    };

    static IncomingFrame ParseIncomingBuf(const BufPtr& buf) {
        const char* p = buf->Data();
        uint32_t id_len = 0;
        memcpy(&id_len, p, sizeof(uint32_t));
        p += sizeof(uint32_t);

        IncomingFrame frame{};
        frame.peer_id = std::string(p, id_len);
        p += id_len;

        memcpy(&frame.msg_type, p, sizeof(uint32_t));
        p += sizeof(uint32_t);
        memcpy(&frame.payload_len, p, sizeof(uint32_t));
        p += sizeof(uint32_t);
        frame.payload = p;
        return frame;
    }

private:
    /**
     * AcceptLoop：独立线程，接收 ROUTER socket 上的所有帧。
     *
     * 规则：
     *   - 握手帧（kRemoteHandshake）：PostMsg(kRemoteStrategyConnected) → 引擎线程创建 Proxy
     *   - 其他帧（Channel B / Heartbeat）：PostMsg(kRemoteChannelB) → 引擎线程路由到对应 Proxy
     *   - 不直接读写 m_strategy_proxy_map，保证线程安全
     */
    void AcceptLoop() {
        while (m_running.load(std::memory_order_acquire)) {
            zmq::message_t identity, empty, body;

            // ROUTER 接收：Frame 0 = peer identity，Frame 1 = 空分隔，Frame 2 = 正文
            // 使用 dontwait + 短 sleep 避免 busy loop
            if (!m_router_sock.recv(identity, zmq::recv_flags::dontwait)) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }

            m_router_sock.recv(empty, zmq::recv_flags::none);
            m_router_sock.recv(body,  zmq::recv_flags::none);

            const std::string peer_id = identity.to_string();

            if (body.size() < 8) {
                SPDLOG_WARN("ZmqAcceptor: frame too short ({} bytes) from peer={}",
                            body.size(), peer_id);
                continue;
            }

            uint32_t msg_type = 0;
            uint32_t payload_len = 0;
            memcpy(&msg_type,    body.data(), 4);
            memcpy(&payload_len, static_cast<const char*>(body.data()) + 4, 4);
            const void* payload = static_cast<const char*>(body.data()) + 8;

            if (msg_type == static_cast<uint32_t>(MsgId::kRemoteHandshake)) {
                // 握手帧：payload = RemoteHandshakePayload
                if (payload_len < sizeof(RemoteHandshakePayload)) {
                    SPDLOG_WARN("ZmqAcceptor: handshake payload too short ({} bytes) from peer={}",
                                payload_len, peer_id);
                    continue;
                }
                // 委托给引擎线程：引擎线程安全地创建或更新 RemoteProxy
                auto buf = MakeHandshakeBuf(peer_id, payload, payload_len);
                m_strat_engine->PostMsg(
                    static_cast<int>(MsgId::kRemoteStrategyConnected), buf);

            } else {
                // Channel B（PlaceOrderReq / SetTimer 等）或 Heartbeat：
                // 委托给引擎线程路由到对应 Proxy 的 OnIncomingFrame
                auto buf = MakeIncomingBuf(peer_id, msg_type, payload, payload_len);
                m_strat_engine->PostMsg(
                    static_cast<int>(MsgId::kRemoteChannelB), buf);
            }
        }
        SPDLOG_INFO("ZmqAcceptor: AcceptLoop exited");
    }

    zmq::context_t&   m_ctx;
    zmq::socket_t     m_router_sock;
    const int         m_bind_port;
    MyHandler*        m_strat_engine{nullptr};

    std::thread        m_accept_thread;
    std::atomic<bool>  m_running{false};
};
