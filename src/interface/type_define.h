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
// 同步回调
using SyncCallback = std::function<void(int, BufPtr, std::promise<BufPtr>&)>;
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

inline auto DefaultSyncMsgHandler = [](const int msg_id, const BufPtr buffer, std::promise<BufPtr>& ret) {
    SPDLOG_ERROR("default handler: msg_id={} buf_sz={}", GetEmName_MsgId(msg_id), buffer->GetSize());
    ret.set_value(std::make_shared<TBuffer>());
};

#define ZRT_SRV_ADD_HANDLER(msg_id, ...) InstallHandler()(MsgId::msg_id, std::bind(&__VA_ARGS__, this, std::placeholders::_1, std::placeholders::_2))
#define ZRT_SRV_ADD_SYNC_HANDLER(msg_id, ...) InstallSyncHandler()(MsgId::msg_id, std::bind(&__VA_ARGS__, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))

#define ZRT_ADD_HANDLER(msg_id, ...) InstallHandler(MsgId::msg_id, std::bind(&__VA_ARGS__, this, std::placeholders::_1, std::placeholders::_2))
#define ZRT_ADD_SYNC_HANDLER(msg_id, ...) InstallSyncHandler(MsgId::msg_id, std::bind(&__VA_ARGS__, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))

#define BACKTEST_TIME_FORMAT "%Y%m%d_%H%M"


struct Account {
    std::string id {};
    std::string market {};
    std::string key {};
    std::string secret {};
    std::string passphrase {};
    // std::string proxy {};
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

struct GTradeConfig {
    std::vector<std::string> strat_cfg_path_vec {};
    std::unordered_map<std::string,Account> account_map {};
    DBConfig db_config {};
    ProxyConfig proxy_config {};
    // Slack配置文件路径
    std::string slack_config_path {};
    // 交易所URL配置 (key: 交易所名称, 如 "okx", "okx_dummy")
    std::unordered_map<std::string, ExchangeUrlConfig> url_map {};
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
