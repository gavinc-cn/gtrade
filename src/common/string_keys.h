//
// Created by dell on 2025/9/27.
//

#pragma once

#include "zrtools/zrt_string_keys.h"

// 日志
DECLARE_K(logger);
DECLARE_K(log_file);
DECLARE_K(async);
DECLARE_K(show_level);
DECLARE_K(log_level);
DECLARE_K(max_files);

// 服务名称
DECLARE_K(QueryServer);
DECLARE_K(StrategyEngine);
DECLARE_K(MySqlGateway);
DECLARE_K(OkxQuote);
DECLARE_K(OkxDummyQuote);
DECLARE_K(OkxTrade);
DECLARE_K(DpdkQuote);     // DPDK 行情探测服务 (ServiceMap key, GTRADE_ENABLE_DPDK_PROBE)
DECLARE_K(CtpQuote);     // CTP 行情服务 (ServiceMap key)
DECLARE_K(CtpTrader);    // CTP 交易网关 (用于日志/标识, 实际按 account_id 存于 m_trade_gw_map)
DECLARE_K(CsvQuote);
DECLARE_K(DummyTrade);
DECLARE_K(TimerManager);
DECLARE_K(MessageServer);
DECLARE_K(HttpGateway);
DECLARE_K(DesktopGateway);

// 命名线程
DECLARE_K(BackTestThread);
DECLARE_K(StrategyEngineThread);
DECLARE_K(TimerManagerThread);
DECLARE_K(MySqlGatewayThread);
DECLARE_K(MessageServerThread);
DECLARE_K(HttpGatewayThread);
DECLARE_K(DesktopGatewayThread);

// 策略模版id
DECLARE_K(StratDemo);
DECLARE_K(StratApiTest);
DECLARE_K(StrategyRecorder);
DECLARE_K(StrategyDemoOkx);
DECLARE_K(StrategyFutureArbitrage);
DECLARE_K(FutureArbitrageV2);
DECLARE_K(FutureArbitrageV3);
DECLARE_K(StratSMA);
DECLARE_K(StratSMA_V2);
DECLARE_K(IndicatorKline);
DECLARE_K(StratSpotGrid);
DECLARE_K(StratSpotGridFull);
DECLARE_K(StratBacktestValidator);

// 行情类型
DECLARE_K(depth1);

// 交易所
DECLARE_K(okx);
DECLARE_K(okx_dummy);
DECLARE_K(ctp);             // CTP 期货 market 标识
DECLARE_K(dpdk_bench);      // DPDK 延时探测 market 标识 (触发 DpdkTradeSink 出口)
DECLARE_K(huobi);
DECLARE_K(binance);

// 数据库
DECLARE_K(mysql);
DECLARE_K(host);
DECLARE_K(port);
DECLARE_K(user);
DECLARE_K(pwd);
DECLARE_K(password);
DECLARE_K(db);

// 策略参数
DECLARE_K(strat_template_id);
DECLARE_K(so_path);          // 动态插件路径，存在时走 dlopen 加载路径
// 远程策略接入端口（ZMQ ROUTER）
DECLARE_K(remote_engine_port);
// 子进程隔离配置
DECLARE_K(isolation);        // 隔离模式: "inprocess"（默认）| "subprocess"
DECLARE_K(subprocess_auto_restart);        // 崩溃后是否自动重启（bool）
DECLARE_K(subprocess_restore_checkpoint);  // 重启后是否从 checkpoint 恢复（bool）
DECLARE_K(subprocess_max_restart_count);   // 最大重启次数（int，默认3）
DECLARE_K(subprocess_restart_interval_ms); // 重启间隔ms（int，默认5000）
DECLARE_K(subprocess_heartbeat_timeout_ms);// 心跳超时阈值ms（int，默认3000）
// Python 策略配置（python_file 存在时走 OutProcessProxy + Python runner 路径）
DECLARE_K(python_file);          // Python 策略文件路径（绝对路径）
DECLARE_K(python_class);         // Python 策略类名
DECLARE_K(python_interpreter);   // Python 解释器路径（默认 python3，可指向 venv）
DECLARE_K(account_id);
DECLARE_K(instrument);
DECLARE_K(amount);
DECLARE_K(fallback_price);
DECLARE_K(instrument_fst);
DECLARE_K(instrument_sec);
DECLARE_K(max_entamt);
DECLARE_K(min_entamt);
DECLARE_K(unequal_wait_time);
DECLARE_K(offset_max_slippage);
DECLARE_K(kline_window_size);
DECLARE_K(fee);
DECLARE_K(expire_date);

// 网格策略参数
DECLARE_K(price_lower);
DECLARE_K(price_upper);
DECLARE_K(grid_count);
DECLARE_K(grid_mode);
DECLARE_K(invest_amount);
DECLARE_K(take_profit_price);
DECLARE_K(stop_loss_price);
DECLARE_K(max_single_order_amount);
DECLARE_K(min_order_amount);
DECLARE_K(fee_rate);
DECLARE_K(order_wait_ms);
DECLARE_K(cancel_wait_ms);
DECLARE_K(max_slippage);

// 账户
DECLARE_K(market);
DECLARE_K(key);
DECLARE_K(secret);
DECLARE_K(passphrase);
// CTP 账户扩展字段 (存入 Account.extra，由 gtrade.cpp 从 YAML 自动收集)
DECLARE_K(broker_id);
DECLARE_K(investor_id);
DECLARE_K(td_front);     // 交易前置地址 tcp://ip:port
DECLARE_K(md_front);     // 行情前置地址 tcp://ip:port
DECLARE_K(auth_code);    // 认证码 (实盘需要，openctp 7x24 可空)
DECLARE_K(app_id);       // 终端认证 AppID (实盘需要，openctp 7x24 可空)
DECLARE_K(proxy);
DECLARE_K(http);
DECLARE_K(socks5);
DECLARE_K(date);

// 配置
DECLARE_K(strategy_config);
DECLARE_K(account_config);
DECLARE_K(db_config);
DECLARE_K(data_config);
DECLARE_K(slack_config);
// 交易所URL配置
DECLARE_K(url);
DECLARE_K(rest);
DECLARE_K(ws_public);
DECLARE_K(ws_private);
DECLARE_K(query_processor_num);
DECLARE_K(http_server_port);
DECLARE_K(entrust_maintain_days);
// DPDK 延时探测配置块 (dpdk_probe)
DECLARE_K(dpdk_probe);
DECLARE_K(port_id);
DECLARE_K(rx_queue);
DECLARE_K(tx_queue);
DECLARE_K(pin_cpu);
DECLARE_K(eal_args);
DECLARE_K(dst_mac);
// 推送出口配置块 (engine_push)：引擎 → web_server
// 注意：不能复用 web_push —— 那段是 web_server (Flask) 的推送参数，语义不同
DECLARE_K(engine_push);
DECLARE_K(ping_ms);
DECLARE_K(quote_min_interval_ms);
DECLARE_K(desktop_gateway);
DECLARE_K(enabled);
DECLARE_K(server_address);
DECLARE_K(ca_cert);
DECLARE_K(server_cert);
DECLARE_K(server_key);
DECLARE_K(jwt_secret);
DECLARE_K(jwt_expiration);

// Slack Channels
DECLARE_K(notice);
DECLARE_K(warning);
DECLARE_K(zz);
DECLARE_K(balance);
DECLARE_K(websocket);
DECLARE_K(multi_factor);

// 标的类型
DECLARE_K(SPOT);
DECLARE_K(MARGIN);
DECLARE_K(SWAP);
DECLARE_K(FUTURES);
DECLARE_K(OPTION);

