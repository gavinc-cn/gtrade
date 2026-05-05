//
// Created by dell on 2025/12/5.
// C++ WebSocket client for OKX API
//

#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/common/thread.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <chrono>

using json = nlohmann::json;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

// Type definitions
typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
typedef websocketpp::lib::shared_ptr<boost::asio::ssl::context> context_ptr;
typedef websocketpp::lib::lock_guard<websocketpp::lib::mutex> scoped_lock;
typedef client::connection_ptr connection_ptr;

/**
 * OKX WebSocket Client
 * Connects to OKX WebSocket API and subscribes to market data
 */
class okx_ws_client {
public:
    okx_ws_client(const std::string& proxy_host, int proxy_port)
        : m_open(false), m_done(false), m_proxy_host(proxy_host), m_proxy_port(proxy_port) {

        // 设置日志级别
        m_client.set_access_channels(websocketpp::log::alevel::all);
        m_client.set_error_channels(websocketpp::log::elevel::all);

        // 初始化ASIO
        m_client.init_asio();

        // 设置回调函数
        m_client.set_tls_init_handler(bind(&okx_ws_client::on_tls_init, this, _1));
        m_client.set_open_handler(bind(&okx_ws_client::on_open, this, _1));
        m_client.set_message_handler(bind(&okx_ws_client::on_message, this, _1, _2));
        m_client.set_close_handler(bind(&okx_ws_client::on_close, this, _1));
        m_client.set_fail_handler(bind(&okx_ws_client::on_fail, this, _1));
    }

    /**
     * TLS初始化处理器
     */
    context_ptr on_tls_init(websocketpp::connection_hdl) {
        context_ptr ctx = websocketpp::lib::make_shared<boost::asio::ssl::context>(
            boost::asio::ssl::context::sslv23);

        try {
            ctx->set_options(
                boost::asio::ssl::context::default_workarounds |
                boost::asio::ssl::context::no_sslv2 |
                boost::asio::ssl::context::no_sslv3 |
                boost::asio::ssl::context::single_dh_use
            );
            // 不验证证书 (类似Python的ssl._create_unverified_context())
            ctx->set_verify_mode(boost::asio::ssl::verify_none);
        } catch (std::exception& e) {
            std::cerr << "TLS初始化错误: " << e.what() << std::endl;
        }
        return ctx;
    }

    /**
     * 连接建立时的处理
     */
    void on_open(websocketpp::connection_hdl hdl) {
        std::cout << "WebSocket 连接已建立" << std::endl;

        scoped_lock guard(m_lock);
        m_open = true;
        m_hdl = hdl;

        // 发送订阅消息
        send_subscribe();
    }

    /**
     * 接收消息处理器
     */
    void on_message(websocketpp::connection_hdl, client::message_ptr msg) {
        try {
            std::string payload = msg->get_payload();

            // 解析JSON
            auto data = json::parse(payload);

            // 美化输出JSON
            std::cout << "收到消息: " << data.dump(2) << std::endl;
        } catch (json::parse_error& e) {
            std::cerr << "JSON解析错误: " << e.what() << std::endl;
            std::cout << "原始消息: " << msg->get_payload() << std::endl;
        } catch (std::exception& e) {
            std::cerr << "消息处理错误: " << e.what() << std::endl;
        }
    }

    /**
     * 连接关闭处理器
     */
    void on_close(websocketpp::connection_hdl hdl) {
        connection_ptr con = m_client.get_con_from_hdl(hdl);
        std::cout << "连接已关闭 - 状态码: " << con->get_remote_close_code()
                  << ", 消息: " << con->get_remote_close_reason() << std::endl;

        scoped_lock guard(m_lock);
        m_done = true;
    }

    /**
     * 连接失败处理器
     */
    void on_fail(websocketpp::connection_hdl hdl) {
        connection_ptr con = m_client.get_con_from_hdl(hdl);
        std::cerr << "发生错误: " << con->get_ec().message() << std::endl;

        scoped_lock guard(m_lock);
        m_done = true;
    }

    /**
     * 发送订阅请求
     */
    void send_subscribe() {
        // 构建订阅消息
        json subscribe_msg = {
            {"op", "subscribe"},
            {"args", json::array({
                {
                    {"channel", "bbo-tbt"},
                    {"instId", "BTC-USDT"}
                }
            })}
        };

        std::string msg_str = subscribe_msg.dump();
        std::cout << "发送订阅请求: " << msg_str << std::endl;

        websocketpp::lib::error_code ec;
        m_client.send(m_hdl, msg_str, websocketpp::frame::opcode::text, ec);

        if (ec) {
            std::cerr << "发送错误: " << ec.message() << std::endl;
        }
    }

    /**
     * 运行客户端
     */
    void run(const std::string& uri) {
        websocketpp::lib::error_code ec;

        std::cout << "正在通过代理 " << m_proxy_host << ":" << m_proxy_port
                  << " 连接到 " << uri << "..." << std::endl;

        // 获取连接
        client::connection_ptr con = m_client.get_connection(uri, ec);

        if (ec) {
            std::cerr << "连接创建失败: " << ec.message() << std::endl;
            return;
        }

        // 设置HTTP代理 (格式: http://host:port)
        std::string proxy_uri = "http://" + m_proxy_host + ":" + std::to_string(m_proxy_port);
        con->set_proxy(proxy_uri);

        // 保存连接句柄
        m_hdl = con->get_handle();

        // 连接
        m_client.connect(con);

        // 运行事件循环
        m_client.run();
    }

    /**
     * 停止客户端
     */
    void stop() {
        websocketpp::lib::error_code ec;
        m_client.close(m_hdl, websocketpp::close::status::normal, "客户端关闭", ec);

        if (ec) {
            std::cerr << "关闭连接出错: " << ec.message() << std::endl;
        }
    }

private:
    client m_client;
    websocketpp::connection_hdl m_hdl;
    websocketpp::lib::mutex m_lock;
    bool m_open;
    bool m_done;
    std::string m_proxy_host;
    int m_proxy_port;
};

int main() {
    try {
        // 代理配置
        const std::string PROXY_HOST = "192.0.2.10";
        const int PROXY_PORT = 10808;

        // WebSocket URL
        // 实盘: wss://ws.okx.com:8443/ws/v5/public
        // 模拟盘: wss://wspap.okx.com:8443/ws/v5/public
        const std::string WS_URL = "wss://wspap.okx.com:8443/ws/v5/public";

        // 创建客户端
        okx_ws_client client(PROXY_HOST, PROXY_PORT);

        // 运行客户端（阻塞）
        client.run(WS_URL);

    } catch (std::exception& e) {
        std::cerr << "异常: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
