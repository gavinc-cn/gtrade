//
// Created by dell on 2025/2/14.
//


#include <iostream>
#include <functional>
#include <future>
#include <csignal>
#include <yaml-cpp/yaml.h>
#include "type_define.h"
#include "strategy_engine.h"
#include "logger_config.h"
#include "compile_check.h"
#include "global.h"
#include "zrtools/io_pool_v2/engine_pool.h"
#include "okx_ws.h"
#include "okx_trade.h"
#include "my_utc.h"
#include "timer_manager.h"
#include "backtest_engine.h"
#include "time_machine.h"
#include "zrtools/io_pool_v2/sync_thread.h"
#include "string_keys.h"
#include "message_server.h"
#include "mysql_gateway.h"
#include "http_gateway.h"
#include "desktop_gateway_grpc_service.h"
#include "3rd/CLI11.hpp"
#include "gtrade_cli.h"
#include "process_utils.h"

namespace {
    // 信号处理函数
    void signal_handler(int signum) {
        const char* signal_name = "UNKNOWN";
        switch (signum) {
            case SIGTERM: signal_name = "SIGTERM"; break;
            case SIGINT:  signal_name = "SIGINT";  break;
            case SIGHUP:  signal_name = "SIGHUP";  break;
            default: break;
        }

        LOG_WARN("========================================");
        LOG_WARN("收到信号 {} ({}), 开始优雅退出...", signum, signal_name);
        LOG_WARN("========================================");

        // 设置退出标志
        GlobalControl::is_running = false;
    }

    // 设置信号处理器
    void setup_signal_handlers() {
        // 处理 SIGTERM (kill 默认信号, systemd 停止服务时使用)
        if (std::signal(SIGTERM, signal_handler) == SIG_ERR) {
            LOG_ERROR("无法设置 SIGTERM 信号处理器");
        } else {
            LOG_INFO("已注册 SIGTERM 信号处理器");
        }

        // 处理 SIGINT (Ctrl+C)
        if (std::signal(SIGINT, signal_handler) == SIG_ERR) {
            LOG_ERROR("无法设置 SIGINT 信号处理器");
        } else {
            LOG_INFO("已注册 SIGINT 信号处理器");
        }

        // 处理 SIGHUP (终端关闭)
        if (std::signal(SIGHUP, signal_handler) == SIG_ERR) {
            LOG_ERROR("无法设置 SIGHUP 信号处理器");
        } else {
            LOG_INFO("已注册 SIGHUP 信号处理器");
        }

        // 忽略 SIGPIPE (避免网络连接断开时崩溃)
        if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
            LOG_ERROR("无法忽略 SIGPIPE 信号");
        } else {
            LOG_INFO("已忽略 SIGPIPE 信号");
        }

        LOG_INFO("信号处理器设置完成");
    }
}


class GTrade {
public:
    void LoadConfig() {
        // 主配置
        YAML::Node main_yml = YAML::LoadFile("config/config.yml");

        zrt::logger_config logger_config {};
        logger_config.m_log_file = main_yml[k_logger][k_log_file].as<std::string>();
        logger_config.m_async = main_yml[k_logger][k_async].as<bool>();
        logger_config.m_show_level = main_yml[k_logger][k_show_level].as<std::string>();
        logger_config.m_log_level = main_yml[k_logger][k_log_level].as<std::string>();
        if constexpr (GlobalConst::IsRealTrading) {
            logger_config.m_max_files = main_yml[k_logger][k_max_files].as<uint>();
        } else {
            logger_config.m_max_files = 99;
        }
        zrt::create_logger2(logger_config);
        if (logger_config.m_async) {
            zrt::create_error_logger(logger_config);
        }

        // LOG_INFO("{}", zrt::to_str(logger_config));

        for (const auto& strat_cfg_path : main_yml[k_strategy_config]) {
            m_gtrade_cfg.strat_cfg_path_vec.push_back(strat_cfg_path.as<std::string>());
        }

        LOG_INFO("strat_cfg_path_vec={}", zrt::to_str(m_gtrade_cfg.strat_cfg_path_vec));

        // 账户配置
        YAML::Node account_yml = YAML::LoadFile(main_yml[k_account_config].as<std::string>());
        for (const auto& account : account_yml) {
            std::string account_id = account.first.as<std::string>();
            m_gtrade_cfg.account_map[account_id].market = account.second[k_market].as<std::string>();
            m_gtrade_cfg.account_map[account_id].key = account.second[k_key].as<std::string>();
            m_gtrade_cfg.account_map[account_id].secret = account.second[k_secret].as<std::string>();
            if (account.second[k_passphrase].IsDefined()) {
                m_gtrade_cfg.account_map[account_id].passphrase = account.second[k_passphrase].as<std::string>();
            }
        }
        // 加载交易所URL配置
        if (main_yml[k_url].IsDefined()) {
            for (const auto& exchange : main_yml[k_url]) {
                const std::string exchange_name = exchange.first.as<std::string>();
                ExchangeUrlConfig url_config {};
                url_config.rest = exchange.second[k_rest].as<std::string>();
                url_config.ws_public = exchange.second[k_ws_public].as<std::string>();
                url_config.ws_private = exchange.second[k_ws_private].as<std::string>();
                m_gtrade_cfg.url_map[exchange_name] = url_config;
                LOG_INFO("loaded url config for exchange={}, rest={}, ws_public={}, ws_private={}",
                    exchange_name, url_config.rest, url_config.ws_public, url_config.ws_private);
            }
        }
        m_gtrade_cfg.query_processor_num = main_yml[k_query_processor_num].as<int>();
        m_gtrade_cfg.http_server_port = main_yml[k_http_server_port].as<int>();
        m_gtrade_cfg.entrust_maintain_days = main_yml[k_entrust_maintain_days].as<int>();
        // 远程策略 ZMQ 接入端口（可选，默认 0 表示不启用）
        m_gtrade_cfg.remote_engine_port = main_yml[k_remote_engine_port].as<int>(0);

        // Desktop Gateway配置 (gRPC)
        if (main_yml[k_desktop_gateway].IsDefined()) {
            m_gtrade_cfg.desktop_gateway_enabled = main_yml[k_desktop_gateway][k_enabled].as<bool>(true);
            m_gtrade_cfg.desktop_gateway_endpoint = main_yml[k_desktop_gateway][k_server_address].as<std::string>("0.0.0.0:50051");
            m_gtrade_cfg.desktop_gateway_ca_cert = main_yml[k_desktop_gateway][k_ca_cert].as<std::string>("");
            m_gtrade_cfg.desktop_gateway_server_cert = main_yml[k_desktop_gateway][k_server_cert].as<std::string>("");
            m_gtrade_cfg.desktop_gateway_server_key = main_yml[k_desktop_gateway][k_server_key].as<std::string>("");
            m_gtrade_cfg.desktop_gateway_jwt_secret = main_yml[k_desktop_gateway][k_jwt_secret].as<std::string>();
            m_gtrade_cfg.desktop_gateway_jwt_expiration = main_yml[k_desktop_gateway][k_jwt_expiration].as<int>(86400);
        }

        // Web登录凭证
        m_gtrade_cfg.user = main_yml[k_user].as<std::string>();
        m_gtrade_cfg.password = main_yml[k_password].as<std::string>();
        if constexpr (GlobalConst::IsBackTest) {
            m_gtrade_cfg.start_date = main_yml["start_date"].as<std::string>();
            m_gtrade_cfg.end_date = main_yml["end_date"].as<std::string>();
            m_gtrade_cfg.backtest_rate = main_yml["backtest_rate"].as<double>();
            m_gtrade_cfg.backtest_interval = main_yml["backtest_interval"].as<int64_t>();
            m_gtrade_cfg.csv_quote_base_dir = main_yml["csv_quote_base_dir"].as<std::string>();
            m_gtrade_cfg.backtest_out_dir = main_yml["backtest_out_dir"].as<std::string>();
            m_gtrade_cfg.fill_mode = main_yml["fill_mode"].as<char>();
        }

        if (main_yml[k_proxy].IsDefined()) {
            m_gtrade_cfg.proxy_config.http = main_yml[k_proxy][k_http].as<std::string>();
            m_gtrade_cfg.proxy_config.socks5 = main_yml[k_proxy][k_socks5].as<std::string>();

            // // 不需要放到account_map中, OkxClient中还在用, 会逐步放弃
            // for (auto& [acc_id, acc] : m_gtrade_cfg.account_map) {
            //     acc.proxy = main_yml[k_proxy]["http"].as<std::string>();
            // }
        }

        // 数据库配置
        YAML::Node db_yml = YAML::LoadFile(main_yml[k_db_config].as<std::string>());
        m_gtrade_cfg.db_config.host = db_yml[k_mysql][k_host].as<std::string>();
        m_gtrade_cfg.db_config.port = db_yml[k_mysql][k_port].as<int>();
        m_gtrade_cfg.db_config.user = db_yml[k_mysql][k_user].as<std::string>();
        m_gtrade_cfg.db_config.password = db_yml[k_mysql][k_pwd].as<std::string>();
        m_gtrade_cfg.db_config.db = db_yml[k_mysql][k_db].as<std::string>();
        
        // Slack配置
        m_gtrade_cfg.slack_config_path = main_yml[k_slack_config].as<std::string>();
        LOG_INFO("slack_config_path={}", m_gtrade_cfg.slack_config_path);
    }

    void Run() {
        setup_signal_handlers();
        LoadConfig();
        LOG_INFO("GlobalConst::IsRealTrading={}", GlobalConst::IsRealTrading);

        zrt::EnginePool& pool = zrt::EnginePool::GetInstance();
        pool.AddNamedEngine<SyncThread>(k_BackTestThread);
        pool.AddNamedEngine<BoostAsioThread>(k_StrategyEngineThread);
        pool.AddNamedEngine<BoostAsioThread>(k_TimerManagerThread);
        pool.AddNamedEngine<BoostAsioThread>(k_MySqlGatewayThread);
        pool.AddNamedEngine<BoostAsioThread>(k_MessageServerThread);
        if constexpr (GlobalConst::IsRealTrading) {
            pool.AddNamedEngine<BoostAsioThread>(k_HttpGatewayThread);
            pool.AddNamedEngine<BoostAsioThread>(k_DesktopGatewayThread);
        }
        pool.AddSharedEngine<BoostAsioThread>(10);

        // 先设置时间
        if constexpr (GlobalConst::IsRealTrading) {
            TimeMachine::GetInstance().Start(200);
        }
        else {
            TimeMachine::GetInstance().SetEpoch(MyUTC(m_gtrade_cfg.start_date, BACKTEST_TIME_FORMAT).Epoch19());
        }

        /*
        服务结构:
        StrategyEngine
            策略, 委托, 成交, 持仓, 资金管理
            StrategyBase
                策略相关操作, 是每个策略的基类
            OkxTrade
                因为每个账号都有一个, 所以放到Engine下面, 负责交易账户(委托)相关的操作, 交易所公用的数据(基础信息等)由StrategyEngine管理
        MySqlGateway
            查询操作可以通过query_server来完成, 因为query_server是并行的, 但写入操作应当通过mysql_gateway, 只有一个线程
        OkxWs
            行情, 每个交易所只有一个
        TimerManager
            实盘模式的定时器
        TimerManagerDummy
            回测模式的定时器
        QueryServer
            查询服务
        MessageServer
            对外通知发送
         */

        // 先构造所有服务
        ServiceMap& m_service_map = ServiceMap::GetInstance();
        m_service_map.emplace(k_StrategyEngine, std::make_unique<StrategyEngine>(m_service_map, m_gtrade_cfg));
        m_service_map.emplace(k_MySqlGateway, std::make_unique<MySqlGateway>(m_service_map, m_gtrade_cfg));

        // 实盘
        if constexpr (GlobalConst::IsRealTrading) {
            m_service_map.emplace(k_OkxQuote, std::make_unique<OkxWs>(m_service_map, m_gtrade_cfg, k_okx));
            m_service_map.emplace(k_OkxDummyQuote, std::make_unique<OkxWs>(m_service_map, m_gtrade_cfg, k_okx_dummy));
            m_service_map.emplace(k_TimerManager, std::make_unique<TimerManager>(m_service_map, m_gtrade_cfg));
            m_service_map.emplace(k_HttpGateway, std::make_unique<HttpGateway>(m_service_map, m_gtrade_cfg));
            if (m_gtrade_cfg.desktop_gateway_enabled) {
                m_service_map.emplace(k_DesktopGateway, std::make_unique<DesktopGatewayGrpcService>(m_service_map, m_gtrade_cfg));
            }
        }
        // 回测
        else {
            BacktestEngine::RegisterServices(m_service_map, m_gtrade_cfg);
        }
        m_service_map.emplace(k_QueryServer, std::make_unique<QueryServer>(m_service_map, m_gtrade_cfg));
        m_service_map.emplace(k_MessageServer, std::make_unique<MessageServer>(m_service_map, m_gtrade_cfg));

        LOG_INFO("current time is: {}", MyUTC().ToFormat());

        // 初始化所有服务, 包括赋值各服务间互相发消息的指针, 注册回调等
        m_service_map.ForEach([](auto& srv) {
            srv->Init();
        });
        // EngineStart之前, 线程还没有创建, 所以在Init里面服务间不能互相Post
        pool.EngineStart();
        m_service_map.ForEach([](auto& srv) {
            srv->Start();
        });

        // 不能放在外面, 因为出了Run() pool会析构
        LOG_INFO("GTrade 主循环开始，PID={}", getpid());
        while(GlobalControl::is_running) {
            sleep(1);
        }

        LOG_INFO("========================================");
        LOG_INFO("开始停止 GTrade...");
        LOG_INFO("========================================");

        // 先停止所有服务（HTTP, gRPC, WebSocket等）
        // 确保外部连接和额外线程在线程池停止之前关闭
        LOG_INFO("正在停止所有服务...");
        m_service_map.StopAll();
        LOG_INFO("所有服务已停止");

        // 等待线程池中所有任务完成并停止线程
        pool.WaitStop();
        LOG_INFO("GTrade 已退出");
    }

private:
    GTradeConfig m_gtrade_cfg {};
};

int main(const int argc, char** argv) {
    std::set_terminate(zrt::safe_terminate_hdl);

    CLI::App app{"GTrade - Quantitative Trading System"};

    // 子命令
    CLI::App* add_cmd = app.add_subcommand("add", "Add a new strategy");
    std::string add_cfg_path;
    add_cmd->add_option("config", add_cfg_path, "Strategy configuration file path")->required();

    CLI::App* delete_cmd = app.add_subcommand("delete", "Delete an existing strategy");
    std::string delete_strat_id;
    delete_cmd->add_option("strategy_id", delete_strat_id, "Strategy ID to delete")->required();

    CLI::App* restart_cmd = app.add_subcommand("restart", "Restart an existing strategy");
    std::string restart_strat_id;
    restart_cmd->add_option("strategy_id", restart_strat_id, "Strategy ID to restart")->required();

    const CLI::App* list_cmd = app.add_subcommand("list", "List all strategies");

    // 系统管理命令
    const CLI::App* snapshot_cmd = app.add_subcommand("snapshot", "Save a snapshot of current state (orders, trades, positions)");
    const CLI::App* wal_stats_cmd = app.add_subcommand("wal-stats", "Get WAL (Write-Ahead Log) statistics");

    // 解析命令行参数
    CLI11_PARSE(app, argc, argv);

    // 如果没有子命令, 正常运行
    if (app.get_subcommands().empty()) {
        // 修改内核进程名
        SetProcessName("gtrade_main");
        // 修改进程列表中显示的argv
        SetParamShow("gtrade", argc, argv);

        GTrade gtrade {};
        gtrade.Run();
    }
    else {
        // 如果有子命令, 进入HTTP客户端模式
        GTradeClient client;  // 使用封装的HTTP客户端类

        if (*add_cmd) {
            return client.AddStrategy(add_cfg_path);
        } else if (*delete_cmd) {
            return client.DeleteStrategy(delete_strat_id);
        } else if (*restart_cmd) {
            return client.RestartStrategy(restart_strat_id);
        } else if (*list_cmd) {
            return client.ListStrategies();
        } else if (*snapshot_cmd) {
            return client.SaveSnapshot();
        } else if (*wal_stats_cmd) {
            return client.GetWalStats();
        }
    }
}
