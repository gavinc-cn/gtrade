//
// Created by Claude Code
// 备机同步接收服务
//
// 职责:
// - 接收主机发送的 WAL 条目和策略检查点
// - 应用到本地状态（OrderManager, 策略检查点）
// - 维护热备状态
// - 发送 ACK 确认
//

#pragma once

#include <atomic>
#include <string>
#include <memory>
#include <thread>
#include <vector>
#include <boost/asio.hpp>
#include "spdlog/spdlog.h"
#include "global.h"
#include "shm_wal_ring.h"
#include "wal_entry.h"
#include "db_structures.h"
#include "replication_sender.h"  // For ReplicationMsgHeader, ReplicationMsgType

namespace gtrade {

// 前向声明
class OrderManager;

/**
 * 备机同步接收服务
 */
class ReplicationReceiver {
public:
    ReplicationReceiver(const HAConfig& config, OrderManager* order_manager)
        : config_(config)
        , order_manager_(order_manager)
        , io_context_()
        , acceptor_(io_context_)
        , running_(false)
        , last_received_seq_(0)
        , total_received_entries_(0) {
    }

    ~ReplicationReceiver() {
        Stop();
    }

    // 禁止拷贝
    ReplicationReceiver(const ReplicationReceiver&) = delete;
    ReplicationReceiver& operator=(const ReplicationReceiver&) = delete;

    /**
     * 启动接收服务
     */
    bool Start() {
        if (running_.load()) {
            SPDLOG_WARN("ReplicationReceiver already running");
            return false;
        }

        try {
            // 绑定监听端口
            boost::asio::ip::tcp::endpoint endpoint(
                boost::asio::ip::tcp::v4(),
                config_.replication_port);

            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
            acceptor_.bind(endpoint);
            acceptor_.listen();

            running_.store(true);

            // 启动接受连接线程
            acceptor_thread_ = std::thread([this]() { AcceptorLoop(); });

            SPDLOG_INFO("ReplicationReceiver started on port {}",
                        config_.replication_port);
            return true;

        } catch (const std::exception& e) {
            SPDLOG_ERROR("Failed to start ReplicationReceiver: {}", e.what());
            return false;
        }
    }

    /**
     * 停止接收服务
     */
    void Stop() {
        if (!running_.load()) {
            return;
        }

        running_.store(false);

        boost::system::error_code ec;
        acceptor_.close(ec);
        io_context_.stop();

        if (acceptor_thread_.joinable()) {
            acceptor_thread_.join();
        }

        // 等待所有客户端线程退出
        for (auto& t : client_threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        client_threads_.clear();

        SPDLOG_INFO("ReplicationReceiver stopped");
    }

    /**
     * 获取最后接收的序号
     */
    uint64_t GetLastReceivedSeq() const {
        return last_received_seq_.load();
    }

    /**
     * 获取总接收条目数
     */
    uint64_t GetTotalReceivedEntries() const {
        return total_received_entries_.load();
    }

private:
    /**
     * 接受连接循环
     */
    void AcceptorLoop() {
        SPDLOG_INFO("AcceptorLoop started");

        while (running_.load()) {
            try {
                auto socket = std::make_shared<boost::asio::ip::tcp::socket>(io_context_);
                acceptor_.accept(*socket);

                // 设置 TCP_NODELAY
                socket->set_option(boost::asio::ip::tcp::no_delay(true));

                SPDLOG_INFO("Accepted connection from {}",
                            socket->remote_endpoint().address().to_string());

                // 启动客户端处理线程
                client_threads_.emplace_back([this, socket]() {
                    HandleClient(socket);
                });

            } catch (const std::exception& e) {
                if (running_.load()) {
                    SPDLOG_WARN("AcceptorLoop error: {}", e.what());
                }
            }
        }

        SPDLOG_INFO("AcceptorLoop stopped");
    }

    /**
     * 处理客户端连接
     */
    void HandleClient(std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
        SPDLOG_INFO("HandleClient started");

        std::vector<char> buffer(65536);  // 64KB buffer

        while (running_.load()) {
            try {
                // 接收消息头
                ReplicationMsgHeader msg_header {};
                boost::asio::read(*socket,
                                  boost::asio::buffer(&msg_header, sizeof(msg_header)));

                // 验证魔数
                if (msg_header.magic != 0x52455053) {
                    SPDLOG_ERROR("Invalid message magic: {:#x}", msg_header.magic);
                    break;
                }

                switch (msg_header.type) {
                    case ReplicationMsgType::kHeartbeat:
                        HandleHeartbeat(socket, msg_header);
                        break;

                    case ReplicationMsgType::kWalEntry:
                        HandleWalEntry(socket, msg_header, buffer);
                        break;

                    case ReplicationMsgType::kCheckpoint:
                        HandleCheckpoint(socket, msg_header, buffer);
                        break;

                    default:
                        SPDLOG_WARN("Unknown message type: {}",
                                    static_cast<int>(msg_header.type));
                        break;
                }

            } catch (const std::exception& e) {
                if (running_.load()) {
                    SPDLOG_WARN("HandleClient error: {}", e.what());
                }
                break;
            }
        }

        boost::system::error_code ec;
        socket->close(ec);
        SPDLOG_INFO("HandleClient stopped");
    }

    /**
     * 处理心跳
     */
    void HandleHeartbeat(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
                         const ReplicationMsgHeader& msg_header) {
        // 发送心跳响应
        ReplicationMsgHeader ack {};
        ack.type = ReplicationMsgType::kHeartbeatAck;
        ack.payload_len = 0;
        ack.seq = 0;
        ack.timestamp_ns = GetNanoTimestamp();

        boost::asio::write(*socket,
                           boost::asio::buffer(&ack, sizeof(ack)));

        SPDLOG_DEBUG("Heartbeat received and ACK sent");
    }

    /**
     * 处理 WAL 条目
     */
    void HandleWalEntry(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
                        const ReplicationMsgHeader& msg_header,
                        std::vector<char>& buffer) {
        // 接收 WAL 头
        WalEntryHeader wal_header {};
        boost::asio::read(*socket,
                          boost::asio::buffer(&wal_header, sizeof(wal_header)));

        // 接收数据
        const size_t data_len = msg_header.payload_len - sizeof(WalEntryHeader);
        if (data_len > 0) {
            if (buffer.size() < data_len) {
                buffer.resize(data_len);
            }
            boost::asio::read(*socket,
                              boost::asio::buffer(buffer.data(), data_len));
        }

        // 应用 WAL 条目
        ApplyWalEntry(wal_header, buffer.data(), data_len);

        // 更新序号
        if (wal_header.seq > last_received_seq_.load()) {
            last_received_seq_.store(wal_header.seq);
        }
        total_received_entries_.fetch_add(1);

        // 发送 ACK
        ReplicationMsgHeader ack {};
        ack.type = ReplicationMsgType::kWalAck;
        ack.payload_len = 0;
        ack.seq = wal_header.seq;
        ack.timestamp_ns = GetNanoTimestamp();

        boost::asio::write(*socket,
                           boost::asio::buffer(&ack, sizeof(ack)));

        SPDLOG_DEBUG("WAL entry received and ACK sent: seq={}, type={}",
                     wal_header.seq, GetWalEntryTypeName(wal_header.type));
    }

    /**
     * 处理检查点
     */
    void HandleCheckpoint(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
                          const ReplicationMsgHeader& msg_header,
                          std::vector<char>& buffer) {
        // 接收检查点数据
        if (msg_header.payload_len > 0) {
            if (buffer.size() < msg_header.payload_len) {
                buffer.resize(msg_header.payload_len);
            }
            boost::asio::read(*socket,
                              boost::asio::buffer(buffer.data(), msg_header.payload_len));

            // 应用检查点
            if (msg_header.payload_len >= sizeof(StrategyCheckpoint)) {
                const StrategyCheckpoint* cp =
                    reinterpret_cast<const StrategyCheckpoint*>(buffer.data());
                ApplyCheckpoint(*cp);
            }
        }

        // 发送 ACK
        ReplicationMsgHeader ack {};
        ack.type = ReplicationMsgType::kCheckpointAck;
        ack.payload_len = 0;
        ack.seq = msg_header.seq;
        ack.timestamp_ns = GetNanoTimestamp();

        boost::asio::write(*socket,
                           boost::asio::buffer(&ack, sizeof(ack)));

        SPDLOG_DEBUG("Checkpoint received and ACK sent: seq={}", msg_header.seq);
    }

    /**
     * 应用 WAL 条目到本地状态
     */
    void ApplyWalEntry(const WalEntryHeader& header,
                       const void* data, size_t data_len) {
        if (!order_manager_) {
            SPDLOG_WARN("OrderManager not set, skip WAL entry");
            return;
        }

        switch (header.type) {
            case WalEntryType::kOrder: {
                if (data_len >= sizeof(Order)) {
                    const Order* order = reinterpret_cast<const Order*>(data);
                    // 注意：备机只更新内存状态，不写入共享内存和 WAL
                    order_manager_->RecoverOrder(*order);
                    SPDLOG_DEBUG("Applied order: entno={}, status={}",
                                 order->entno, order->status);
                }
                break;
            }

            case WalEntryType::kTrade: {
                if (data_len >= sizeof(Trade)) {
                    const Trade* trade = reinterpret_cast<const Trade*>(data);
                    // TODO: 需要添加 RecoverTrade 方法
                    SPDLOG_DEBUG("Received trade: tdno={}", trade->tdno);
                }
                break;
            }

            case WalEntryType::kPosition: {
                if (data_len >= sizeof(Position)) {
                    const Position* pos = reinterpret_cast<const Position*>(data);
                    SPDLOG_DEBUG("Received position: account={}, inst={}",
                                 pos->account_id, pos->instrument);
                }
                break;
            }

            default:
                SPDLOG_DEBUG("Skip WAL entry type: {}",
                             GetWalEntryTypeName(header.type));
                break;
        }
    }

    /**
     * 应用检查点到本地状态
     */
    void ApplyCheckpoint(const StrategyCheckpoint& cp) {
        // 验证校验和
        if (!cp.ValidateChecksum()) {
            SPDLOG_WARN("Checkpoint checksum validation failed: strategy_id={}",
                        cp.strategy_id);
            return;
        }

        // TODO: 写入到本地策略检查点共享内存
        SPDLOG_DEBUG("Applied checkpoint: strategy_id={}, market_seq={}, order_seq={}",
                     cp.strategy_id, cp.market_seq, cp.order_seq);
    }

    static uint64_t GetNanoTimestamp() {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
               static_cast<uint64_t>(ts.tv_nsec);
    }

    const HAConfig& config_;
    OrderManager* order_manager_;

    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;

    std::atomic<bool> running_;

    std::atomic<uint64_t> last_received_seq_;
    std::atomic<uint64_t> total_received_entries_;

    std::thread acceptor_thread_;
    std::vector<std::thread> client_threads_;
};

}  // namespace gtrade
