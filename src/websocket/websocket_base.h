
#pragma once

#include <zlib.h>
#include <functional>
#include <deque>
#include <mutex>
//#include <gzDecompress.h>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/logger/stub.hpp>
#include <websocketpp/common/thread.hpp>
#include "zrtools/io_pool_v2/io_pool.h"
#include "zrtools/io_pool_v2/io_service.h"
#include "zrtools/zrt_time.h"
#include "type_define.h"

typedef websocketpp::config::asio_tls_client::message_type::ptr message_ptr;
typedef websocketpp::lib::shared_ptr<boost::asio::ssl::context> context_ptr;
typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
typedef websocketpp::lib::lock_guard<websocketpp::lib::mutex> scoped_lock;
typedef client::connection_ptr connection_ptr;

// 带时间戳的消息结构体
struct MessageWithTimestamp {
    std::string content;
    int64_t timestamp_ns;  // 纳秒时间戳

    MessageWithTimestamp(const std::string& msg, int64_t ts)
        : content(msg), timestamp_ns(ts) {}
};

class WebSocketBase {
public:
    explicit WebSocketBase(const std::string& uri, const std::string& proxy);
    virtual ~WebSocketBase() noexcept;

    context_ptr on_tls_init(websocketpp::connection_hdl);
    void connect();
    void run();
    virtual void on_open_impl() {}
    virtual void on_close_impl() {}
    virtual void on_fail_impl() {}
    virtual void on_reconnected_impl() {}  // Called after successful reconnection to sync data
    virtual void on_message(websocketpp::connection_hdl, client::message_ptr msg) = 0;
    void set_timer();
    void ResetTimer();
    void close_on_timeout();
    void on_open(websocketpp::connection_hdl);
    void on_close(websocketpp::connection_hdl);
    void on_fail(websocketpp::connection_hdl);
    bool on_timer(websocketpp::lib::error_code const &);
    bool on_ping(websocketpp::connection_hdl, const std::string& payload);
    void on_pong(websocketpp::connection_hdl, const std::string& payload);
    void on_pong_timeout(websocketpp::connection_hdl, const std::string& payload);
    void on_interrupt(websocketpp::connection_hdl);
    void on_http(websocketpp::connection_hdl);
    bool on_validate(websocketpp::connection_hdl);
    void Send(const std::string& content) {
        // Check if connection is open before sending
        if (!m_open || !m_hdl.lock()) {
            SPDLOG_WARN("Connection not ready, queuing message: {}", content);
            enqueue_message(content);
            return;
        }

        websocketpp::lib::error_code ec {};
        m_client.send(m_hdl, content, websocketpp::frame::opcode::text, ec);
        if (ec) {
            SPDLOG_ERROR("Send Error: {}, queuing message and triggering reconnect", ec.message());
            // Send failed, queue the message and close connection to trigger reconnect
            enqueue_message(content);
            // Use async post to avoid potential reentrancy issues
            m_client.get_io_service().post([this]() {
                close_on_timeout();
            });
        }
    }
    bool IsOpen() const noexcept { return m_open; }
    void SetLastMsgRecvTime(const int64_t epoch19) noexcept {
        m_last_msg_recv_time = epoch19;
    }
protected:
    // 只能子类调用, 所以调用的时候m_uri不会被析构(谁异步, 谁负责拷贝)
    const std::string& RefUri() const noexcept { return m_uri; }
private:
    void schedule_reconnect();
    void reconnect();
    void enqueue_message(const std::string& content);
    void flush_pending_messages();

    client m_client {};
    const std::string m_uri {};
    const std::string m_proxy {};
    websocketpp::connection_hdl m_hdl {};
    std::atomic<bool> m_open {};
    std::atomic<bool> m_is_reconnecting {};
    int m_reconnect_interval = 0;
    std::chrono::high_resolution_clock::time_point m_tls_init {};
    websocketpp::lib::thread m_asio_thread {};
    client::timer_ptr m_ping_timer {};
    int64_t m_last_msg_recv_time = {};

    // Message queue for reliable delivery
    std::deque<MessageWithTimestamp> m_pending_messages {};
    std::mutex m_message_queue_mutex {};
    std::mutex m_timer_mutex {};  // Protect timer lifecycle
    static constexpr size_t MAX_QUEUE_SIZE = 1000;
    // 10分钟以上的消息就不再重发
    static constexpr int64_t MESSAGE_TIMEOUT_NS = 600 * zrt::kGiga;
};

// todo: 需要支持logger写到spdlog中
class spdlog_logger : public websocketpp::log::stub {
public:
    spdlog_logger()
    {}

    void write(websocketpp::log::level channel, std::string const& msg) {
        if (channel & websocketpp::log::alevel::all) {
            SPDLOG_INFO("{}", msg);
        } else if (channel & websocketpp::log::elevel::all) {
            SPDLOG_ERROR("{}", msg);
        }
    }
};