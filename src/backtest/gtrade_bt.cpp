//
// 回测模式独立入口
// 不依赖 src/gtrade.cpp，只启动回测必要的服务
//

#include <iostream>
#include <csignal>
#include <yaml-cpp/yaml.h>
#include "type_define.h"
#include "backtest_strategy_engine.h"
#include "logger_config.h"
#include "global.h"
#include "zrtools/io_pool_v2/engine_pool.h"
#include "zrtools/io_pool_v2/sync_thread.h"
#include "my_utc.h"
#include "time_machine.h"
#include "string_keys.h"
#include "backtest_engine.h"
#include "process_utils.h"
#include "3rd/CLI11.hpp"

namespace {
    // 信号处理函数
    void signal_handler(const int signum) {
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

        GlobalControl::is_running = false;
    }

    // 设置信号处理器
    void setup_signal_handlers() {
        if (std::signal(SIGTERM, signal_handler) == SIG_ERR) {
            LOG_ERROR("无法设置 SIGTERM 信号处理器");
        } else {
            LOG_INFO("已注册 SIGTERM 信号处理器");
        }

        if (std::signal(SIGINT, signal_handler) == SIG_ERR) {
            LOG_ERROR("无法设置 SIGINT 信号处理器");
        } else {
            LOG_INFO("已注册 SIGINT 信号处理器");
        }

        if (std::signal(SIGHUP, signal_handler) == SIG_ERR) {
            LOG_ERROR("无法设置 SIGHUP 信号处理器");
        } else {
            LOG_INFO("已注册 SIGHUP 信号处理器");
        }

        // 忽略 SIGPIPE（避免网络连接断开时崩溃）
        if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
            LOG_ERROR("无法忽略 SIGPIPE 信号");
        } else {
            LOG_INFO("已忽略 SIGPIPE 信号");
        }

        LOG_INFO("信号处理器设置完成");
    }
}

/**
 * 回测模式应用类。
 *
 * 相比实盘 GTrade 类，精简了以下服务：
 *   - HttpGateway   （无 HTTP 管理接口）
 *   - DesktopGateway（无 gRPC 桌面网关）
 *   - MessageServer  （无 Slack 通知）
 *   - OkxWs          （无真实行情 WebSocket）
 *
 * 只启动：StrategyEngine、MySqlGateway、CsvQuote、
 *         TimerManagerDummy（后两者由 BacktestEngine 注册）、QueryServer
 */
class GTradeBacktest {
public:
    /**
     * 从指定路径加载回测配置文件。
     * @param config_path 配置文件路径，默认 config/backtest_config.yml
     */
    void LoadConfig(const std::string& config_path = "config/backtest_config.yml") {
        YAML::Node yml = YAML::LoadFile(config_path);

        // 日志配置（回测 max_files 固定为 99，避免日志滚动丢失回测记录）
        zrt::logger_config logger_cfg {};
        logger_cfg.m_log_file   = yml[k_logger][k_log_file].as<std::string>();
        logger_cfg.m_async      = yml[k_logger][k_async].as<bool>();
        logger_cfg.m_show_level = yml[k_logger][k_show_level].as<std::string>();
        logger_cfg.m_log_level  = yml[k_logger][k_log_level].as<std::string>();
        logger_cfg.m_max_files  = 99;
        zrt::create_logger2(logger_cfg);
        if (logger_cfg.m_async) {
            zrt::create_error_logger(logger_cfg);
        }

        // 策略配置路径列表
        for (const auto& path : yml[k_strategy_config]) {
            m_cfg.strat_cfg_path_vec.push_back(path.as<std::string>());
        }
        LOG_INFO("strat_cfg_path_vec={}", zrt::to_str(m_cfg.strat_cfg_path_vec));

        // 账户配置（DummyTrade 需要账户 ID 作为 key）
        YAML::Node account_yml = YAML::LoadFile(yml[k_account_config].as<std::string>());
        for (const auto& account : account_yml) {
            const std::string account_id = account.first.as<std::string>();
            m_cfg.account_map[account_id].market = account.second[k_market].as<std::string>();
            m_cfg.account_map[account_id].key    = account.second[k_key].as<std::string>();
            m_cfg.account_map[account_id].secret = account.second[k_secret].as<std::string>();
            if (account.second[k_passphrase].IsDefined()) {
                m_cfg.account_map[account_id].passphrase = account.second[k_passphrase].as<std::string>();
            }
        }

        // 数据库配置
        YAML::Node db_yml = YAML::LoadFile(yml[k_db_config].as<std::string>());
        m_cfg.db_config.host     = db_yml[k_mysql][k_host].as<std::string>();
        m_cfg.db_config.port     = db_yml[k_mysql][k_port].as<int>();
        m_cfg.db_config.user     = db_yml[k_mysql][k_user].as<std::string>();
        m_cfg.db_config.password = db_yml[k_mysql][k_pwd].as<std::string>();
        m_cfg.db_config.db       = db_yml[k_mysql][k_db].as<std::string>();

        // 回测专用参数
        m_cfg.start_date         = yml["start_date"].as<std::string>();
        m_cfg.end_date           = yml["end_date"].as<std::string>();
        m_cfg.backtest_rate      = yml["backtest_rate"].as<double>();
        m_cfg.backtest_interval  = yml["backtest_interval"].as<int64_t>();
        m_cfg.csv_quote_base_dir = yml["csv_quote_base_dir"].as<std::string>();
        m_cfg.backtest_out_dir   = yml["backtest_out_dir"].as<std::string>();
        m_cfg.fill_mode          = yml["fill_mode"].as<char>();

        // query_processor_num 使用合理默认值（回测串行，不需要太多线程）
        m_cfg.query_processor_num = yml["query_processor_num"].as<int>(4);

        LOG_INFO("回测配置加载完成: start={} end={} fill_mode={}",
            m_cfg.start_date, m_cfg.end_date, m_cfg.fill_mode);
    }

    /**
     * 启动回测。
     * 线程池只创建回测需要的命名线程，不创建 HTTP/gRPC 相关线程。
     */
    void Run(const std::string& config_path = "config/backtest_config.yml") {
        setup_signal_handlers();
        LoadConfig(config_path);
        LOG_INFO("GTradeBacktest 启动，回测模式");

        // 创建线程池
        // 注意：k_BackTestThread 为 SyncThread（串行），是回测主循环线程
        zrt::EnginePool& pool = zrt::EnginePool::GetInstance();
        pool.AddNamedEngine<SyncThread>(k_BackTestThread);
        pool.AddNamedEngine<BoostAsioThread>(k_StrategyEngineThread);
        pool.AddNamedEngine<BoostAsioThread>(k_TimerManagerThread);
        pool.AddNamedEngine<BoostAsioThread>(k_MySqlGatewayThread);
        pool.AddSharedEngine<BoostAsioThread>(4);  // 回测不需要太多共享线程

        // 将虚拟时钟设置到回测起始时间
        TimeMachine::GetInstance().SetEpoch(
            MyUTC(m_cfg.start_date, BACKTEST_TIME_FORMAT).Epoch19());

        // 注册服务
        // 回测服务结构：
        //   StrategyEngine   - 策略执行核心（含委托、成交、持仓管理）
        //   MySqlGateway     - 数据库写入（单线程）
        //   CsvQuote         - CSV 历史行情数据源（替代 OkxWs）
        //   TimerManagerDummy- 虚拟定时器（替代 TimerManager）
        //   QueryServer      - 查询服务（策略状态读取等）
        ServiceMap& service_map = ServiceMap::GetInstance();
        service_map.emplace(k_StrategyEngine,
            std::make_unique<BacktestStrategyEngine>(service_map, m_cfg));
        BacktestEngine::RegisterServices(service_map, m_cfg);
        service_map.emplace(k_QueryServer,
            std::make_unique<QueryServer>(service_map, m_cfg));

        LOG_INFO("当前回测时间: {}", MyUTC().ToFormat());

        // 初始化所有服务（注册回调、赋值服务间指针）
        // 注意：EngineStart 之前线程未创建，Init 内不能互相 Post
        service_map.ForEach([](auto& srv) {
            srv->Init();
        });
        pool.EngineStart();
        service_map.ForEach([](auto& srv) {
            srv->Start();
        });

        LOG_INFO("GTradeBacktest 主循环开始，PID={}", getpid());
        while (GlobalControl::is_running) {
            sleep(1);
        }

        LOG_INFO("========================================");
        LOG_INFO("开始停止 GTradeBacktest...");
        LOG_INFO("========================================");

        service_map.StopAll();
        LOG_INFO("所有服务已停止");

        pool.WaitStop();
        LOG_INFO("GTradeBacktest 已退出");
    }

private:
    GTradeConfig m_cfg {};
};

int main(const int argc, char** argv) {
    std::set_terminate(zrt::safe_terminate_hdl);

    CLI::App app{"GTrade Backtest - Quantitative Trading Backtest System"};

    // 配置文件路径（可选，默认 config/backtest_config.yml）
    std::string config_path = "config/backtest_config.yml";
    app.add_option("--config", config_path, "Backtest configuration file path")
        ->default_val("config/backtest_config.yml");

    CLI11_PARSE(app, argc, argv);

    // 修改进程名，便于 ps 区分
    SetProcessName("gtrade_bt_main");
    SetParamShow("gtrade_bt", argc, argv);

    GTradeBacktest bt {};
    bt.Run(config_path);

    return 0;
}
