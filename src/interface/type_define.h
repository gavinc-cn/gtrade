//
// Created by dell on 2025/2/15.
//

#pragma once

#include "zrtools/io_pool_v2/moodycamel_thread.h"
// #include "zrtools/io_pool_v2/lock_deque_thread.h"
#include "zrtools/io_pool_v2/io_service.h"
// #include "zrtools/io_pool_v2/boost_lockfree_thread.h"
#include "zrtools/io_pool_v2/boost_asio_thread.h"
#include "zrtools/io_pool_v2/sync_thread.h"
#include "zrtools/io_pool_v2/i_handler.h"
#include "zrtools/zrt_misc.h"
#include "tbuffer.h"
// #include "str_types.h"
#include "msg_id_dump.h"

class SyncThread;

// 共享指针封装的缓存
using BufPtr = TBufferPtr;
// 异步回调
using Callback = std::function<void(int, BufPtr)>;
// 同步回调（返回值语义）：handler 直接返回响应，promise 兑现由框架负责（OnSyncMessage 内
// set_value），handler 无从接触 promise，结构上不存在"漏兑现导致调用方挂死"。
// 注意：handler 返回 nullptr 会被框架兜底替换为非空空响应（调用方会直接解引用）
using SyncCallback = std::function<BufPtr(int, BufPtr)>;
// 基于boost::asio的异步服务
using BoostAsioSrv = IOService<BufPtr,Callback,SyncCallback,BoostAsioThread>;
// 同步服务
using SyncIoSrv = IOService<BufPtr,Callback,SyncCallback,SyncThread>;
// 只有接口, 没有线程, 需要接入线程池
using MyHandler = IHandler<BufPtr,Callback,SyncCallback>;
// using ServiceMap = std::unordered_map<std::string,std::unique_ptr<MyHandler>>;

inline auto DefaultMsgHandler = [](const int msg_id, const BufPtr buffer) {
    SPDLOG_ERROR("default handler: msg_id={} buf_sz={}", GetEmName_MsgId(msg_id), buffer->GetSize());
};

inline auto DefaultSyncMsgHandler = [](const int msg_id, const BufPtr buffer) {
    SPDLOG_ERROR("default handler: msg_id={} buf_sz={}", GetEmName_MsgId(msg_id), buffer->GetSize());
    return std::make_shared<TBuffer>();
};

#define ZRT_SRV_ADD_HANDLER(msg_id, ...) InstallHandler()(MsgId::msg_id, std::bind(&__VA_ARGS__, this, std::placeholders::_1, std::placeholders::_2))
#define ZRT_SRV_ADD_SYNC_HANDLER(msg_id, ...) InstallSyncHandler()(MsgId::msg_id, std::bind(&__VA_ARGS__, this, std::placeholders::_1, std::placeholders::_2))

#define ZRT_ADD_HANDLER(msg_id, ...) InstallHandler(MsgId::msg_id, std::bind(&__VA_ARGS__, this, std::placeholders::_1, std::placeholders::_2))
#define ZRT_ADD_SYNC_HANDLER(msg_id, ...) InstallSyncHandler(MsgId::msg_id, std::bind(&__VA_ARGS__, this, std::placeholders::_1, std::placeholders::_2))

#define BACKTEST_TIME_FORMAT "%Y%m%d_%H%M"


struct Account {
    std::string id {};
    std::string market {};
    std::string key {};
    std::string secret {};
    std::string passphrase {};
    // std::string proxy {};

    // 交易所专属扩展字段（key/value 形式）。
    // 由 gtrade.cpp 从账户 YAML 中"非通用键"自动收集，避免每接入一个交易所
    // 就改 Account 结构。CTP 用到的键见 src/common/string_keys.h：
    //   broker_id / investor_id / password / td_front / md_front / auth_code / app_id
    std::unordered_map<std::string, std::string> extra {};
};

struct DBConfig {
    std::string host {};
    int port {};
    std::string user {};
    std::string password {};
    std::string db {};
};

struct ProxyConfig {
    std::string http {};
    std::string socks5 {};
};

// 交易所URL配置
struct ExchangeUrlConfig {
    std::string rest {};        // REST API URL
    std::string ws_public {};   // WebSocket公有URL
    std::string ws_private {};  // WebSocket私有URL
};

// DPDK wire2wire 延时探测配置（仅 GTRADE_ENABLE_DPDK_PROBE 下生效，YAML 可选块 dpdk_probe）
// 缺失时全默认、enabled=false，主线行为完全不变。
struct DpdkProbeConfig {
    bool enabled {false};                   // 是否启用 DPDK 行情/交易探测入口
    int port_id {0};                        // DPDK 端口号（devbind 后查得）
    int rx_queue {0};                       // 收包队列（单核用 0）
    int tx_queue {0};                       // 发包队列（单核与 rx 复用 0）
    int pin_cpu {2};                        // busy-poll 收包线程绑核（建议 P 核，避开 CPU0）
    std::string eal_args {"-l 2 -n 4"};     // rte_eal_init 参数（-l 须含 pin_cpu 核）
    std::string dst_mac {};                 // 委托回执目的 MAC（空=广播，host 嗅探用）
};

// 推送出口配置（引擎 → web_server，方案 rev4 §5）
// YAML 可选块 web_push；缺失或 enabled=false 时完全不建连接（主线行为不变）。
struct WebPushConfig {
    bool enabled {false};                       // 是否启用推送出口
    std::string url {"ws://127.0.0.1:46013/ws/engine"};  // web_server 的引擎入口
    std::string secret {};                      // 首帧 hello 的共享密钥（不走 URL，避免进访问日志）
    int ping_ms {10000};                        // 应用层心跳周期；对端超过 2× 周期无帧即重连
    int quote_min_interval_ms {500};            // 盘口推送下限（每个标的独立限频，与 SSE 侧一致）
};

struct GTradeConfig {
    std::vector<std::string> strat_cfg_path_vec {};
    std::unordered_map<std::string,Account> account_map {};
    DBConfig db_config {};
    ProxyConfig proxy_config {};
    // Slack配置文件路径
    std::string slack_config_path {};
    // 交易所URL配置 (key: 交易所名称, 如 "okx", "okx_dummy")
    std::unordered_map<std::string, ExchangeUrlConfig> url_map {};
    // DPDK 延时探测配置（YAML 缺失 dpdk_probe 块则全默认、enabled=false）
    DpdkProbeConfig dpdk_probe {};
    // 推送出口（引擎 → web_server；YAML 缺失 engine_push 块则全默认、enabled=false）
    WebPushConfig engine_push {};
    // 查询服务线程数
    int query_processor_num {};
    int http_server_port {};
    int entrust_maintain_days {};
    // 远程策略 ZMQ 接入端口（0 表示不启用，Runner 通过此端口连接 Engine）
    int remote_engine_port {};

    // Desktop Gateway配置 (gRPC)
    bool desktop_gateway_enabled {};
    std::string desktop_gateway_endpoint {};  // gRPC服务器地址 (e.g., "0.0.0.0:50051")
    std::string desktop_gateway_ca_cert {};   // CA证书路径
    std::string desktop_gateway_server_cert {};  // 服务器证书路径
    std::string desktop_gateway_server_key {};   // 服务器私钥路径
    std::string desktop_gateway_jwt_secret {};
    int desktop_gateway_jwt_expiration {};

    // Web登录凭证
    std::string user {};
    std::string password {};

    // 是否回测模式
    // bool is_backtest {};
    // 回测起始日期UTC(YYYYmmdd-HHMM)
    std::string start_date {};
    // 回测结束日期UTC(YYYYmmdd-HHMM)
    std::string end_date {};
    // 回测速度倍率
    double backtest_rate {};
    int64_t backtest_interval {};
    // csv行情保存路径
    std::string csv_quote_base_dir {};
    // 成交委托输出目录
    std::string backtest_out_dir {};
    // 成交模式
    char fill_mode {};
};
