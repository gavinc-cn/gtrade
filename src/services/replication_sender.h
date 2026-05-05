//
// Created by Claude Code
// 备机同步发送服务
//
// 职责:
// - 读取共享内存中的 WAL 条目和策略检查点
// - 通过 TCP 长连接发送到备机
// - 支持同步等待 ACK 确认（用于关键操作如报单）
//

#pragma once

#include <atomic>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <boost/asio.hpp>
#include "spdlog/spdlog.h"
#include "global.h"
#include "shm_wal_ring.h"
#include "wal_entry.h"
#include "strategy_checkpoint.h"

namespace gtrade {

/**
 * 复制协议消息类型
 */
enum class ReplicationMsgType : uint16_t {
    kHeartbeat = 1,      // 心跳
    kHeartbeatAck = 2,   // 心跳响应
    kWalEntry = 3,       // WAL 条目
    kWalAck = 4,         // WAL 确认
    kCheckpoint = 5,     // 策略检查点
    kCheckpointAck = 6,  // 检查点确认
    kSyncRequest = 7,    // 同步请求
    kSyncResponse = 8,   // 同步响应
};

/**
 * 复制协议消息头
 */
struct ReplicationMsgHeader {
    uint32_t magic {0x52455053};  // "REPS"
    uint16_t version {1};
    ReplicationMsgType type;
    uint32_t payload_len;
    uint64_t seq;
    uint64_t timestamp_ns;
};
static_assert(sizeof(ReplicationMsgHeader) == 24, "ReplicationMsgHeader should be 24 bytes");

/**
 * 备机同步发送服务
 */
class ReplicationSender {
public:
    ReplicationSender(const HAConfig& config, ShmWalRing* wal)
        : config_(config)
        , wal_(wal)
        , io_context_()
        , socket_(io_context_)
        , running_(false)
        , connected_(false)
        , last_sent_seq_(0)
        , last_acked_seq_(0) {
    }

    ~ReplicationSender() {
        Stop();
    }

    // 禁止拷贝
    ReplicationSender(const ReplicationSender&) = delete;
    ReplicationSender& operator=(const ReplicationSender&) = delete;

    /**
     * 启动发送服务
     */
    bool Start() {
        if (running_.load()) {
            SPDLOG_WARN("ReplicationSender already running");
            return false;
        }

        running_.store(true);

        // 启动发送线程
        sender_thread_ = std::thread([this]() { SenderLoop(); });

        // 启动心跳线程
        heartbeat_thread_ = std::thread([this]() { HeartbeatLoop(); });

        // 启动接收线程（用于接收 ACK）
        receiver_thread_ = std::thread([this]() { ReceiverLoop(); });

        SPDLOG_INFO("ReplicationSender started");
        return true;
    }

    /**
     * 停止发送服务
     */
    void Stop() {
        if (!running_.load()) {
            return;
        }

        running_.store(false);

        // 关闭连接
        boost::system::error_code ec;
        socket_.close(ec);
        io_context_.stop();

        // 等待线程退出
        if (sender_thread_.joinable()) {
            sender_thread_.join();
        }
        if (heartbeat_thread_.joinable()) {
            heartbeat_thread_.join();
        }
        if (receiver_thread_.joinable()) {
            receiver_thread_.join();
        }

        SPDLOG_INFO("ReplicationSender stopped");
    }

    /**
     * 同步等待指定序号的 ACK
     *
     * @param seq 要等待的序号
     * @param timeout_ms 超时时间（毫秒）
     * @return true 如果收到 ACK，false 如果超时
     */
    bool WaitForAck(uint64_t seq, int timeout_ms) {
        if (!config_.sync_order_send) {
            return true;  // 异步模式，不等待
        }

        std::unique_lock<std::mutex> lock(ack_mutex_);
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);

        while (last_acked_seq_ < seq) {
            if (ack_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                SPDLOG_WARN("WaitForAck timeout: seq={}, last_acked={}", seq, last_acked_seq_);
                return false;
            }
        }
        return true;
    }

    /**
     * 获取连接状态
     */
    bool IsConnected() const { return connected_.load(); }

    /**
     * 获取最后发送的序号
     */
    uint64_t GetLastSentSeq() const { return last_sent_seq_.load(); }

    /**
     * 获取最后确认的序号
     */
    uint64_t GetLastAckedSeq() const { return last_acked_seq_.load(); }

private:
    /**
     * 连接到备机
     */
    bool Connect() {
        if (connected_.load()) {
            return true;
        }

        try {
            // 解析地址
            const size_t colon_pos = config_.peer_addr.find(':');
            if (colon_pos == std::string::npos) {
                SPDLOG_ERROR("Invalid peer_addr format: {}", config_.peer_addr);
                return false;
            }

            const std::string host = config_.peer_addr.substr(0, colon_pos);
            const std::string port = config_.peer_addr.substr(colon_pos + 1);

            boost::asio::ip::tcp::resolver resolver(io_context_);
            auto endpoints = resolver.resolve(host, port);

            boost::asio::connect(socket_, endpoints);

            // 设置 TCP_NODELAY
            socket_.set_option(boost::asio::ip::tcp::no_delay(true));

            connected_.store(true);
            SPDLOG_INFO("Connected to standby: {}", config_.peer_addr);
            return true;

        } catch (const std::exception& e) {
            SPDLOG_WARN("Failed to connect to standby: {}", e.what());
            connected_.store(false);
            return false;
        }
    }

    /**
     * 发送循环
     */
    void SenderLoop() {
        SPDLOG_INFO("SenderLoop started");

        while (running_.load()) {
            // 确保连接
            if (!connected_.load()) {
                if (!Connect()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
            }

            // 发送 WAL 条目
            if (wal_ && wal_->IsValid()) {
                SendWalEntries();
            }

            // 短暂休眠避免 CPU 空转
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        SPDLOG_INFO("SenderLoop stopped");
    }

    /**
     * 发送 WAL 条目
     */
    void SendWalEntries() {
        // 批量读取未确认的 WAL 条目
        std::vector<std::pair<WalEntryHeader, std::vector<char>>> entries;
        const size_t count = wal_->ReadUnconfirmed(entries, 100);

        for (const auto& [header, data] : entries) {
            // 跳过已发送的条目
            if (header.seq <= last_sent_seq_.load()) {
                continue;
            }

            // 构建消息
            ReplicationMsgHeader msg_header {};
            msg_header.type = ReplicationMsgType::kWalEntry;
            msg_header.payload_len = sizeof(WalEntryHeader) + data.size();
            msg_header.seq = header.seq;
            msg_header.timestamp_ns = header.timestamp_ns;

            // 发送消息头
            if (!SendData(&msg_header, sizeof(msg_header))) {
                HandleDisconnect();
                return;
            }

            // 发送 WAL 头
            if (!SendData(&header, sizeof(header))) {
                HandleDisconnect();
                return;
            }

            // 发送数据
            if (!data.empty() && !SendData(data.data(), data.size())) {
                HandleDisconnect();
                return;
            }

            last_sent_seq_.store(header.seq);
            SPDLOG_DEBUG("Sent WAL entry: seq={}, type={}",
                         header.seq, GetWalEntryTypeName(header.type));
        }
    }

    /**
     * 心跳循环
     */
    void HeartbeatLoop() {
        SPDLOG_INFO("HeartbeatLoop started");

        while (running_.load()) {
            if (connected_.load()) {
                SendHeartbeat();
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config_.heartbeat_interval_ms));
        }

        SPDLOG_INFO("HeartbeatLoop stopped");
    }

    /**
     * 发送心跳
     */
    void SendHeartbeat() {
        ReplicationMsgHeader msg {};
        msg.type = ReplicationMsgType::kHeartbeat;
        msg.payload_len = 0;
        msg.seq = 0;
        msg.timestamp_ns = GetNanoTimestamp();

        if (!SendData(&msg, sizeof(msg))) {
            HandleDisconnect();
        }
    }

    /**
     * 接收循环（处理 ACK）
     */
    void ReceiverLoop() {
        SPDLOG_INFO("ReceiverLoop started");

        while (running_.load()) {
            if (!connected_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            ReplicationMsgHeader msg {};
            if (!ReceiveData(&msg, sizeof(msg))) {
                HandleDisconnect();
                continue;
            }

            // 验证魔数
            if (msg.magic != 0x52455053) {
                SPDLOG_ERROR("Invalid message magic: {:#x}", msg.magic);
                HandleDisconnect();
                continue;
            }

            switch (msg.type) {
                case ReplicationMsgType::kHeartbeatAck:
                    SPDLOG_DEBUG("Received heartbeat ACK");
                    break;

                case ReplicationMsgType::kWalAck: {
                    std::unique_lock<std::mutex> lock(ack_mutex_);
                    if (msg.seq > last_acked_seq_.load()) {
                        last_acked_seq_.store(msg.seq);
                        ack_cv_.notify_all();
                    }
                    SPDLOG_DEBUG("Received WAL ACK: seq={}", msg.seq);
                    break;
                }

                default:
                    SPDLOG_WARN("Unknown message type: {}", static_cast<int>(msg.type));
                    break;
            }
        }

        SPDLOG_INFO("ReceiverLoop stopped");
    }

    /**
     * 发送数据
     */
    bool SendData(const void* data, size_t len) {
        try {
            boost::asio::write(socket_,
                               boost::asio::buffer(data, len));
            return true;
        } catch (const std::exception& e) {
            SPDLOG_WARN("SendData failed: {}", e.what());
            return false;
        }
    }

    /**
     * 接收数据
     */
    bool ReceiveData(void* data, size_t len) {
        try {
            boost::asio::read(socket_,
                              boost::asio::buffer(data, len));
            return true;
        } catch (const std::exception& e) {
            SPDLOG_WARN("ReceiveData failed: {}", e.what());
            return false;
        }
    }

    /**
     * 处理断开连接
     */
    void HandleDisconnect() {
        if (connected_.load()) {
            SPDLOG_WARN("Disconnected from standby");
            connected_.store(false);
            boost::system::error_code ec;
            socket_.close(ec);
        }
    }

    static uint64_t GetNanoTimestamp() {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
               static_cast<uint64_t>(ts.tv_nsec);
    }

    const HAConfig& config_;
    ShmWalRing* wal_;

    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::socket socket_;

    std::atomic<bool> running_;
    std::atomic<bool> connected_;

    std::atomic<uint64_t> last_sent_seq_;
    std::atomic<uint64_t> last_acked_seq_;

    std::mutex ack_mutex_;
    std::condition_variable ack_cv_;

    std::thread sender_thread_;
    std::thread heartbeat_thread_;
    std::thread receiver_thread_;
};

}  // namespace gtrade
