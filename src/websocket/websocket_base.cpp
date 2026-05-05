
#include "pch.h"
#include "websocket_base.h"
#include "my_utc.h"
#include "zrtools/zrt_time.h"
#include "zrtools/zrt_fill.h"
#include "i_client.h"
#include "tbuffer.h"
#include <openssl/ssl.h>
#include <openssl/err.h>

WebSocketBase::WebSocketBase(const std::string& uri, const std::string& proxy):
m_uri(uri),
m_proxy(proxy)
{
    using std::placeholders::_1;
    using std::placeholders::_2;

    m_client.clear_access_channels(websocketpp::log::alevel::frame_header);
    m_client.clear_access_channels(websocketpp::log::alevel::frame_payload);
    //        m_client.set_access_channels(websocketpp::log::alevel::all);
    //        m_client.set_error_channels(websocketpp::log::elevel::all);
    // m_client.set_access_channels(websocketpp::log::alevel::connect);
    // m_client.set_access_channels(websocketpp::log::alevel::disconnect);
    // m_client.set_access_channels(websocketpp::log::alevel::app);
    m_client.init_asio();
    m_client.set_tls_init_handler(std::bind(&WebSocketBase::on_tls_init, this, _1));
    m_client.set_open_handler(std::bind(&WebSocketBase::on_open, this, _1));
    m_client.set_close_handler(std::bind(&WebSocketBase::on_close, this, _1));
    m_client.set_fail_handler(std::bind(&WebSocketBase::on_fail, this, _1));
    m_client.set_message_handler(std::bind(&WebSocketBase::on_message, this, _1, _2));
    m_client.set_ping_handler(std::bind(&WebSocketBase::on_ping, this, _1, _2));
    m_client.set_pong_handler(std::bind(&WebSocketBase::on_pong, this, _1, _2));
    m_client.set_pong_timeout_handler(std::bind(&WebSocketBase::on_pong_timeout, this, _1, _2));
    m_client.set_interrupt_handler(std::bind(&WebSocketBase::on_interrupt, this, _1));
    m_client.set_http_handler(std::bind(&WebSocketBase::on_http, this, _1));
    m_client.set_validate_handler(std::bind(&WebSocketBase::on_validate, this, _1));

}

WebSocketBase::~WebSocketBase() noexcept {
    m_open = false;

    // Stop the client to exit the event loop
    m_client.stop();

    // Wait for thread to finish
    if (m_asio_thread.joinable()) {
        m_asio_thread.join();
    }
}

context_ptr WebSocketBase::on_tls_init(websocketpp::connection_hdl hdl) {
    m_tls_init = std::chrono::high_resolution_clock::now();

    // Use sslv23_client for broader compatibility
    context_ptr ctx = websocketpp::lib::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23_client);

    try {
        // Set TLS options with maximum compatibility
        ctx->set_options(boost::asio::ssl::context::default_workarounds
                         | boost::asio::ssl::context::no_sslv2
                         | boost::asio::ssl::context::no_sslv3
                         | boost::asio::ssl::context::single_dh_use
        );

        // Try to load system CA certificates, but don't fail if unavailable
        try {
            ctx->set_default_verify_paths();
        } catch (...) {
            SPDLOG_WARN("Failed to load default CA certificates, continuing anyway");
        }

        // Disable certificate verification
        ctx->set_verify_mode(boost::asio::ssl::verify_none);

        // Configure OpenSSL for maximum compatibility
        SSL_CTX* ssl_ctx = ctx->native_handle();
        if (ssl_ctx) {
            // Allow TLS 1.0+ for maximum compatibility (server will negotiate highest)
            SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_VERSION);

            // Set cipher suites for broad compatibility
            SSL_CTX_set_cipher_list(ssl_ctx, "DEFAULT:!aNULL:!eNULL:!EXPORT:!DES:!MD5:!PSK:!RC4");

            // Enable all workarounds for compatibility
            SSL_CTX_set_options(ssl_ctx, SSL_OP_ALL);
        }

        SPDLOG_INFO("TLS context initialized: compatible mode");
    } catch (const std::exception& e) {
        SPDLOG_ERROR("TLS init error: {}", e.what());
    }
    return ctx;
}

// This method will block until the connection is complete
void WebSocketBase::connect() {
    // Create a new connection to the given URI
    websocketpp::lib::error_code ec;
    SPDLOG_INFO("uri={}", m_uri);
    const client::connection_ptr con = m_client.get_connection(m_uri, ec);
    if (ec) {
        SPDLOG_ERROR("Get Connection Error: {}", ec.message());
        return;
    }

    // 设置HTTP代理 (格式: http://host:port)
    if (!m_proxy.empty()) {
        con->set_proxy(fmt::format("http://{}", m_proxy));
    }

    // Grab a handle for this connection so we can talk to it in a thread
    // safe manor after the event loop starts.
    m_hdl = con->get_handle();

    // 设置重连选项
    // con->set_reconnect_interval(5000); // 重连间隔为5秒
    // con->set_reconnect_attempts(5);    // 最大重连次数为5次

    // Queue the connection. No DNS queries or network connections will be
    // made until the io_service event loop is run.
    ec.clear();
    m_client.connect(con);
    if (ec) {
        SPDLOG_ERROR("Connect Error: {}", ec.message());
    }
}

void WebSocketBase::run() {
    connect();
    m_asio_thread = websocketpp::lib::thread(&client::run, &m_client);
}

void WebSocketBase::on_open(websocketpp::connection_hdl) {
    try {
        SPDLOG_INFO("WebSocket connection opened");
        bool was_reconnecting = m_is_reconnecting;
        m_is_reconnecting = false;
        m_reconnect_interval = 0;
        m_last_msg_recv_time = MyUTC().Epoch19();
        m_open = true;
        set_timer();
        on_open_impl();

        // Flush pending messages after connection is established
        flush_pending_messages();

        // Notify subclass if this was a reconnection (not initial connection)
        if (was_reconnecting) {
            SPDLOG_INFO("Reconnection successful, notifying subclass for data sync");
            on_reconnected_impl();
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in on_open: {}", e.what());
        m_open = false;
        schedule_reconnect();
    }
}

void WebSocketBase::on_close(websocketpp::connection_hdl) {
    try {
        SPDLOG_INFO("");
        m_open = false;
        // Cancel timer to prevent it from firing on invalid connection
        {
            std::lock_guard<std::mutex> lock(m_timer_mutex);
            if (m_ping_timer) {
                m_ping_timer->cancel();
            }
        }
        on_close_impl();
        schedule_reconnect();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in on_close: {}", e.what());
        // Still try to reconnect even if exception occurs
        m_open = false;
        schedule_reconnect();
    }
}

void WebSocketBase::on_fail(websocketpp::connection_hdl) {
    try {
        SPDLOG_INFO("");
        m_open = false;
        // Cancel timer to prevent it from firing on invalid connection
        {
            std::lock_guard<std::mutex> lock(m_timer_mutex);
            if (m_ping_timer) {
                m_ping_timer->cancel();
            }
        }
        on_fail_impl();
        // Reset reconnecting flag before scheduling next reconnect attempt
        m_is_reconnecting = false;
        schedule_reconnect();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in on_fail: {}", e.what());
        // Still try to reconnect even if exception occurs
        m_open = false;
        m_is_reconnecting = false;
        schedule_reconnect();
    }
}

void WebSocketBase::schedule_reconnect() {
    // 使用 atomic compare_exchange 避免并发重连
    bool expected = false;
    if (!m_is_reconnecting.compare_exchange_strong(expected, true)) {
        SPDLOG_WARN("is already reconnecting, skip");
        return;
    }

    SPDLOG_INFO("wait for {} seconds to reconnect", ++m_reconnect_interval);
    sleep(m_reconnect_interval);
    m_client.get_io_service().post(websocketpp::lib::bind(&WebSocketBase::reconnect, this));
}

void WebSocketBase::reconnect() {
    SPDLOG_INFO("start to reconnect...");
    // 清理旧连接状态
    if (m_hdl.lock()) {
        websocketpp::lib::error_code ec;
        m_client.close(m_hdl, websocketpp::close::status::going_away, "reconnecting", ec);
        if (ec) {
            SPDLOG_ERROR("close old connection error: {}, may already be closed", ec.message());
        }
    }
    m_open = false;
    // 重置句柄
    m_hdl.reset();
    // 创建新连接
    connect();
    // connect() 是异步的，on_open/on_fail 回调会处理 m_is_reconnecting 标志的重置
}

void WebSocketBase::set_timer() {
    ResetTimer();
}

void WebSocketBase::ResetTimer() {
    std::lock_guard<std::mutex> lock(m_timer_mutex);
    if (m_ping_timer) {
        m_ping_timer->expires_after(std::chrono::milliseconds(20000));
        m_ping_timer->async_wait(std::bind(&WebSocketBase::on_timer, this, std::placeholders::_1));
    } else {
        m_ping_timer = m_client.set_timer(20000,std::bind(
&WebSocketBase::on_timer, this, std::placeholders::_1));
    }
}

void WebSocketBase::close_on_timeout() {
    // Simply close the connection, on_close() will handle reconnection
    websocketpp::lib::error_code ec;
    m_client.close(m_hdl, websocketpp::close::status::going_away, "timeout", ec);
    if (ec) {
        SPDLOG_ERROR("close connection error: {}, may already be closing", ec.message());
    } else {
        SPDLOG_ERROR("websocket connection closed due to timeout");
    }
}

bool WebSocketBase::on_timer(websocketpp::lib::error_code const & ec) {
    try {
        if (ec) {
            SPDLOG_WARN("{}", ec.message());
            return true;
        }

        // Check if connection is still open before timer operations
        if (!m_open) {
            SPDLOG_ERROR("timer skipped: connection not open");
            return false;  // Cancel timer
        }

        const int64_t epoch19 = MyUTC().Epoch19();
        if (epoch19 - m_last_msg_recv_time > 25000 * zrt::kMega) {
            m_last_msg_recv_time = epoch19;
            close_on_timeout();
        } else {
            SPDLOG_DEBUG("send ping");
            Send("ping");
            set_timer();
        }
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in on_timer: {}", e.what());
        return false;  // Cancel timer on exception
    }
}

bool WebSocketBase::on_ping(websocketpp::connection_hdl hdl, const std::string& payload) {
    SPDLOG_INFO("{}", payload);
    return true;
}

void WebSocketBase::on_pong(websocketpp::connection_hdl hdl, const std::string& payload) {
    SPDLOG_INFO("{}", payload);
}

void WebSocketBase::on_pong_timeout(websocketpp::connection_hdl hdl, const std::string& payload) {
    SPDLOG_INFO("{}", payload);
}

void WebSocketBase::on_interrupt(websocketpp::connection_hdl hdl) {
    SPDLOG_INFO("");
}

void WebSocketBase::on_http(websocketpp::connection_hdl hdl) {
    SPDLOG_INFO("");
}

bool WebSocketBase::on_validate(websocketpp::connection_hdl hdl) {
    SPDLOG_INFO("");
    return true;
}

void WebSocketBase::enqueue_message(const std::string& content) {
    std::lock_guard<std::mutex> lock(m_message_queue_mutex);

    if (m_pending_messages.size() >= MAX_QUEUE_SIZE) {
        SPDLOG_ERROR("Message queue full ({}), dropping oldest message", MAX_QUEUE_SIZE);
        m_pending_messages.pop_front();
    }

    m_pending_messages.emplace_back(content, MyUTC().Epoch19());
    SPDLOG_INFO("Message queued, queue size: {}", m_pending_messages.size());
}

void WebSocketBase::flush_pending_messages() {
    bool should_reconnect = false;

    {  // Explicit scope for lock - make lock lifetime clear
        std::lock_guard<std::mutex> lock(m_message_queue_mutex);

        if (m_pending_messages.empty()) {
            return;
        }

        SPDLOG_INFO("Flushing {} pending messages", m_pending_messages.size());

        size_t sent_count = 0;
        size_t failed_count = 0;
        size_t expired_count = 0;
        const int64_t current_time = MyUTC().Epoch19();

        while (!m_pending_messages.empty()) {
            const MessageWithTimestamp& msg_with_ts = m_pending_messages.front();

            // 检查消息是否超时
            const int64_t message_age_ns = current_time - msg_with_ts.timestamp_ns;
            if (message_age_ns > MESSAGE_TIMEOUT_NS) {
                SPDLOG_WARN("Dropping expired message (age: {} seconds), content: {}",
                           message_age_ns / zrt::kGiga, msg_with_ts.content);
                expired_count++;
                m_pending_messages.pop_front();
                continue;
            }

            websocketpp::lib::error_code ec {};
            m_client.send(m_hdl, msg_with_ts.content, websocketpp::frame::opcode::text, ec);

            if (ec) {
                SPDLOG_ERROR("Failed to send queued message: {}", ec.message());
                failed_count++;
                should_reconnect = true;
                // Keep failed messages in queue for next retry
                break;
            }

            sent_count++;
            m_pending_messages.pop_front();
        }

        SPDLOG_INFO("Flushed {} messages successfully, {} expired, {} failed, {} remaining in queue",
                    sent_count, expired_count, failed_count, m_pending_messages.size());
    }  // Lock released here

    // Trigger reconnect after lock is released
    if (should_reconnect) {
        // Use get_io_service().post() to trigger close asynchronously
        m_client.get_io_service().post([this]() {
            SPDLOG_WARN("Flush failed, closing connection to trigger reconnect");
            close_on_timeout();
        });
    }
}

