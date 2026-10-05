#include "ws_push_client.h"

#include <chrono>
#include <utility>

#include "my_utc.h"
#include "sonic_helper.h"
#include "spdlog/spdlog.h"

namespace {

// 在途积压上限（帧）：超过即认为对端消费不过来。推送事件可被"更新的一帧"或"一次补查"替代，
// 故处置是"记日志 + 主动断连重连（closing 告知）"，由对端重连后补查恢复（方案 rev4 §4）。
constexpr int64_t kMaxBacklogFrames = 512;

// 本地单调毫秒（心跳/看门狗计时用，不受系统时间调整影响）
int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

WsPushClient::WsPushClient(std::string uri, std::string hello_json, const int ping_ms)
    : m_uri(std::move(uri)), m_hello_json(std::move(hello_json)), m_ping_ms(ping_ms > 0 ? ping_ms : 10000) {
    m_client.clear_access_channels(websocketpp::log::alevel::all);
    m_client.clear_error_channels(websocketpp::log::elevel::all);
    m_client.init_asio();
    m_client.set_reuse_addr(true);

    m_client.set_open_handler([this](websocketpp::connection_hdl hdl) {
        m_hdl = hdl;
        m_ready = true;
        m_reconnect_interval_ms = 1000;   // 连上后重置退避
        m_last_recv_ms = NowMs();
        SPDLOG_INFO("[WsPushClient] connected: {}", m_uri);
        // 首帧必须是握手/鉴权帧（不放进 URL，避免凭据进访问日志）
        DoSend(m_hello_json);
        ArmHeartbeat();
    });

    m_client.set_fail_handler([this](websocketpp::connection_hdl) {
        m_ready = false;
        ScheduleReconnect("fail");
    });

    m_client.set_close_handler([this](websocketpp::connection_hdl) {
        m_ready = false;
        ScheduleReconnect("close");
    });

    m_client.set_message_handler([this](websocketpp::connection_hdl, ClientType::message_ptr msg) {
        m_recv.fetch_add(1, std::memory_order_relaxed);
        m_last_recv_ms = NowMs();
        const std::string payload = msg->get_payload();
        if (m_on_message) {
            m_on_message(payload);
        }
    });

    // 对端 ping（协议级）→ 回 pong；应用层心跳走 JSON 帧，两者互不替代
    m_client.set_ping_handler([](websocketpp::connection_hdl, const std::string&) { return true; });
    m_client.set_pong_handler([this](websocketpp::connection_hdl, const std::string&) {
        m_last_recv_ms = NowMs();
    });
}

WsPushClient::~WsPushClient() noexcept {
    Stop();
}

void WsPushClient::SetMessageHandler(MessageHandler handler) {
    m_on_message = std::move(handler);
}

void WsPushClient::Start() {
    if (m_started.exchange(true)) {
        return;
    }
    m_stopping = false;
    SPDLOG_INFO("[WsPushClient] starting, uri={}, ping={}ms", m_uri, m_ping_ms);
    m_io_thread = std::thread([this] { m_client.run(); });
    // connect 必须在 io 线程起来之前/之后都可以：websocketpp 会把它排进 io_service
    m_client.get_io_service().post([this] { ConnectOnce(); });
}

void WsPushClient::Stop() {
    if (!m_started.exchange(false)) {
        return;
    }
    m_stopping = true;
    m_ready = false;
    websocketpp::lib::error_code ec {};
    if (auto con = m_hdl.lock()) {
        m_client.close(m_hdl, websocketpp::close::status::going_away, "shutdown", ec);
    }
    m_client.stop();
    if (m_io_thread.joinable()) {
        m_io_thread.join();
    }
    SPDLOG_INFO("[WsPushClient] stopped: sent={}, recv={}, dropped_not_ready={}",
                m_sent.load(), m_recv.load(), m_dropped_not_ready.load());
}

void WsPushClient::ConnectOnce() {
    if (m_stopping) {
        return;
    }
    websocketpp::lib::error_code ec {};
    ClientType::connection_ptr con = m_client.get_connection(m_uri, ec);
    if (ec) {
        SPDLOG_ERROR("[WsPushClient] get_connection failed: {}", ec.message());
        ScheduleReconnect("get_connection");
        return;
    }
    m_hdl = con->get_handle();
    m_client.connect(con);
}

void WsPushClient::ScheduleReconnect(const char* reason) {
    if (m_stopping) {
        return;
    }
    const int64_t delay_ms = m_reconnect_interval_ms;
    // 指数退避 1s → 10s
    m_reconnect_interval_ms = std::min<int64_t>(delay_ms * 2, 10000);
    m_reconnects.fetch_add(1, std::memory_order_relaxed);
    SPDLOG_WARN("[WsPushClient] disconnected ({}), reconnect in {}ms", reason, delay_ms);

    // 用 websocketpp 提供的定时器（挂在同一个 io_service 上）做退避，
    // 避免像 WebSocketBase 那样 sleep 阻塞 io 线程
    m_client.set_timer(static_cast<long>(delay_ms), [this](websocketpp::lib::error_code const& ec) {
        if (!ec && !m_stopping) {
            ConnectOnce();
        }
    });
}

bool WsPushClient::SendJson(const std::string& json) {
    if (!m_ready.load(std::memory_order_relaxed)) {
        m_dropped_not_ready.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    // 跨线程投递：拷贝 json 进闭包，在 io 线程里真正发送
    m_client.get_io_service().post([this, json] { DoSend(json); });
    return true;
}

void WsPushClient::DoSend(const std::string& json) {
    if (!m_ready.load(std::memory_order_relaxed)) {
        m_dropped_not_ready.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    websocketpp::lib::error_code ec {};
    m_client.send(m_hdl, json, websocketpp::frame::opcode::text, ec);
    if (ec) {
        SPDLOG_WARN("[WsPushClient] send failed: {}", ec.message());
        return;
    }
    m_sent.fetch_add(1, std::memory_order_relaxed);
}

void WsPushClient::NotePeerRx(const int64_t peer_rx) {
    // 只在递增时采纳（对端重启会从 0 开始，此时重置基准避免出现巨大 backlog）
    const int64_t old = m_peer_rx.load(std::memory_order_relaxed);
    if (peer_rx < old) {
        m_peer_rx.store(peer_rx, std::memory_order_relaxed);
        return;
    }
    m_peer_rx.store(peer_rx, std::memory_order_relaxed);
}

int64_t WsPushClient::Backlog() const {
    const int64_t sent = static_cast<int64_t>(m_sent.load(std::memory_order_relaxed));
    const int64_t peer_rx = m_peer_rx.load(std::memory_order_relaxed);
    return sent - peer_rx;
}

void WsPushClient::ArmHeartbeat() {
    if (m_stopping) {
        return;
    }
    m_client.set_timer(static_cast<long>(m_ping_ms), [this](websocketpp::lib::error_code const& ec) {
        if (!ec) {
            OnHeartbeatTick();
        }
    });
}

void WsPushClient::OnHeartbeatTick() {
    if (m_stopping || !m_ready.load(std::memory_order_relaxed)) {
        return;
    }

    // 看门狗：超过 2× 心跳周期没收到对端任何帧 ⇒ 判定半开连接，主动断开重连
    const int64_t now = NowMs();
    const int64_t idle_ms = now - m_last_recv_ms.load(std::memory_order_relaxed);
    if (idle_ms > 2LL * m_ping_ms) {
        SPDLOG_WARN("[WsPushClient] no frame from peer for {}ms (>2x ping), reconnect", idle_ms);
        // 先把 closing 帧尽力发出去，让对端知道是主动断开（方案 §4）
        DoSend(R"({"type":"closing","reason":"heartbeat_timeout"})");
        CloseConnection("heartbeat_timeout");
        return;
    }

    // 应用层心跳（JSON 帧；不用协议级 ping，因为 JS 侧看不到 pong）
    DoSend(fmt::format(R"({{"type":"ping","ts":{}}})", MyUTC().Epoch13()));

    // 背压：连续两个心跳周期积压都超阈值 ⇒ 对端消费不过来，主动断连重连（对端补查恢复）
    if (Backlog() > kMaxBacklogFrames) {
        const int ticks = m_congested_ticks.fetch_add(1, std::memory_order_relaxed) + 1;
        SPDLOG_WARN("[WsPushClient] backlog={} > {} (tick {}), peer may be slow",
                    Backlog(), kMaxBacklogFrames, ticks);
        if (ticks >= 2) {
            DoSend(R"({"type":"closing","reason":"slow_consumer"})");
            CloseConnection("slow_consumer");
            return;
        }
    } else {
        m_congested_ticks.store(0, std::memory_order_relaxed);
    }

    ArmHeartbeat();
}

void WsPushClient::CloseConnection(const std::string& reason) {
    websocketpp::lib::error_code ec {};
    if (auto con = m_hdl.lock()) {
        m_client.close(m_hdl, websocketpp::close::status::normal, reason, ec);
    }
    m_ready = false;
    ScheduleReconnect(reason.c_str());
}

WsPushClient::Stats WsPushClient::GetStats() const {
    Stats stats {};
    stats.sent = m_sent.load(std::memory_order_relaxed);
    stats.dropped_not_ready = m_dropped_not_ready.load(std::memory_order_relaxed);
    stats.recv = m_recv.load(std::memory_order_relaxed);
    stats.reconnects = m_reconnects.load(std::memory_order_relaxed);
    return stats;
}
