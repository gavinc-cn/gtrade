//
// WsPushClient —— 推送通道专用的 WebSocket 客户端（明文 ws://）
//
// 为什么不复用 WebSocketBase（三个语义差异，都是刻意的）：
//   1. 传输：WebSocketBase 用 asio_tls_client，只能连 wss://；本通道连本机 web_server
//      （ws://127.0.0.1:46013），明文即可，避免为回环链路引入证书签发/轮换。
//   2. 待发队列：WebSocketBase 的队列是**命令队列**语义（1000 深 + 10 分钟超时重发），
//      适合下单/撤单这类"必须送达交易所"的指令；推送事件**不能重放**（10 分钟前的行情/
//      委托状态没有意义），故本客户端**完全不缓存**：未就绪或背压过大即丢弃并计数，
//      交给对端"重连 + 按游标补查"恢复（方案 rev4 §4）。
//   3. 心跳：WebSocketBase 发裸字符串 "ping"；本通道契约是应用层 JSON 帧
//      （{"type":"ping","ts":...}），并要求对端失联超过 2× 心跳周期即重连。
//
// 线程模型：websocketpp 的回调全部在 io 线程（run() 单线程）；SendJson() 可从任意线程调用
// （内部 post 到 io 线程）。重连退避用 asio 定时器实现，**不阻塞 io 线程**。
//
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>

class WsPushClient {
public:
    // 收到对端一帧文本（在 io 线程回调，实现里不要做重活）
    using MessageHandler = std::function<void(const std::string& json)>;

    // uri:       形如 ws://127.0.0.1:46013/ws/engine
    // hello_json: 建连后**第一帧**发送的鉴权/握手帧（形如 {"type":"hello","secret":"..."}）
    // ping_ms:    应用层心跳周期；对端超过 2×该周期无任何帧即判定失联并重连
    WsPushClient(std::string uri, std::string hello_json, int ping_ms);
    ~WsPushClient() noexcept;

    WsPushClient(const WsPushClient&) = delete;
    WsPushClient& operator=(const WsPushClient&) = delete;

    void SetMessageHandler(MessageHandler handler);

    void Start();   // 发起连接并启动 io 线程（幂等）
    void Stop();    // 停止 io 线程并断开

    bool IsReady() const noexcept { return m_ready.load(std::memory_order_relaxed); }

    // 发送一帧文本。未就绪时**直接丢弃**并计入统计（不重放，交给对端重连后补查）。
    // 返回 false 表示这一帧被丢弃（调用方据此累计"缺口"，供诊断）。
    bool SendJson(const std::string& json);

    // 背压判定：websocketpp 在本版本没有写完成回调，无法直接看到 TCP 背压，
    // 故用"应用层已确认帧数"（对端在 ping/pong 里回带 rx）估算在途积压：
    //   Backlog() = 已发帧数 - 对端已收帧数
    // 对端未回带 rx 时 Backlog 恒为 0（判定失效但不误报）。
    void NotePeerRx(int64_t peer_rx);
    int64_t Backlog() const;

    struct Stats {
        uint64_t sent {};               // 成功交给 websocketpp 的帧数
        uint64_t dropped_not_ready {};  // 未就绪丢弃
        uint64_t recv {};               // 收到的帧数
        uint64_t reconnects {};         // 重连次数
    };
    Stats GetStats() const;

private:
    using ClientType = websocketpp::client<websocketpp::config::asio_client>;

    void DoSend(const std::string& json);
    void ArmHeartbeat();
    void OnHeartbeatTick();
    void ScheduleReconnect(const char* reason);
    void ConnectOnce();
    void CloseConnection(const std::string& reason);

    ClientType m_client {};
    const std::string m_uri {};
    const std::string m_hello_json {};
    const int m_ping_ms {};

    websocketpp::connection_hdl m_hdl {};
    std::thread m_io_thread {};
    std::atomic<bool> m_started {false};
    std::atomic<bool> m_stopping {false};
    std::atomic<bool> m_ready {false};
    std::atomic<int64_t> m_last_recv_ms {0};     // 最近一次收到对端帧（本地单调 ms）
    std::atomic<int64_t> m_reconnect_interval_ms {1000};

    // 背压估算（见 NotePeerRx 注释）
    std::atomic<int64_t> m_peer_rx {0};
    std::atomic<int> m_congested_ticks {0};

    // 统计
    std::atomic<uint64_t> m_sent {0};
    std::atomic<uint64_t> m_dropped_not_ready {0};
    std::atomic<uint64_t> m_recv {0};
    std::atomic<uint64_t> m_reconnects {0};

    MessageHandler m_on_message {};
};
