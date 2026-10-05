//
// gtrade_repl - 独立复制进程
//
// 职责：
// 1. 从共享内存读取 WAL 数据
// 2. 写入本地文件 WAL（持久化）
// 3. 发送到备机（复制）
//
// 独立于主交易进程运行，主进程崩溃后仍可继续发送未确认的数据
//
// 数据流：
//   gtrade (主进程) -> 共享内存 WAL -> gtrade_repl -> 文件 WAL + 备机
//

#include <algorithm>
#include <csignal>
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <boost/asio.hpp>
#include "shm_wal_ring.h"
#include "wal_entry.h"
#include "wal_file.h"
#include "global.h"
#include "logger_config.h"
#include "process_utils.h"
#include "3rd/CLI11.hpp"

namespace {

std::atomic<bool> g_running{true};

void SignalHandler(int sig) {
    const char* signal_name = "UNKNOWN";
    switch (sig) {
        case SIGTERM: signal_name = "SIGTERM"; break;
        case SIGINT:  signal_name = "SIGINT";  break;
        case SIGHUP:  signal_name = "SIGHUP";  break;
        default: break;
    }
    LOG_INFO("Received signal {} ({}), shutting down...", sig, signal_name);
    g_running.store(false);
}

// 复制协议消息类型
enum class ReplicationMsgType : uint16_t {
    kHeartbeat = 1,
    kHeartbeatAck = 2,
    kWalEntry = 3,
    kWalAck = 4,
};

// 复制协议消息头
#pragma pack(push, 1)
struct ReplicationMsgHeader {
    uint32_t magic{0x52455053};  // "REPS"
    uint16_t version{1};
    ReplicationMsgType type;
    uint32_t payload_len;
    uint64_t seq;
    uint64_t timestamp_ns;
};
#pragma pack(pop)
static_assert(sizeof(ReplicationMsgHeader) == 28, "ReplicationMsgHeader should be 28 bytes");

// 备机重连退避：首次失败后等待 200ms，之后指数增长，封顶 5s
// 目的：备机长期不可用时不再每轮循环都发起连接（旧实现无退避，空转烧 CPU）
static constexpr std::chrono::milliseconds kReconnectBackoffMin{200};
static constexpr std::chrono::milliseconds kReconnectBackoffMax{5000};

// 空闲轮询间隔：仅当本轮没有待处理 WAL 条目时休眠
// 无数据时休眠以消除忙等；有数据时立即进入下一轮，不增加复制延迟
static constexpr std::chrono::milliseconds kIdlePollInterval{2};

class ReplicationProcess {
public:
    ReplicationProcess(const std::string& wal_name,
                       const std::string& peer_addr,
                       int peer_port,
                       const std::string& file_wal_dir,
                       bool enable_file_wal)
        : wal_name_(wal_name)
        , peer_addr_(peer_addr)
        , peer_port_(peer_port)
        , file_wal_dir_(file_wal_dir)
        , enable_file_wal_(enable_file_wal)
        , io_context_()
        , socket_(io_context_)
        , connected_(false)
        , last_sent_seq_(0) {
    }

    bool Init() {
        // 打开 WAL 共享内存
        try {
            shm_wal_ = std::make_unique<gtrade::ShmWalRing>(wal_name_);
            if (!shm_wal_->IsValid()) {
                LOG_ERROR("Failed to open shared memory WAL: {}", wal_name_);
                return false;
            }
            LOG_INFO("Opened shared memory WAL: {}, write_pos={}, confirmed_pos={}",
                     wal_name_, shm_wal_->GetWritePos(), shm_wal_->GetConfirmedPos());
        } catch (const std::exception& e) {
            LOG_ERROR("Shared memory WAL open exception: {}", e.what());
            return false;
        }

        // 初始化文件 WAL
        if (enable_file_wal_) {
            try {
                // 创建目录
                std::error_code ec;
                std::filesystem::create_directories(file_wal_dir_, ec);
                if (ec) {
                    LOG_ERROR("Failed to create WAL directory {}: {}", file_wal_dir_, ec.message());
                    return false;
                }

                file_wal_ = std::make_unique<gtrade::WalFileWriter>(file_wal_dir_);
                if (!file_wal_->Init()) {
                    LOG_ERROR("Failed to initialize file WAL: {}", file_wal_dir_);
                    return false;
                }
                LOG_INFO("Initialized file WAL: {}, current_seq={}",
                         file_wal_dir_, file_wal_->GetCurrentSeq());
            } catch (const std::exception& e) {
                LOG_ERROR("File WAL init exception: {}", e.what());
                return false;
            }
        }

        return true;
    }

    /**
     * 复制主循环
     *
     * 每轮依次：
     * 1. 备机未连接且已到重试点时发起一次连接（失败按指数退避，避免忙等重连）
     * 2. 读取并处理共享内存 WAL 中未确认的条目
     * 3. 本轮无数据时休眠 kIdlePollInterval；有数据立即进入下一轮保复制延迟
     */
    void Run() {
        LOG_INFO("ReplicationProcess starting, peer={}:{}", peer_addr_, peer_port_);

        while (g_running.load()) {
            const auto now = std::chrono::steady_clock::now();

            // 尝试连接备机（非阻塞）：next_connect_time_ 默认为 epoch，首轮立即尝试；
            // 失败后按 reconnect_backoff_ 指数退避（200ms 起、5s 封顶），成功后清零
            if (!connected_ && now >= next_connect_time_) {
                if (TryConnect()) {
                    reconnect_backoff_ = kReconnectBackoffMin;
                    next_connect_time_ = {};
                } else {
                    // 仅在退避档位变化时告警，长期不可用按 debug 记录，避免日志刷屏
                    if (reconnect_backoff_ != kReconnectBackoffMax) {
                        LOG_WARN("Connect to standby {}:{} failed, retry in {} ms", peer_addr_, peer_port_,
                                 reconnect_backoff_.count());
                    } else {
                        LOG_DEBUG("Connect to standby {}:{} failed, retry in {} ms", peer_addr_, peer_port_,
                                  reconnect_backoff_.count());
                    }
                    next_connect_time_ = now + reconnect_backoff_;
                    reconnect_backoff_ = std::min(reconnect_backoff_ * 2, kReconnectBackoffMax);
                }
            }

            // 读取并处理 WAL 条目（无论是否连接到备机）
            // 文件 WAL 写入不依赖网络连接
            const size_t processed = ProcessWalEntries();

            // 有数据处理时立即进入下一轮，保证复制延迟最小；
            // 无数据时休眠，替代原先 100µs 的近忙等空转
            if (processed == 0) {
                std::this_thread::sleep_for(kIdlePollInterval);
            }
        }

        // 处理剩余的未确认条目
        ProcessWalEntries();

        if (file_wal_) {
            file_wal_->Sync();
            LOG_INFO("File WAL synced before exit");
        }

        Disconnect();
        LOG_INFO("ReplicationProcess stopped");
    }

private:
    /**
     * 尝试连接备机（非阻塞语义）
     *
     * 一次同步 resolve + connect，失败立即返回，不做重试与等待；
     * 重试节奏由调用方 Run() 按退避时间点控制
     *
     * @return true 表示 TCP 建链成功（connected_ 已置位）
     */
    bool TryConnect() {
        try {
            boost::asio::ip::tcp::resolver resolver(io_context_);
            auto endpoints = resolver.resolve(peer_addr_, std::to_string(peer_port_));

            boost::system::error_code ec;
            boost::asio::connect(socket_, endpoints, ec);

            if (ec) {
                LOG_DEBUG("Connect failed: {}", ec.message());
                return false;
            }

            // 设置 TCP_NODELAY
            socket_.set_option(boost::asio::ip::tcp::no_delay(true));

            connected_ = true;
            LOG_INFO("Connected to standby: {}:{}", peer_addr_, peer_port_);
            return true;
        } catch (const std::exception& e) {
            LOG_DEBUG("Connect exception: {}", e.what());
            return false;
        }
    }

    void Disconnect() {
        if (connected_) {
            boost::system::error_code ec;
            socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            socket_.close(ec);
            connected_ = false;
            LOG_INFO("Disconnected from standby");
        }
    }

    /**
     * 处理 WAL 条目
     *
     * 1. 从共享内存读取未确认的条目
     * 2. 写入本地文件 WAL（持久化）
     * 3. 发送到备机（复制，如果已连接）
     * 4. 确认已处理的条目
     *
     * @return 本次处理的条目数，0 表示当前无待处理数据（供调用方决定是否休眠）
     */
    size_t ProcessWalEntries() {
        // 读取未确认的条目
        std::vector<std::pair<gtrade::WalEntryHeader, std::vector<char>>> entries;
        const size_t count = shm_wal_->ReadUnconfirmed(entries, 100);

        if (count == 0) {
            return 0;
        }

        size_t processed = 0;
        for (const auto& [header, data] : entries) {
            // 1. 写入本地文件 WAL（持久化，优先级最高）
            if (file_wal_) {
                const uint64_t file_seq = file_wal_->Append(
                    header.type, data.data(), header.data_len);
                if (file_seq == 0) {
                    LOG_ERROR("Failed to write to file WAL, seq={}", header.seq);
                    // 文件写入失败仍继续，确保数据至少发送到备机
                }
            }

            // 2. 发送到备机（复制）
            if (connected_) {
                if (!SendWalEntry(header, data.data(), data.size())) {
                    LOG_WARN("Failed to send WAL entry to standby, seq={}", header.seq);
                    Disconnect();
                    // 网络发送失败不中断，文件已写入
                }
            }

            processed++;
            last_sent_seq_ = header.seq;
        }

        if (processed > 0) {
            // 刷新文件 WAL（确保持久化）
            if (file_wal_) {
                file_wal_->Sync();
            }

            // 确认已处理的条目
            shm_wal_->ConfirmBatch(processed);
            LOG_DEBUG("Processed {} WAL entries, last_seq={}", processed, last_sent_seq_);
        }

        return processed;
    }

    bool SendWalEntry(const gtrade::WalEntryHeader& wal_header,
                      const void* data, size_t data_len) {
        // 构造复制消息
        ReplicationMsgHeader msg_header{};
        msg_header.type = ReplicationMsgType::kWalEntry;
        msg_header.payload_len = static_cast<uint32_t>(sizeof(wal_header) + data_len);
        msg_header.seq = wal_header.seq;
        msg_header.timestamp_ns = wal_header.timestamp_ns;

        try {
            // 发送消息头
            boost::asio::write(socket_,
                boost::asio::buffer(&msg_header, sizeof(msg_header)));

            // 发送 WAL 头
            boost::asio::write(socket_,
                boost::asio::buffer(&wal_header, sizeof(wal_header)));

            // 发送数据
            if (data_len > 0) {
                boost::asio::write(socket_,
                    boost::asio::buffer(data, data_len));
            }

            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("Send failed: {}", e.what());
            return false;
        }
    }

    std::string wal_name_;
    std::string peer_addr_;
    int peer_port_;
    std::string file_wal_dir_;
    bool enable_file_wal_;

    std::unique_ptr<gtrade::ShmWalRing> shm_wal_;
    std::unique_ptr<gtrade::WalFileWriter> file_wal_;

    boost::asio::io_context io_context_;
    boost::asio::ip::tcp::socket socket_;
    bool connected_;
    uint64_t last_sent_seq_;

    // 重连退避状态：当前退避间隔 + 下次允许发起重连的时间点
    // next_connect_time_ 默认 epoch，表示首轮循环立即尝试连接
    std::chrono::milliseconds reconnect_backoff_{kReconnectBackoffMin};
    std::chrono::steady_clock::time_point next_connect_time_{};
};

void SetupSignalHandlers() {
    // 处理 SIGTERM (kill 默认信号, systemd 停止服务时使用)
    if (std::signal(SIGTERM, SignalHandler) == SIG_ERR) {
        LOG_ERROR("无法设置 SIGTERM 信号处理器");
    }

    // 处理 SIGINT (Ctrl+C)
    if (std::signal(SIGINT, SignalHandler) == SIG_ERR) {
        LOG_ERROR("无法设置 SIGINT 信号处理器");
    }

    // 处理 SIGHUP (终端关闭)
    if (std::signal(SIGHUP, SignalHandler) == SIG_ERR) {
        LOG_ERROR("无法设置 SIGHUP 信号处理器");
    }

    // 忽略 SIGPIPE (避免网络连接断开时崩溃)
    if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        LOG_ERROR("无法忽略 SIGPIPE 信号");
    }
}

}  // namespace

int main(const int argc, char* argv[]) {
    // 默认 WAL 目录为当前目录下的 wal/
    const std::string default_wal_dir = "./wal";

    // CLI11 命令行参数解析
    CLI::App app{"gtrade_repl - WAL Replication Process"};

    std::string wal_name {};
    std::string peer_addr {};
    int peer_port {};
    std::string file_wal_dir {};
    bool no_file_wal {};
    std::string log_level {};
    std::string log_file {};

    app.add_option("--wal", wal_name, "WAL shared memory name")
        ->default_val(gtrade::kOrderWalShmName);
    app.add_option("--peer", peer_addr, "Standby address")
        ->default_val("127.0.0.1");
    app.add_option("--port", peer_port, "Standby port")
        ->default_val(9999);
    app.add_option("--file-wal", file_wal_dir, "File WAL directory")
        ->default_val(default_wal_dir);
    app.add_flag("--no-file-wal", no_file_wal, "Disable file WAL writing");
    app.add_option("--log-level", log_level, "Log level (trace/debug/info/warn/error)")
        ->default_val("info");
    app.add_option("--log-file", log_file, "Log file path")
        ->default_val("log/gtrade_repl/gtrade_repl.log");

    CLI11_PARSE(app, argc, argv);

    // 设置进程名（必须在 CLI11 解析后，因为会清空 argv）
    SetParamShow("gtrade_repl", argc, argv);

    // 初始化日志
    zrt::logger_config logger_config{};
    logger_config.m_log_file = log_file;
    logger_config.m_async = true;
    logger_config.m_show_level = log_level;
    logger_config.m_log_level = log_level;
    logger_config.m_max_files = 10;
    zrt::create_logger2(logger_config);

    LOG_INFO("========================================");
    LOG_INFO("  gtrade_repl starting...");
    LOG_INFO("========================================");
    LOG_INFO("  Shared Memory WAL: {}", wal_name);
    LOG_INFO("  File WAL: {} ({})", file_wal_dir, no_file_wal ? "disabled" : "enabled");
    LOG_INFO("  Standby: {}:{}", peer_addr, peer_port);
    LOG_INFO("  Log Level: {}", log_level);
    LOG_INFO("  Log File: {}", log_file);
    LOG_INFO("========================================");

    // 设置信号处理
    SetupSignalHandlers();

    // 创建并运行复制进程
    ReplicationProcess repl(wal_name, peer_addr, peer_port, file_wal_dir, !no_file_wal);
    if (!repl.Init()) {
        LOG_ERROR("Failed to initialize replication process");
        return 1;
    }

    repl.Run();

    LOG_INFO("gtrade_repl exited");
    return 0;
}
