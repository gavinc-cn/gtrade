#include <filesystem>
#include <dlfcn.h>
#include <cstring>
#include <boost/interprocess/shared_memory_object.hpp>
#include "strategy_engine.h"
#include "strategy_plugin.h"
#include "strategy_checkpoint.h"
#include "dict_mapping.h"
#include "zrtools/zrt_compare.h"
#include "i_strategy_engine_dump.h"
#include "zrtools/zrt_define.h"
#include "err_code.h"
#include "type_define_dump.h"
#include "i_timer_manager_dump.h"
#include "okx_trade.h"
#include "dummy_trade.h"
#include "ctp_trader.h"   // CTP 交易网关（每账户一个）
#ifdef GTRADE_ENABLE_DPDK_PROBE
#include "latency_probe/dpdk_trade_sink.h"  // DPDK 延时探测交易出口（平级 OkxTrade）
#endif
#include "global.h"
#include "my_utc.h"
#include "OkexClient.h"
#include "strategy_zmq_channel.h"
#include "zrtools/latency/latency_tracer.h"  // 延时测试打点宏（未启用时编译为空）

// 策略通过自注册工厂加载，无需在此 include 各策略头文件
#include "strategy_factory.h"

namespace {
// tdMode → 一个"优先尝试"的 instType（OKX 风格）。只用于给行情缓存查询一个偏好，不是过滤器：
// tdMode 对现货表示"现货(cash) vs 币币杠杆(cross/isolated)"，对永续/交割表示的是**合约保证金
// 模式**，所以合约单带 isolated 时这里推出来的是 MARGIN，与该 instId 实际登记的 SWAP 不符 ——
// FindMarketInfo 未精确命中会按优先级回落到该 instId 的任一条，结果仍正确。
std::string InstTypeFromTdMode(const char td_mode) {
    switch (td_mode) {
        case TradeMode::Cash: return std::string(k_SPOT);
        case TradeMode::Cross:
        case TradeMode::Isolated: return std::string(k_MARGIN);
        default: return {};
    }
}
}   // namespace

bool StrategyEngine::Init() {
    SPDLOG_INFO("{}", __PRETTY_FUNCTION__ );

    // if (!m_gtrade_cfg.is_backtest) {
    m_qry_srv = m_pool.at(k_QueryServer).get();
    // }
    m_timer_manager = m_pool.at(k_TimerManager).get();
    // m_csv_quote = m_pool.at(k_CsvQuote).get();
    m_msg_srv = m_pool.at(k_MessageServer).get();
    m_mysql_gateway = m_pool.at(k_MySqlGateway).get();

    m_kline_manager = std::make_unique<KLineManager>(m_gtrade_cfg, *this, dynamic_cast<QueryServer &>(*m_qry_srv));

    // 推送出口（引擎 → web_server）：配置启用才建连接；查询回调把 web_server 的补查请求
    // 投到引擎线程执行（PostSyncMsg），因此补查结果与推送事件在引擎侧天然同序。
    m_event_publisher.Init(m_gtrade_cfg.engine_push);
    m_event_publisher.SetQueryHandler([this](const int msg_id, const BufPtr& req) -> BufPtr {
        BufPtr rsp {};
        PostSyncMsg(msg_id, req, rsp);
        return rsp;
    });
    
    // 事件源管理器只在回测模式下初始化
    if constexpr (!GlobalConst::IsRealTrading) {
        m_event_source_manager = std::make_unique<EventSourceManager>(*this, m_gtrade_cfg);
    }

    InstallDefaultHandler(DefaultMsgHandler);

    // 外发通知消息
    ZRT_ADD_HANDLER(kNotifyMessage, StrategyEngine::OnNotifyMsg);

    // 发往行情网关
    ZRT_ADD_HANDLER(kStratSubscribeQuote, StrategyEngine::OnSubscribeQuote);
    // 发往交易网关
    ZRT_ADD_HANDLER(kStratSubscribeTrade, StrategyEngine::OnSubscribeTrade);
    ZRT_ADD_HANDLER(kPlaceOrder, StrategyEngine::OnPlaceOrderReq);
    ZRT_ADD_HANDLER(kCancelOrder, StrategyEngine::OnCancelOrderReq);

    /// 发往策略
    // 行情推送
    ZRT_ADD_HANDLER(kDepth1, StrategyEngine::OnDepth1);
    // 交易推送
    ZRT_ADD_HANDLER(kPlaceOrderRsp, StrategyEngine::OnPlaceOrderRsp); // 未调用
    ZRT_ADD_HANDLER(kCancelOrderRsp, StrategyEngine::OnCancelOrderRsp);
    ZRT_ADD_HANDLER(kPlaceOrderConfirm, StrategyEngine::OnPlaceOrderConfirm);
    ZRT_ADD_HANDLER(kOrderRecovery, StrategyEngine::OnOrderRecovery);  // websocket重连后的委托恢复
    ZRT_ADD_HANDLER(kTradePush, StrategyEngine::OnTradePush);
    ZRT_ADD_HANDLER(kPositionPush, StrategyEngine::OnPosPush);
    ZRT_ADD_HANDLER(kBalancePush, StrategyEngine::OnBalancePush);
    // 补查（web_server → 引擎，同步：调用方阻塞等结果）
    ZRT_ADD_SYNC_HANDLER(kHttpQueryOrdersReq, StrategyEngine::OnHttpQueryOrders);
    ZRT_ADD_SYNC_HANDLER(kHttpQueryTradesReq, StrategyEngine::OnHttpQueryTrades);
    // 策略查询
    ZRT_ADD_HANDLER(kQueryKLineReq, StrategyEngine::OnQueryKLineReq);
    ZRT_ADD_HANDLER(kQueryOrderReq, StrategyEngine::OnHandleQueryServerReq);
    ZRT_ADD_HANDLER(kQueryOrderByPrivateNoReq, StrategyEngine::OnQueryOrderByPrivateNoReq);
    ZRT_ADD_HANDLER(kQueryMarketInfoReq, StrategyEngine::OnStratQueryMarketInfoReq);
    // 查询服务应答
    ZRT_ADD_HANDLER(kQueryMarketInfoRsp, StrategyEngine::OnQueryMarketInfoRsp);
    ZRT_ADD_HANDLER(kQueryOrderRsp, StrategyEngine::OnQueryOrderRsp);
    ZRT_ADD_HANDLER(kQueryTradeRsp, StrategyEngine::OnQueryTradeRsp);
    ZRT_ADD_HANDLER(kQueryPositionRsp, StrategyEngine::OnQueryPosRsp);
    ZRT_ADD_HANDLER(kQueryBalanceRsp, StrategyEngine::OnQueryBalanceRsp);
    ZRT_ADD_HANDLER(kQueryKLinePatchRsp, StrategyEngine::OnQueryKLinePatchRsp);
    ZRT_ADD_HANDLER(kQueryKLineRsp, StrategyEngine::OnQueryKLineRsp);
    ZRT_ADD_HANDLER(kQueryHisOrdersRsp, StrategyEngine::OnQueryHisOrdersRsp);
    // 定时器
    ZRT_ADD_HANDLER(kTimerEvent, StrategyEngine::OnHandleTimerEvent);
    // 控制消息
    ZRT_ADD_HANDLER(kWebSocketOpenNotify, StrategyEngine::OnWebSocketOpenNotify);
    ZRT_ADD_HANDLER(kSetTimer, StrategyEngine::OnSetTimer);
    ZRT_ADD_HANDLER(kKillTimer, StrategyEngine::OnHandleKillTimerReq);
    ZRT_ADD_HANDLER(kClearAllTimer, StrategyEngine::OnHandleClearAllTimerReq);
    // 指标订阅
    ZRT_ADD_HANDLER(kStratSubscribeKLine, StrategyEngine::OnSubscribeKLine);
    ZRT_ADD_HANDLER(kStratSubscribeKLineOpen, StrategyEngine::OnSubscribeKLineOpen);
    ZRT_ADD_HANDLER(kStratSubscribeKLineClose, StrategyEngine::OnSubscribeKLineClose);
    // 指标推送
    ZRT_ADD_HANDLER(kIndicatorKLinePush, StrategyEngine::OnIndicatorKlinePush);
    ZRT_ADD_HANDLER(kIndicatorKLineOpenPush, StrategyEngine::OnIndicatorKlineOpenPush);
    ZRT_ADD_HANDLER(kIndicatorKLineClosePush, StrategyEngine::OnIndicatorKlineClosePush);
    // 策略请求
    ZRT_ADD_HANDLER(kDbSetStrategyInfo, StrategyEngine::OnDbSetStrategyInfo);
    ZRT_ADD_HANDLER(kDbSetStrategyLog, StrategyEngine::OnDbSetStrategyLog);

    // 远程策略 ZMQ 事件（AcceptLoop 线程通过 PostMsg 投递到引擎线程）
    ZRT_ADD_HANDLER(kRemoteStrategyConnected, StrategyEngine::OnRemoteStrategyConnected);
    ZRT_ADD_HANDLER(kRemoteChannelB, StrategyEngine::OnRemoteChannelB);
    ZRT_ADD_HANDLER(kRemoteSyncReq, StrategyEngine::OnRemoteSyncReq);

    // 启动远程策略 ZMQ 接受器（仅当配置了 remote_engine_port 时）
    if constexpr (GlobalConst::IsRealTrading) {
        if (m_gtrade_cfg.remote_engine_port > 0) {
            m_zmq_context = std::make_unique<zmq::context_t>(1 /*io_threads*/);
            m_zmq_acceptor = std::make_unique<ZmqAcceptor>(
                *m_zmq_context,
                m_gtrade_cfg.remote_engine_port,
                this);
            m_zmq_acceptor->Start();
            SPDLOG_INFO("StrategyEngine: ZmqAcceptor started on port {}",
                        m_gtrade_cfg.remote_engine_port);
        }
    }

    /// 同步接口
    // 策略同步查询
    ZRT_ADD_SYNC_HANDLER(kQueryMarketInfoSync, StrategyEngine::OnStratQueryMarketInfoSync);
    // HTTP策略管理同步消息处理
    ZRT_ADD_SYNC_HANDLER(kHttpAddStrategy, StrategyEngine::OnHttpAddStrategy);
    ZRT_ADD_SYNC_HANDLER(kHttpDeleteStrategy, StrategyEngine::OnHttpDeleteStrategy);
    ZRT_ADD_SYNC_HANDLER(kHttpRestartStrategy, StrategyEngine::OnHttpRestartStrategy);
    ZRT_ADD_SYNC_HANDLER(kHttpStartStrategy, StrategyEngine::OnHttpStartStrategy);
    ZRT_ADD_SYNC_HANDLER(kHttpStopStrategy, StrategyEngine::OnHttpStopStrategy);
    ZRT_ADD_SYNC_HANDLER(kHttpQueryAllStrategies, StrategyEngine::OnHttpQueryAllStrategies);
    ZRT_ADD_SYNC_HANDLER(kHttpQueryStrategiesByTemplate, StrategyEngine::OnHttpQueryStrategiesByTemplate);
    ZRT_ADD_SYNC_HANDLER(kHttpGetTemplates, StrategyEngine::OnHttpGetTemplates);
    ZRT_ADD_SYNC_HANDLER(kHttpGetTemplateConfig, StrategyEngine::OnHttpGetTemplateConfig);
    // HTTP系统管理
    ZRT_ADD_SYNC_HANDLER(kHttpSaveSnapshot, StrategyEngine::OnHttpSaveSnapshot);
    ZRT_ADD_SYNC_HANDLER(kHttpGetWalStats, StrategyEngine::OnHttpGetWalStats);
#ifdef GTRADE_ENABLE_HTTP_TRADE
    ZRT_ADD_SYNC_HANDLER(kHttpPlaceOrder, StrategyEngine::OnHttpPlaceOrder);
    ZRT_ADD_SYNC_HANDLER(kHttpCancelOrder, StrategyEngine::OnHttpCancelOrder);
#endif  // GTRADE_ENABLE_HTTP_TRADE
    ZRT_ADD_SYNC_HANDLER(kHttpGetDepth, StrategyEngine::OnHttpGetDepth);
    // 标的范围订阅（web 设置页）
    ZRT_ADD_SYNC_HANDLER(kHttpQueryInstruments, StrategyEngine::OnHttpQueryInstruments);
    ZRT_ADD_SYNC_HANDLER(kHttpGetInstrumentScope, StrategyEngine::OnHttpGetInstrumentScope);
    ZRT_ADD_SYNC_HANDLER(kHttpSetInstrumentScope, StrategyEngine::OnHttpSetInstrumentScope);

    return true;
}

void StrategyEngine::SetupUplUpdateTimer() {
    // 设置策略持仓 UPL 定时更新（每 5 秒更新一次）
    // 注：改为定时器触发而非每条行情触发，避免性能瓶颈
    SetTimerReq upl_timer_req {};
    zrt::fill_field(upl_timer_req.service_name, k_StrategyEngine);
    zrt::fill_field(upl_timer_req.setter_id, k_StrategyEngine);
    upl_timer_req.timer_id = TimerId::kTimer_UpdateUpl;  // UPL更新定时器ID
    upl_timer_req.delay_ms = 5000;  // 5秒
    upl_timer_req.repeat = true;    // 重复触发
    m_timer_manager->PostMsg(kSetTimer, std::make_shared<TBuffer>(upl_timer_req));
    SPDLOG_INFO("Set UPL update timer: interval={}ms", upl_timer_req.delay_ms);
}

void StrategyEngine::SetupMarketInfoRefreshTimer() const {
    // 设置市场信息定时刷新（每 1 小时刷新一次）
    // 用于定期从交易所更新可交易标的列表，确保本地缓存与交易所同步
    if constexpr (!GlobalConst::IsRealTrading) {
        SPDLOG_INFO("Market info refresh timer disabled in backtest mode");
        return;
    }
    SetTimerReq market_info_timer_req {};
    zrt::fill_field(market_info_timer_req.service_name, k_StrategyEngine);
    zrt::fill_field(market_info_timer_req.setter_id, k_StrategyEngine);
    market_info_timer_req.timer_id = TimerId::kTimer_RefreshMarketInfo;
    market_info_timer_req.delay_ms = 3600'1000;  // 1小时
    market_info_timer_req.repeat = true;
    m_timer_manager->PostMsg(kSetTimer, std::make_shared<TBuffer>(market_info_timer_req));
    SPDLOG_INFO("Set market info refresh timer: interval=1hour");
}

void StrategyEngine::LoadRecentOrdersFromDb() {
    SPDLOG_INFO("Requesting to load recent entrusts from database (last {} days)", m_gtrade_cfg.entrust_maintain_days);
    HisEntrustsQryReq req {};
    zrt::fill_field(req.start_time, MyUTC().Epoch19() - m_gtrade_cfg.entrust_maintain_days * 3600 * 24 * zrt::kGiga);
    zrt::fill_field(req.market, k_mysql);
    BufPtr rsp_buf {};
    m_mysql_gateway->PostSyncMsg(kDbQueryHisOrdersReq, std::make_shared<TBuffer>(req), rsp_buf);

    // 判空：MySQL 不可用时 handler 返回空响应，避免空指针解引用
    if (rsp_buf) {
        rsp_buf->ForEach<Order>([this](const Order& entrust) {
            SPDLOG_DEBUG("Received entrust from DB: entno={}, private_no={}, policy_no={}, status={}", entrust.entno, entrust.private_no, entrust.policy_no, entrust.status);
            m_order_manager.RecoverOrder(entrust);
        });
    } else {
        SPDLOG_WARN("Empty response for recent entrusts query, skip order recovery");
    }
    SPDLOG_INFO("Loaded {} historical entrusts from database", m_order_manager.GetOrderCount());

    // 叠加共享内存中的数据（最新的未确认数据）
    size_t shm_recovered = m_order_manager.RecoverFromShm();
    SPDLOG_INFO("Overlayed {} uncommitted entrusts/dones from shared memory", shm_recovered);

    // 打印持久化统计信息
    const OrderManager::PersistenceStats stats = m_order_manager.GetPersistenceStats();
    SPDLOG_INFO("Persistence stats - Entrust: write_seq={}, confirmed_seq={}, unconfirmed={}", stats.order_write_seq, stats.order_confirmed_seq, stats.order_unconfirmed);
    SPDLOG_INFO("Persistence stats - Done: write_seq={}, confirmed_seq={}, unconfirmed={}",
                stats.trade_write_seq, stats.trade_confirmed_seq, stats.trade_unconfirmed);
    SPDLOG_INFO("Historical entrust loading completed, total entrusts in m_order_manager: {}", m_order_manager.GetOrderCount());

    // 设置号段基数（启动高水位）：保证新生成的 entno/tdno 不与历史号重叠，
    // 即使"同一秒内重启"也不会与前一次运行的号段撞车（订单 REPLACE 覆盖、成交主键冲突）。
    SetupIdBaseFromDb();
}

void StrategyEngine::SetupIdBaseFromDb() {
    // 取 DB 里的历史最大号（order/trade 主键即 entno/tdno，MAX 走主键末行）
    BufPtr max_rsp {};
    m_mysql_gateway->PostSyncMsg(kDbQueryMaxIdsReq, std::make_shared<TBuffer>(), max_rsp);
    int64_t db_max_entno = 0;
    int64_t db_max_tdno = 0;
    if (max_rsp && max_rsp->GetSize() > 0) {
        const auto& rsp = max_rsp->RefData<MaxIdsQryRsp>();
        db_max_entno = rsp.max_entno;
        db_max_tdno = rsp.max_tdno;
    } else {
        // MySQL 不可用：退回时间基数（由 SetIdBase 兜底），仅记警告不阻塞启动
        SPDLOG_WARN("max ids query unavailable, fall back to time-based id base");
    }

    // 再把内存里已恢复的（含 SHM 叠加的未确认数据）算进来，取三者上界
    const int64_t mem_max_entno = m_order_manager.GetMaxOrderNo();
    const int64_t mem_max_tdno = m_order_manager.GetMaxTradeNo();
    m_order_manager.SetIdBase(std::max(db_max_entno, mem_max_entno), std::max(db_max_tdno, mem_max_tdno));
}

bool StrategyEngine::Start() {
    SPDLOG_INFO("");

    // 清空strat_info表，准备重新加载策略信息
    SPDLOG_INFO("Clearing strat_info table before loading strategies");
    StrategyInfo dummy_info {};
    m_mysql_gateway->PostMsg(kDbDelAllStrategyInfo, std::make_shared<TBuffer>(dummy_info));

    // 实盘模式：先从共享内存加载策略（恢复之前运行的策略），然后加载配置文件中的策略
    // 回测模式：从配置文件加载策略
    if constexpr (GlobalConst::IsRealTrading) {
        LoadStrategiesFromShm();
    }
    // 加载配置文件中的策略（如果策略ID已存在，LoadSingleStrategyFromFile会跳过）
    SPDLOG_INFO("Loading strategies from config files");
    LoadStrategyCfgFromFile();

    // 初始化并启动事件源管理器（仅回测模式） - 必须在策略启动之前
    if constexpr (GlobalConst::IsBackTest) {
        if (!m_event_source_manager->Initialize()) {
            SPDLOG_ERROR("Failed to initialize EventSourceManager");
            return false;
        }

        if (!m_event_source_manager->Start()) {
            SPDLOG_ERROR("Failed to start EventSourceManager");
            return false;
        }
    }
    // 实盘模式：从数据库加载7天内委托并查询最新状态（在启动策略之前）
    else {
        LoadRecentOrdersFromDb();
        RefreshAllMarketInfo(true);
        LoadInstrumentScopeFromDb();   // 从 DB 恢复标的范围订阅（策略启动前）
    }

    // 启动策略 - 策略启动时可能会立即订阅事件源
    for (const auto& p: m_strategy_proxy_map) {
        p.second->Start();
        SPDLOG_INFO("strat={} started", p.first);
    }

    // if (m_gtrade_cfg.is_backtest) {
    //     int64_t bt_epoch = MyUTC(m_gtrade_cfg.start_date, BACKTEST_TIME_FORMAT).Epoch19();
    //     int64_t end_epoch = MyUTC(m_gtrade_cfg.end_date, BACKTEST_TIME_FORMAT).Epoch19();
    //     while (true) {
    //         TimeMachine::GetInstance().SetEpoch(bt_epoch);
    //         const auto buf = std::make_shared<TBuffer>();
    //         buf->Append(bt_epoch);
    //         m_csv_quote->PostMsg(kCsvQuoteTimerEvent, buf);
    //         bt_epoch += m_gtrade_cfg.backtest_interval * zrt::kGiga;
    //         if (bt_epoch > end_epoch) {
    //             break;
    //         }
    //     }
    // }

    if constexpr (GlobalConst::IsRealTrading) {
        // 设置 UPL 定时更新（改为定时器触发而非每条行情触发，性能优化）
        SetupUplUpdateTimer();
        // 设置市场信息定时刷新（每小时刷新一次，仅实盘模式）
        SetupMarketInfoRefreshTimer();
    } else {
        // 使用事件源管理器进行回测
        // 这将自动处理所有事件源的时间推进和事件分发
        m_event_source_manager->RunBacktest();
        SPDLOG_INFO("Backtest completed via EventSourceManager");
        GlobalControl::is_running = false;
    }
    // 启动推送出口（配置 enabled=false 时内部直接返回；连接失败只在日志里体现，不影响交易）
    m_event_publisher.Start();
    return true;
}

void StrategyEngine::Stop() {
    SPDLOG_INFO("stopping all strategies");
    // 先停推送出口：避免策略停止过程中仍在向 web_server 发事件
    m_event_publisher.Stop();
    for (const auto& [strat_id, strat_ptr] : m_strategy_proxy_map) {
        strat_ptr->Stop();
        SPDLOG_INFO("strat={} stopped", strat_id);
    }
}

void StrategyEngine::OnDefaultMsg(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("{}", msg_id);
}

BufPtr StrategyEngine::OnDefaultSyncMsg(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("{}", msg_id);
    // default handler：未注册的同步消息走到这里，返回 nullptr 由框架兜底兑现空响应并打 ERROR
    return nullptr;
}

std::shared_ptr<StrategyBase> StrategyEngine::LoadStrategyFromSo(
    const std::string& so_path,
    const std::string& strat_id)
{
    // 解析路径：相对路径以可执行文件目录为基准
    std::filesystem::path resolved_path = so_path;
    if (resolved_path.is_relative()) {
        std::error_code ec;
        const auto exe_dir = std::filesystem::read_symlink("/proc/self/exe", ec).parent_path();
        if (ec) {
            SPDLOG_ERROR("LoadStrategyFromSo: failed to read /proc/self/exe: {}", ec.message());
            return nullptr;
        }
        resolved_path = exe_dir / so_path;
    }

    SPDLOG_INFO("LoadStrategyFromSo: loading plugin from {}", resolved_path.string());

    // 加载 .so
    // RTLD_LAZY  : 延迟解析符号，加快加载速度
    // RTLD_GLOBAL: 使 .so 中对主进程符号（StrategyBase vtable、EnginePool 单例等）的
    //              未定义引用从主进程全局符号表解析（-rdynamic 保证符号可见），避免 ODR 违规
    void* handle = dlopen(resolved_path.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    if (!handle) {
        SPDLOG_ERROR("LoadStrategyFromSo: dlopen failed for {}: {}", resolved_path.string(), dlerror());
        return nullptr;
    }

    // 清除历史错误状态，dlsym 返回 nullptr 可能是合法的，必须用 dlerror() 区分
    dlerror();

    // 校验构建模式（0=实盘，1=回测），防止误装载
    using GetBuildModeFunc = int (*)();
    auto get_mode = reinterpret_cast<GetBuildModeFunc>(dlsym(handle, "gtrade_get_build_mode"));
    const char* dl_err = dlerror();
    if (dl_err != nullptr) {
        SPDLOG_ERROR("LoadStrategyFromSo: dlsym(gtrade_get_build_mode) failed: {}", dl_err);
        dlclose(handle);
        return nullptr;
    }
    const int expected_mode = GlobalConst::IsRealTrading ? 0 : 1;
    const int actual_mode   = get_mode();
    if (actual_mode != expected_mode) {
        SPDLOG_ERROR("LoadStrategyFromSo: build mode mismatch in {}: expected={} got={}",
                     resolved_path.string(), expected_mode, actual_mode);
        dlclose(handle);
        return nullptr;
    }

    // 获取工厂函数
    dlerror();
    auto factory = reinterpret_cast<GTradeStrategyFactory>(dlsym(handle, "gtrade_create_strategy"));
    dl_err = dlerror();
    if (dl_err != nullptr) {
        SPDLOG_ERROR("LoadStrategyFromSo: dlsym(gtrade_create_strategy) failed: {}", dl_err);
        dlclose(handle);
        return nullptr;
    }

    // 调用工厂创建策略实例
    StrategyBase* raw_ptr = factory(m_gtrade_cfg, this, strat_id);
    if (raw_ptr == nullptr) {
        SPDLOG_ERROR("LoadStrategyFromSo: factory returned nullptr for strat_id={}", strat_id);
        dlclose(handle);
        return nullptr;
    }

    SPDLOG_INFO("LoadStrategyFromSo: plugin loaded ok, strat_id={}", strat_id);

    // 包装为 shared_ptr，自定义 deleter 保证析构顺序：
    //   先 delete ptr（执行析构函数，清理 std::function 等持有的回调）
    //   再 dlclose （卸载 .so 代码段，此时已无任何代码引用）
    // 顺序不能反转，否则析构时会访问已卸载的代码段导致段错误
    return std::shared_ptr<StrategyBase>(raw_ptr, [handle, strat_id](StrategyBase* p) {
        SPDLOG_INFO("LoadStrategyFromSo: unloading plugin for strat_id={}", strat_id);
        delete p;
        dlclose(handle);
    });
}

bool StrategyEngine::LoadSingleStrategyFromFile(const std::string& strat_cfg_path) {
    SPDLOG_INFO("load strategy from file: {}", strat_cfg_path);
    try {
        YAML::Node strat_yml = YAML::LoadFile(strat_cfg_path);
        // 策略ID从文件名提取（去掉.yml后缀）
        std::string strat_id = std::filesystem::path(strat_cfg_path).stem();
        // Python 策略不需要 strat_template_id（通过 python_file 字段区分）
        const std::string strat_template_id =
            strat_yml[k_strat_template_id] ? strat_yml[k_strat_template_id].as<std::string>() : "";

        // 按策略ID判断是否已存在（不再按配置文件路径判断）
        if (m_strategy_proxy_map.count(strat_id)) {
            SPDLOG_WARN("strategy with id={} already exists, cannot add duplicate", strat_id);
            return false;
        }

        bool loaded = false;

        // 解析公共子进程配置（so_path subprocess 路径和 python_file 路径均使用）
        auto parse_subprocess_config = [&]() {
            SubprocessConfig sp_cfg{};
            if (strat_yml[k_subprocess_auto_restart])
                sp_cfg.auto_restart = strat_yml[k_subprocess_auto_restart].as<bool>();
            if (strat_yml[k_subprocess_restore_checkpoint])
                sp_cfg.restore_checkpoint = strat_yml[k_subprocess_restore_checkpoint].as<bool>();
            if (strat_yml[k_subprocess_max_restart_count])
                sp_cfg.max_restart_count = strat_yml[k_subprocess_max_restart_count].as<int>();
            if (strat_yml[k_subprocess_restart_interval_ms])
                sp_cfg.restart_interval_ms = strat_yml[k_subprocess_restart_interval_ms].as<int>();
            if (strat_yml[k_subprocess_heartbeat_timeout_ms])
                sp_cfg.heartbeat_timeout_ms = strat_yml[k_subprocess_heartbeat_timeout_ms].as<int>();
            return sp_cfg;
        };

        // python_file 字段存在 → Python 子进程策略（OutProcessProxy + Python runner）
        if (strat_yml[k_python_file]) {
#ifdef __linux__
            // ── Python 子进程路径 ────────────────────────────────────────────────
            SubprocessConfig sp_cfg = parse_subprocess_config();
            sp_cfg.runner_type = SubprocessConfig::RunnerType::Python;
            if (strat_yml[k_python_interpreter])
                sp_cfg.python_interpreter = strat_yml[k_python_interpreter].as<std::string>();
            if (strat_yml[k_python_class])
                sp_cfg.python_class = strat_yml[k_python_class].as<std::string>();

            if (sp_cfg.python_class.empty()) {
                SPDLOG_ERROR("LoadSingleStrategyFromFile: python_class is required when "
                             "python_file is set, strat={}", strat_id);
                return false;
            }

            OutProcessProxyConfig op_cfg{};
            op_cfg.strat_cfg_path = strat_cfg_path;
            op_cfg.subprocess     = sp_cfg;

            auto proxy = std::make_unique<OutProcessProxy>(
                strat_id, StrategyInfo{}, std::move(op_cfg), this);
            if (!proxy->Init(strat_yml)) {
                SPDLOG_ERROR("OutProcessProxy::Init (Python) failed for strat={}", strat_id);
                return false;
            }
            m_strategy_proxy_map.emplace(strat_id, std::move(proxy));
            loaded = true;
#else
            SPDLOG_ERROR("python_file strategy is only supported on Linux, strat={}", strat_id);
            return false;
#endif
        }
        // 优先检查 so_path 字段：存在则走动态插件加载路径，无需修改引擎代码
        else if (strat_yml[k_so_path]) {
            // 检查隔离模式（默认 inprocess）
            const std::string isolation =
                strat_yml[k_isolation] ? strat_yml[k_isolation].as<std::string>() : "inprocess";

            if (isolation == "subprocess") {
#ifdef __linux__
                // ── C++ 子进程隔离路径 ─────────────────────────────────────────
                SubprocessConfig sp_cfg = parse_subprocess_config();

                OutProcessProxyConfig op_cfg{};
                op_cfg.strat_cfg_path = strat_cfg_path;
                op_cfg.subprocess     = sp_cfg;

                auto proxy = std::make_unique<OutProcessProxy>(
                    strat_id, StrategyInfo{}, std::move(op_cfg), this);
                if (!proxy->Init(strat_yml)) {
                    SPDLOG_ERROR("OutProcessProxy::Init failed for strat={}", strat_id);
                    return false;
                }
                m_strategy_proxy_map.emplace(strat_id, std::move(proxy));
                loaded = true;
#else
                SPDLOG_ERROR("isolation=subprocess is only supported on Linux, strat={}", strat_id);
                return false;
#endif
            } else {
                // ── 进程内路径（默认）─────────────────────────────────────────
                const std::string so_path = strat_yml[k_so_path].as<std::string>();
                auto strat_ptr = LoadStrategyFromSo(so_path, strat_id);
                if (!strat_ptr) {
                    return false;
                }
                loaded = LoadStrategy(strat_id, strat_ptr, strat_yml);
            }
        }
        else if (StrategyFactory::Contains(strat_template_id)) {
            // 已通过自注册机制注册的策略，由工厂统一创建
            auto strat_ptr = StrategyFactory::Create(strat_template_id, m_gtrade_cfg, this, strat_id);
            loaded = LoadStrategy(strat_id, strat_ptr, strat_yml);
            LOG_INFO("load {} {} from factory", strat_template_id, strat_id);
        }
        else {
            SPDLOG_ERROR("strat_template_id={} not found in config={}", strat_template_id, strat_cfg_path);
            return false;
        }

        if (loaded) {
            // 记录策略的配置文件路径
            m_strategy_cfg_path_map[strat_id] = strat_cfg_path;
            // 记录策略的模板ID
            m_strategy_template_map[strat_id] = strat_template_id;
            // 将策略添加到模板的策略集合中
            m_template_strategy_map[strat_template_id].insert(strat_id);
        }
        return loaded;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("load strategy config={} failed: {}", strat_cfg_path, e.what());
        return false;
    }
}

bool StrategyEngine::LoadStrategyCfgFromFile() {
    for (const auto& strat_cfg_path: m_gtrade_cfg.strat_cfg_path_vec) {
        LoadSingleStrategyFromFile(strat_cfg_path);
    }
    return true;
}

void StrategyEngine::LoadStrategiesFromShm() {
    // 共享内存检查点的前缀
    constexpr std::string_view kCheckpointPrefix = "gtrade_strategy_checkpoint_";
    constexpr std::string_view kShmDir = "/dev/shm/";

    // 使用 gtrade 运行目录同级的 strategy_config 目录
    const std::string config_dir = k_strategy_config;

    if (!std::filesystem::exists(config_dir)) {
        SPDLOG_WARN("Strategy config directory not found: {}", config_dir);
        return;
    }

    SPDLOG_INFO("Strategy config directory: {}", config_dir);

    // 扫描 /dev/shm/ 目录，找到所有检查点文件
    // 系统启动时不捕获异常，有错误直接报错退出，便于快速发现错误
    std::vector<std::string> strat_ids_from_shm {};

    for (const auto& entry : std::filesystem::directory_iterator(kShmDir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::string filename = entry.path().filename().string();
        // 检查是否是策略检查点文件
        if (filename.rfind(kCheckpointPrefix, 0) == 0) {
            // 提取策略ID（去掉前缀）
            std::string strat_id = filename.substr(kCheckpointPrefix.size());
            strat_ids_from_shm.push_back(strat_id);
            SPDLOG_INFO("Found strategy checkpoint in shm: {}", strat_id);
        }
    }

    if (strat_ids_from_shm.empty()) {
        SPDLOG_INFO("No strategy checkpoints found in shm");
        return;
    }

    SPDLOG_INFO("Found {} strategy checkpoints in shm", strat_ids_from_shm.size());

    // 为每个策略ID查找配置文件并加载
    for (const auto& strat_id : strat_ids_from_shm) {
        // 构建配置文件路径: strategy_config/<strat_id>.yml
        std::filesystem::path cfg_path = std::filesystem::path(config_dir) / (strat_id + ".yml");

        if (std::filesystem::exists(cfg_path)) {
            SPDLOG_INFO("Found config file for strategy={}: {}", strat_id, cfg_path.string());
            LoadSingleStrategyFromFile(cfg_path.string());
        } else {
            // 配置文件不存在，不加载该策略，仅输出错误日志
            SPDLOG_ERROR("Config file not found for strategy={}: {}, skipping", strat_id, cfg_path.string());
        }
    }
}

void StrategyEngine::OnNotifyMsg(int msg_id, const BufPtr buffer) const {
    m_msg_srv->PostMsg(kNotifyMessage, buffer);
}

void StrategyEngine::OnSubscribeQuote(int msg_id, const BufPtr buffer) {
    const QuoteSub quote_sub = *reinterpret_cast<const QuoteSub*>(buffer->Data());
    m_quote_sub_map[{quote_sub.channel,quote_sub.market,quote_sub.inst_id}].emplace(quote_sub.strat_id);
    SPDLOG_INFO("{} sub_cnt={}", zrt::to_str(quote_sub), m_quote_sub_map.size());
    // 实盘
    if constexpr (GlobalConst::IsRealTrading) {
        if (zrt::equal(quote_sub.market, k_okx)) {
            m_pool.at(k_OkxQuote)->PostMsg(kStratSubscribeQuote, buffer);
        }
        else if (zrt::equal(quote_sub.market, k_okx_dummy)) {
            m_pool.at(k_OkxDummyQuote)->PostMsg(kStratSubscribeQuote, buffer);
        }
        else if (zrt::equal(quote_sub.market, k_ctp)) {
            // CTP 行情订阅转给 CtpMd 服务（ServiceMap 中的 k_CtpQuote）
            m_pool.at(k_CtpQuote)->PostMsg(kStratSubscribeQuote, buffer);
        }
        else {
            SPDLOG_ERROR("market={} not supported", quote_sub.market);
        }
    }
    // 回测
    else {
        // m_pool.at(k_CsvQuote)->PostMsg(kStratSubscribeQuote, buffer);
        m_event_source_manager->SubscribeDepth({quote_sub.market, quote_sub.inst_id});
    }
}

bool StrategyEngine::EnsureTradeGateway(const std::string& market, const std::string& account_id) {
    if constexpr (GlobalConst::IsRealTrading) {
        if (!m_trade_gw_map.count(account_id)) {
            // okx和okx_dummy都使用OkxTrade, URL由account的market字段决定
            if (zrt::equal_any_of(market, k_okx, k_okx_dummy)) {
                m_trade_gw_map.emplace(account_id, std::make_shared<OkxTrade>(m_gtrade_cfg, account_id, *this));
                m_trade_gw_map.at(account_id)->Init();
                m_trade_gw_map.at(account_id)->Start();
                SPDLOG_INFO("create trade_gateway {} for market={}", account_id, market);
            }
            else if (zrt::equal(market, k_ctp)) {
                // CTP 交易网关：每账户一个 CtpTrader，凭证取自 account.extra
                m_trade_gw_map.emplace(account_id, std::make_shared<CtpTrader>(m_gtrade_cfg, account_id, this));
                m_trade_gw_map.at(account_id)->Init();
                m_trade_gw_map.at(account_id)->Start();
                SPDLOG_INFO("create trade_gateway {} for market={}", account_id, market);
            }
#ifdef GTRADE_ENABLE_DPDK_PROBE
            else if (zrt::equal(market, k_dpdk_bench)) {
                // DPDK 延时探测交易出口（平级 OkxTrade）：rte_eth_tx_burst 上线 + wire2wire 打点。
                // 须在 DpdkQuoteSource.Start()（EAL/端口）之后创建——EnsureTradeGateway 首单时行情源已起。
                m_trade_gw_map.emplace(account_id, std::make_shared<DpdkTradeSink>(m_gtrade_cfg, account_id, *this));
                m_trade_gw_map.at(account_id)->Init();
                m_trade_gw_map.at(account_id)->Start();
                SPDLOG_INFO("create trade_gateway {} for market={}", account_id, market);
            }
#endif
            else {
                SPDLOG_ERROR("market={} not supported", market);
                return false;
            }
        }
    }
    else {
        if (!m_trade_gw_map.count(k_DummyTrade)) {
            m_trade_gw_map.emplace(k_DummyTrade, std::make_shared<DummyTrade>(m_gtrade_cfg, k_DummyTrade, *this));
            m_trade_gw_map.at(k_DummyTrade)->Init();
            m_trade_gw_map.at(k_DummyTrade)->Start();
            SPDLOG_INFO("create trade_gateway {}", k_DummyTrade);
        }
    }
    return true;
}

void StrategyEngine::OnSubscribeTrade(int msg_id, const BufPtr buffer) {
    const TradeSub trade_sub = buffer->RefData<TradeSub>();
    SPDLOG_INFO("{}", zrt::to_str(trade_sub));
    m_trade_sub_map[trade_sub.account_id][trade_sub.inst_id].emplace(trade_sub.strat_id);
    if (!EnsureTradeGateway(trade_sub.market, trade_sub.account_id)) {
        SPDLOG_ERROR("create trade gateway failed, market={}, account={}", trade_sub.market, trade_sub.account_id);
        return;
    }
    const std::string account_id = [&trade_sub]() {
        if constexpr (GlobalConst::IsRealTrading) {
            return trade_sub.account_id;
        } else {
            return k_DummyTrade;
        }
    }();
    // okx会自动订阅所有的标的, 不用主动订阅, 有其他交易所再视情况主动订阅
    // m_trade_gw_map.at(account_id)->PostMsg(kStratSubscribeTrade, buffer);

    if constexpr (GlobalConst::IsRealTrading) {
        if (m_market_info_status[trade_sub.market] == DataStatus::kNotReady) {
            for (const auto& d: {InstType::Spot, InstType::Margin, InstType::Swap, InstType::Futures, InstType::Option}) {
                MarketInfoQryReq req {};
                zrt::fill_field(req.market, trade_sub.market);
                zrt::fill_field(req.account_id, trade_sub.account_id);
                zrt::fill_field(req.inst_type, d);
                m_qry_srv->PostMsg(kQueryMarketInfoReq, std::make_shared<TBuffer>(req));
            }
            m_market_info_status[trade_sub.market] = DataStatus::kReqSent;
        }

        if (m_balance_status[trade_sub.account_id] == DataStatus::kNotReady) {
            BalanceQryReq req {};
            zrt::fill_field(req.market, trade_sub.market);
            zrt::fill_field(req.account_id, trade_sub.account_id);
            m_qry_srv->PostMsg(kQueryBalanceReq, std::make_shared<TBuffer>(req));
            m_balance_status[trade_sub.account_id] = DataStatus::kReqSent;
        }

        if (m_pos_status[trade_sub.account_id] == DataStatus::kNotReady) {
            HoldQryReq req {};
            zrt::fill_field(req.market, trade_sub.market);
            zrt::fill_field(req.account_id, trade_sub.account_id);
            m_qry_srv->PostMsg(kQueryPositionReq, std::make_shared<TBuffer>(req));
            m_pos_status[trade_sub.account_id] = DataStatus::kReqSent;
        }

        // 报盘会在登录的时候主动查询, 这里不用查询
        // if (m_entrust_status[trade_sub.account_id] == DataStatus::kNotReady) {
        //     for (const auto& d: {InstType::Spot, InstType::Margin, InstType::Swap, InstType::Futures, InstType::Option}) {
        //         EntrustQryReq req {};
        //         zrt::fill_field(req.market, trade_sub.market);
        //         zrt::fill_field(req.account_id, trade_sub.account_id);
        //         zrt::fill_field(req.inst_type, DictInstType2Okx(d));
        //         m_qry_srv->PostMsg(kQueryEntrustReq, std::make_shared<TBuffer>(req));
        //     }
        //     m_entrust_status[trade_sub.account_id] = DataStatus::kReqSent;
        // }
    }
}

void StrategyEngine::FillNewEntByReq(Order& dst, const OrderReq& src) {
    zrt::fill_field(dst.market, src.market);
    zrt::fill_field(dst.account_id, src.account_id);
    zrt::fill_field(dst.inst_id, src.inst_id);
    zrt::fill_field(dst.inst_id_code, src.inst_id_code);
    zrt::fill_field(dst.policy_no, src.policy_no);
    zrt::fill_field(dst.private_no, src.private_no);
    zrt::fill_field(dst.bs_side, src.bs_side);
    zrt::fill_field(dst.pos_side, src.pos_side);
    zrt::fill_field(dst.oc_side, src.oc_side);
    zrt::fill_field(dst.trade_mode, src.trade_mode);
    zrt::fill_field(dst.price_type, src.price_type);
    zrt::fill_field(dst.price, src.price);
    zrt::fill_field(dst.amount, src.amount);
    zrt::fill_field(dst.expire_time, src.expire_time);
    zrt::fill_field(dst.ent_time, src.ent_time);
    zrt::fill_field(dst.quote_monotonic, src.quote_monotonic);  // 延时 T0 贯穿：OrderReq.quote_monotonic → Order，供下游 OkxTrade tick2order / DpdkTradeSink wire2wire
    zrt::fill_field(dst.fmt_time, MyUTC(dst.ent_time, 19).GetYmdHMS());
    // 特殊处理
    zrt::fill_field(dst.entno, OrderManager::CreateOrderId());
    // 先置为废单, 如果发出去就改为正报
    zrt::fill_field(dst.status, OrderStatus::_9);
    zrt::fill_field(dst.remain, src.amount);
    zrt::fill_field(dst.update_time, src.ent_time);
    zrt::fill_field(dst.source, OrderSource::Local);
    zrt::fill_field(dst.portfolio, zrt::is_empty(src.portfolio) ? src.policy_no : src.portfolio);
}

void StrategyEngine::FillOrderByOrder(Order& dst, const Order& src) {
    zrt::fill_field(dst.status, src.status);
    zrt::fill_field(dst.filled, src.filled);
    zrt::fill_field(dst.filled_px, src.filled_px);
    zrt::fill_field(dst.confirm_time, src.confirm_time);
    zrt::fill_field(dst.filled_time, src.filled_time);
    zrt::fill_field(dst.update_time, src.update_time);
    zrt::fill_field(dst.source, src.source);
    zrt::fill_field(dst.err_code, src.err_code);
    zrt::fill_field(dst.err_msg, src.err_msg);
    if (dst.err_code) {
        zrt::fill_field(dst.status, OrderStatus::_9);
    }
    if (!src.remain) {
        zrt::fill_field(dst.remain, dst.amount - src.filled - src.draw_amt);
    }
    switch (src.status) {
        case OrderStatus::_2: {
            zrt::fill_field(dst.confirm_time, src.confirm_time);
        } break;
        case OrderStatus::_6: {
            zrt::fill_field(dst.withdraw_time, src.withdraw_time);
            zrt::fill_field(dst.drawno, src.drawno);
            zrt::fill_field(dst.draw_amt, src.draw_amt);
        } break;
        case OrderStatus::_4:
        case OrderStatus::_3: {
            zrt::fill_field(dst.filled_time, src.filled_time);
        } break;
        default:
            break;
    }
}

// 自动开平仓
bool StrategyEngine::FillSide(Order& order, const std::string_view account_id, const std::string_view instrument, const char trade_mode, const char pos_side, const double entamt) {
    if (!order.bs_side) {
        SPDLOG_ERROR("bs_side is required");
        return false;
    }
    if (!order.oc_side && !order.pos_side) {
        const Position& hold = m_order_manager.GetPos(account_id, instrument, trade_mode, pos_side);
        if (zrt::greater_equal(hold.available, entamt)) {
            zrt::fill_field(order.oc_side, PosEffect::Close);
        }
        else {
            zrt::fill_field(order.oc_side, PosEffect::Open);
        }
    }
    if (order.bs_side == TradeSide::Buy) {
        if (!order.pos_side) {
            zrt::fill_field(order.pos_side, order.oc_side == PosEffect::Open ? PosSide::Long : PosSide::Short);
        }
        else if (!order.oc_side) {
            zrt::fill_field(order.oc_side, order.pos_side == PosSide::Long ? PosEffect::Open : PosEffect::Close);
        }
        else {
            if (order.oc_side == PosEffect::Open && order.pos_side != PosSide::Long) {
                return false;
            }
            else if (order.oc_side == PosEffect::Close && order.pos_side != PosSide::Short) {
                return false;
            }
        }
    }
    else if (order.bs_side == TradeSide::Sell) {
        if (!order.pos_side) {
            zrt::fill_field(order.pos_side, order.oc_side == PosEffect::Open ? PosSide::Short : PosSide::Long);
        }
        else if (!order.oc_side) {
            zrt::fill_field(order.oc_side, order.pos_side == PosSide::Long ? PosEffect::Close : PosEffect::Open);
        }
        else {
            if (order.oc_side == PosEffect::Open && order.pos_side != PosSide::Short) {
                return false;
            }
            else if (order.oc_side == PosEffect::Close && order.pos_side != PosSide::Long) {
                return false;
            }
        }
    }
    return true;
}

// 自动开平仓
bool StrategyEngine::FillSide(Order& order) {
    if (ZRT_UNLIKELY(zrt::is_empty(order.bs_side))) {
        SPDLOG_ERROR("bs_side is required");
        return false;
    }
    if (ZRT_LIKELY(!zrt::is_empty(order.pos_side) ||
        zrt::equal(order.trade_mode, TradeMode::Cash))) {
        return true;
    }
    if (zrt::equal_any_of(order.market, k_okx, k_okx_dummy)) {
        zrt::fill_field(order.pos_side, PosSide::Net);
        return true;
    }
    switch (order.bs_side) {
        case TradeSide::Buy: {
            if (const Position& hold = m_order_manager.GetPos(order.account_id, order.inst_id, order.trade_mode, PosSide::Short);
                zrt::greater_equal(hold.available, order.amount))
            {
                zrt::fill_field(order.pos_side, PosSide::Short);
            } else {
                zrt::fill_field(order.pos_side, PosSide::Long);
            }
        } break;
        case TradeSide::Sell: {
            if (const Position& hold = m_order_manager.GetPos(order.account_id, order.inst_id, order.trade_mode, PosSide::Long);
                zrt::greater_equal(hold.available, order.amount))
            {
                zrt::fill_field(order.pos_side, PosSide::Long);
            } else {
                zrt::fill_field(order.pos_side, PosSide::Short);
            }
        } break;
        default: {
            SPDLOG_ERROR("unexpected trade_side({})", order.bs_side);
        } break;
    }
    return true;
}

void StrategyEngine::OnPlaceOrderReq(int msg_id, const BufPtr buffer) {
    const auto& recv_data = buffer->RefData<OrderReq>();
#ifdef GTRADE_ENABLE_LATENCY_TEST
    LATENCY_SPAN_FROM("engine_ent_recv", recv_data.quote_monotonic);  // 原 [TT] engine_ent_in
#endif
    SPDLOG_INFO("EntrustReq={}", zrt::to_str(recv_data));

    Order new_entrust {};
    FillNewEntByReq(new_entrust, recv_data);
    Order* entrust {};
    do {
        entrust = m_order_manager.AddOrder(new_entrust);
        if (!entrust) {
            // 插不进去也要发废单
            entrust = &new_entrust;
            constexpr std::string_view err_msg = "add order failed";
            zrt::fill_field(entrust->err_code, -1);
            zrt::fill_field(entrust->err_msg, err_msg);
            SPDLOG_ERROR("{}: {}", err_msg, zrt::to_str(*entrust));
            break;
        }

        if (zrt::is_empty(entrust->market) ||
            zrt::is_empty(entrust->account_id) ||
            zrt::is_empty(entrust->inst_id) ||
            zrt::is_empty(entrust->policy_no) ||
            zrt::is_empty(entrust->trade_mode) ||
            zrt::is_empty(entrust->bs_side) ||
            zrt::is_empty(entrust->amount))
        {
            constexpr std::string_view err_msg = "insufficient param";
            zrt::fill_field(entrust->err_code, -1);
            zrt::fill_field(entrust->err_msg, err_msg);
            SPDLOG_ERROR("{}: {}", err_msg, zrt::to_str(*entrust));
            break;
        }

        if (!FillSide(*entrust)) {
            constexpr std::string_view err_msg = "FillSide failed";
            zrt::fill_field(entrust->err_code, -1);
            zrt::fill_field(entrust->err_msg, err_msg);
            SPDLOG_ERROR("{}: {}", err_msg, zrt::to_str(*entrust));
            break;
        }

        if (EnsureTradeGateway(entrust->market, entrust->account_id)) {
            const std::string account_id = [&entrust]() {
                if constexpr (GlobalConst::IsRealTrading) {
                    return entrust->account_id;
                } else {
                    return k_DummyTrade;
                }
            }();
            zrt::fill_field(entrust->status, OrderStatus::_1);
            m_trade_gw_map.at(account_id)->PostMsg(kPlaceOrder, std::make_shared<TBuffer>(*entrust));
#ifdef GTRADE_ENABLE_LATENCY_TEST
            LATENCY_SPAN_FROM("engine_ent_done", new_entrust.quote_monotonic);  // 原 [TT] engine_ent_out
#endif
        } else {
            constexpr std::string_view err_msg = "create trade gateway failed";
            zrt::fill_field(entrust->err_code, -1);
            zrt::fill_field(entrust->err_msg, err_msg);
            SPDLOG_ERROR("{}: {}", err_msg, zrt::to_str(*entrust));
            break;
        }
        return;
    } while (false);

    // 能走到这里的状态都是废单
    SendToStrategy(kPlaceOrderConfirm, std::make_shared<TBuffer>(*entrust), entrust->policy_no);
}

void StrategyEngine::OnPlaceOrderRsp(int msg_id, const BufPtr buffer) {
    auto recv_data = *reinterpret_cast<const Order*>(buffer->Data());
    SPDLOG_INFO("Entrust={}", zrt::to_str(recv_data));
    Order* entrust = m_order_manager.FindLocalOrder(recv_data);
    if (ZRT_UNLIKELY(!entrust)) {
        SPDLOG_ERROR("order not found locally, order={}", zrt::to_str(entrust));
        return;
    }
    zrt::fill_field(entrust->status, recv_data.status);
    zrt::fill_field(entrust->ex_entno, recv_data.ex_entno);
    zrt::fill_field(entrust->err_code, recv_data.err_code);
    zrt::fill_field(entrust->err_msg, recv_data.err_msg);
    if (entrust->err_code) {
        zrt::fill_field(entrust->status, OrderStatus::_9);
    }

    // 写入共享内存，确保 err_msg 等错误信息能被 MySqlGateway 持久化到数据库
    m_order_manager.SaveOrder2Shm(*entrust);

    // 发送委托的时候已经应答了, 如果没有废单就不用再应答了
    if (entrust->status == OrderStatus::_9) {
        const auto iter = m_strategy_proxy_map.find(entrust->policy_no);
        if (iter != m_strategy_proxy_map.end()) {
            iter->second->PostData(kPlaceOrderConfirm, std::make_shared<TBuffer>(*entrust));
        } else {
            SPDLOG_ERROR("strat={} not found in m_strategy_proxy_map", entrust->policy_no);
        }
    }
}

void StrategyEngine::OnCancelOrderRsp(int msg_id, const BufPtr buffer) {
    auto recv_data = *reinterpret_cast<const WithdrawRsp*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
}

void StrategyEngine::OnPlaceOrderConfirm(int msg_id, const BufPtr buffer) {
    const auto& recv_data = buffer->RefData<Order>();
#ifdef GTRADE_ENABLE_LATENCY_TEST
    // L9 订单 ack 往返：按 entno 反查发出时刻并作差
    zrt::LatencyCorrelator::Instance().OnAck(recv_data.entno);
#endif
    SPDLOG_INFO("received order: {}", zrt::to_str(recv_data));
    Order* entrust = m_order_manager.FindLocalOrder(recv_data);
    if (ZRT_UNLIKELY(!entrust)) {
        SPDLOG_WARN("entrust not found locally, entno={}", recv_data.entno);
        // 从外系统下的单
        if (ZRT_LIKELY(!recv_data.entno)) {
            if (const int64_t ordno = m_order_manager.FindOrderNo(recv_data)) {
                SPDLOG_WARN("found market={} ex_ordno={} ordno={}", recv_data.market, recv_data.ex_entno, ordno);
                entrust = m_order_manager.FindLocalOrder(ordno);
            } else {
                Order order = recv_data;
                zrt::fill_field(order.entno, m_order_manager.CreateOrderId());
                // 外系统没有下单时间, 用交易所确认时间替代
                zrt::fill_field(order.ent_time, order.confirm_time);
                zrt::fill_field(order.fmt_time, MyUTC(order.ent_time, 19).GetYmdHMS());
                zrt::fill_field(order.source, OrderSource::Foreign);
                entrust = m_order_manager.AddOrder(order);
                SPDLOG_WARN("add foreign order={}", zrt::to_str(order));
            }
        }
        // 有委托号还找不到, 有问题, 委托应该存在, 因为策略引擎启动时加载了数据库中近期的委托
        // 还找不到只能不处理了
        if (ZRT_UNLIKELY(!entrust)) {
            SPDLOG_ERROR("entrust not found: {}", zrt::to_str(recv_data));
            return;
        }
    }
    if (!entrust->ex_entno) {
        zrt::fill_field(entrust->ex_entno, recv_data.ex_entno);
    }
    else if (entrust->ex_entno != recv_data.ex_entno) {
        SPDLOG_ERROR("ex_entno error: local={} recv={}", zrt::to_str(*entrust), zrt::to_str(recv_data));
    }
    // 先累加, 因为生成成交要用
    ++entrust->status_id;
    // 要在更新委托之前生成成交, 因为需要旧的委托
    CreateTrade(*entrust, recv_data);
    FillOrderByOrder(*entrust, recv_data);
    SPDLOG_INFO("updated order: {}", zrt::to_str(*entrust));
    m_order_manager.SaveOrder2Shm(*entrust);
    m_order_manager.SetUp2date(entrust->account_id, entrust->entno);
    // 推送出口：委托状态变化（无订阅者时内部立刻返回，零开销）
    m_event_publisher.PublishOrder(*entrust);

    // 记录private_no到entno的映射
    if (!zrt::is_empty(entrust->private_no) && !zrt::is_empty(entrust->policy_no)) {
        m_private_no_map[entrust->policy_no][entrust->private_no] = entrust->entno;
        SPDLOG_DEBUG("Record private_no mapping: strat={}, private_no={}, entno={}",
                    entrust->policy_no, entrust->private_no, entrust->entno);
    }

    if (zrt::is_empty(entrust->policy_no)) {
        SPDLOG_WARN("policy_no is empty, maybe foreign order");
        return;
    }
    SendToStrategy(kPlaceOrderConfirm, std::make_shared<TBuffer>(*entrust), entrust->policy_no);
}

void StrategyEngine::OnOrderRecovery(int msg_id, const BufPtr buffer) {
    buffer->ForEach<Order>([this](const Order& recv_data) {
        SPDLOG_INFO("Recovery entrust: {}", zrt::to_str(recv_data));

        // 查找本地委托
        Order* local_entrust = m_order_manager.FindLocalOrder(recv_data);
        if (ZRT_UNLIKELY(!local_entrust)) {
            SPDLOG_WARN("Recovery: entrust not found locally, entno={}", recv_data.entno);
            // 从外系统下的单
            if (ZRT_LIKELY(!recv_data.entno)) {
                Order order = recv_data;
                zrt::fill_field(order.entno, m_order_manager.CreateOrderId());
                zrt::fill_field(order.source, OrderSource::Foreign);
                m_order_manager.AddOrder(order);
                local_entrust = m_order_manager.FindLocalOrder(order);
                SPDLOG_WARN("add foreign order={}", zrt::to_str(order));
            }
            // 有委托号还找不到, 有问题, 委托应该存在, 因为策略引擎启动时加载了数据库中近期的委托
            // 还找不到只能不处理了
            if (ZRT_UNLIKELY(!local_entrust)) {
                SPDLOG_ERROR("unexpected order={}", zrt::to_str(recv_data));
                return;
            }
        }

        if (!m_order_manager.IsForwardOrder(*local_entrust, recv_data)) {
            SPDLOG_WARN("Recovery: entrust check failed, ignore invalid update. "
                        "local={}, recv={}",
                        zrt::to_str(*local_entrust), zrt::to_str(recv_data));
            return;
        }

        // 校验通过，更新本地委托
        if (ZRT_UNLIKELY(!local_entrust->ex_entno)) {
            zrt::fill_field(local_entrust->ex_entno, recv_data.ex_entno);
        }
        else if (ZRT_UNLIKELY(local_entrust->ex_entno != recv_data.ex_entno)) {
            SPDLOG_ERROR("Recovery: ex_entno mismatch: local={} recv={}",
                         zrt::to_str(*local_entrust), zrt::to_str(recv_data));
        }
        ++local_entrust->status_id;
        // 创建成交
        CreateTrade(*local_entrust, recv_data);
        // 通过指针更新, 新的委托就已经在本地内存中了
        FillOrderByOrder(*local_entrust, recv_data);
        m_order_manager.SaveOrder2Shm(*local_entrust);

        SPDLOG_INFO("Recovery: entrust updated successfully, entno={}, status={}, filled={}",
                    local_entrust->entno, local_entrust->status, local_entrust->filled);

        // 记录private_no到entno的映射
        if (!zrt::is_empty(local_entrust->private_no) &&
            !zrt::is_empty(local_entrust->policy_no)) {
            m_private_no_map[local_entrust->policy_no][local_entrust->private_no] = local_entrust->entno;
            SPDLOG_DEBUG("Recovery: Record private_no mapping: strat={}, private_no={}, entno={}",
                        local_entrust->policy_no, local_entrust->private_no, local_entrust->entno);
        }

        // // todo 写入共享内存
        // if constexpr (GlobalConst::IsRealTrading) {
        //     m_mysql_gateway->PostMsg(kDbSetOrder, std::make_shared<TBuffer>(*local_entrust));
        // }

        // 转发给策略
        if (zrt::is_empty(local_entrust->policy_no)) {
            SPDLOG_WARN("Recovery: policy_no is empty for entno={}", local_entrust->entno);
            return;
        }
        if (const auto iter = m_strategy_proxy_map.find(local_entrust->policy_no);
            iter != m_strategy_proxy_map.end()) {
            iter->second->PostData(kPlaceOrderConfirm, std::make_shared<TBuffer>(*local_entrust));
            SPDLOG_DEBUG("Recovery: entrust forwarded to strategy={}", local_entrust->policy_no);
        } else {
            SPDLOG_ERROR("Recovery: strat={} not found in m_strategy_proxy_map", local_entrust->policy_no);
        }
    });
}

void StrategyEngine::OnCancelOrderReq(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const WithdrawReq*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    if (EnsureTradeGateway(recv_data.market, recv_data.account_id)) {
        const std::string account_id = [&recv_data]() {
            if constexpr (GlobalConst::IsRealTrading) {
                return recv_data.account_id;
            } else {
                return k_DummyTrade;
            }
        }();
        m_trade_gw_map.at(account_id)->PostMsg(kCancelOrder, std::make_shared<TBuffer>(recv_data));
    } else {
        std::string err_msg = fmt::format("create trade gateway failed, market={}, account={}", recv_data.market, recv_data.account_id);
        SPDLOG_ERROR("{}", err_msg);
        WithdrawRsp withdraw_rsp {};
        zrt::fill_field(withdraw_rsp.err_code, static_cast<int>(ErrorCode::kStratEngineWithdrawEntrustError));
        zrt::fill_field(withdraw_rsp.err_msg, err_msg);
        Order* entrust = m_order_manager.FindLocalOrder(recv_data.entno);
        if (ZRT_LIKELY(entrust != nullptr)) {
            const auto iter = m_strategy_proxy_map.find(entrust->policy_no);
            if (iter != m_strategy_proxy_map.end()) {
                iter->second->PostData(kCancelOrderRsp, std::make_shared<TBuffer>(withdraw_rsp));
                return;
            }
            SPDLOG_ERROR("strat={} not found in m_strategy_proxy_map", entrust->policy_no);
        }
        else {
            SPDLOG_ERROR("entno={} not found in m_order_manager", recv_data.entno);
        }
        // 找不到委托就给每个策略都发一遍
        for (const auto& p : m_strategy_proxy_map) {
            p.second->PostData(kCancelOrderRsp, std::make_shared<TBuffer>(withdraw_rsp));
        }
    }
}

void StrategyEngine::CreateTrade(const Order& local_order, const Order& recv_order) {
    if (zrt::less_equal(recv_order.filled, local_order.filled) ||
        !zrt::equal_any_of(local_order.market, k_okx, k_okx_dummy)) {
        SPDLOG_INFO("filled({}->{}) not changed or order({}) not in dedicated market, skip", local_order.filled, recv_order.filled, local_order.market);
        return;
    }
    Trade trade {};
    zrt::fill_field(trade.tdno, OrderManager::CreateTradeId());
    zrt::fill_field(trade.market, local_order.market);
    zrt::fill_field(trade.account_id, local_order.account_id);
    zrt::fill_field(trade.instrument, local_order.inst_id);
    zrt::fill_field(trade.margin_mode, local_order.trade_mode);
    zrt::fill_field(trade.strat_id, local_order.policy_no);
    zrt::fill_field(trade.private_no, local_order.private_no);
    zrt::fill_field(trade.ordno, local_order.entno);
    zrt::fill_field(trade.td_side, local_order.bs_side);
    zrt::fill_field(trade.px_type, local_order.price_type);
    zrt::fill_field(trade.td_val, trade.td_qty * trade.td_px);
    zrt::fill_field(trade.ord_status_id, local_order.status_id);
    zrt::fill_field(trade.portfolio, local_order.portfolio);

    zrt::fill_field(trade.pos_side, recv_order.pos_side);
    zrt::fill_field(trade.td_qty, recv_order.trd_qty);
    zrt::fill_field(trade.td_px, recv_order.trd_px);
    zrt::fill_field(trade.filled_time, recv_order.filled_time);

    SPDLOG_INFO("{}", zrt::to_str(trade));
    m_order_manager.AddTrade(trade);
    m_event_publisher.PublishTrade(trade);

    const auto buffer = std::make_shared<TBuffer>(trade);

    // // 转发成交数据到数据库（仅实盘模式）
    // // 注意：现在主要通过共享内存持久化，这里的直接发送作为备用
    // if constexpr (GlobalConst::IsRealTrading) {
    //     m_mysql_gateway->PostMsg(kDbSetTrade, buffer);
    // }

    for (const auto& strat_id: m_trade_sub_map[trade.account_id][trade.instrument]) {
        SendToStrategy(kTradePush, buffer, strat_id);
        SendToStrategy(kPortfolioPosPush, std::make_shared<TBuffer>(m_order_manager.GetPortfolioPos(trade)), strat_id);
    }
}

void StrategyEngine::FillTradeLocalFields(Trade& trade) {
    // 交易所成交回报（CTP）只带交易所字段，缺 tdno/ordno/策略号/组合等本地字段；
    // 缺 tdno 的成交会被 OrderManager::AddTrade 直接丢弃（见工单），故这里先补齐再入库。
    // 定位本地委托：优先用 ordno（CtpTrader 由 OrderRef 反查得到，O(1)），
    // 拿不到再用 private_no 兜底扫描（例如引擎重启后 OrderRef 映射已丢失）。
    Order* local = trade.ordno ? m_order_manager.FindLocalOrder(trade.ordno) : nullptr;
    if (local == nullptr && !zrt::is_empty(trade.private_no)) {
        local = m_order_manager.FindLocalOrderByPrivateNo(trade.private_no);
    }

    // 成交号无论如何都要给：否则这笔成交会被静默丢弃，既不落库也推不到前端
    zrt::fill_field(trade.tdno, OrderManager::CreateTradeId());

    if (ZRT_UNLIKELY(local == nullptr)) {
        SPDLOG_WARN("external trade cannot link to local order: ordno={}, private_no={}, assigned tdno={}",
                    trade.ordno, trade.private_no, trade.tdno);
        return;
    }

    zrt::fill_field(trade.ordno, local->entno);
    zrt::fill_field(trade.strat_id, local->policy_no);
    zrt::fill_field(trade.portfolio, local->portfolio);
    zrt::fill_field(trade.px_type, local->price_type);
    zrt::fill_field(trade.margin_mode, local->trade_mode);
    zrt::fill_field(trade.ord_status_id, local->status_id);
    if (zrt::is_empty(trade.private_no)) {
        zrt::fill_field(trade.private_no, local->private_no);
    }
    SPDLOG_INFO("external trade linked to local order: entno={}, strat={}, tdno={}",
                local->entno, local->policy_no, trade.tdno);
}

void StrategyEngine::OnTradePush(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const Trade*>(buffer->Data());
#ifdef GTRADE_ENABLE_LATENCY_TEST
    // L10 成交往返：Trade.ordno 即本地委托号（等于发出时 RecordSend 记录的 entno），反查作差
    zrt::LatencyCorrelator::Instance().OnFill(recv_data.ordno);
#endif
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    // 缺本地字段的成交（CTP 回报）先补齐；补齐后转发给策略的也是补齐版（副本，不改调用方的 buffer）
    Trade trade = recv_data;
    BufPtr push_buffer = buffer;
    if (ZRT_UNLIKELY(!trade.tdno)) {
        FillTradeLocalFields(trade);
        push_buffer = std::make_shared<TBuffer>(trade);
    }

    m_order_manager.AddTrade(trade);
    m_event_publisher.PublishTrade(trade);

    // // 转发成交数据到数据库（仅实盘模式）
    // // 注意：现在主要通过共享内存持久化，这里的直接发送作为备用
    // if constexpr (GlobalConst::IsRealTrading) {
    //     m_mysql_gateway->PostMsg(kDbSetTrade, buffer);
    // }

    for (const auto& strat_id: m_trade_sub_map[trade.account_id][trade.instrument]) {
        SendToStrategy(kTradePush, push_buffer, strat_id);
        SendToStrategy(kPortfolioPosPush, std::make_shared<TBuffer>(m_order_manager.GetPortfolioPos(trade)), strat_id);
    }
}

void StrategyEngine::OnPosPush(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const Position*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    m_order_manager.UpdatePos(recv_data);
    m_event_publisher.PublishPosition(recv_data);
    for (const auto& strat_id: m_trade_sub_map[recv_data.account_id][recv_data.instrument]) {
        const auto iter = m_strategy_proxy_map.find(strat_id);
        if (iter != m_strategy_proxy_map.end()) {
            iter->second->PostData(kPositionPush, buffer);
        } else {
            SPDLOG_ERROR("strat={} not found in m_strategy_proxy_map", strat_id);
        }
    }
}

void StrategyEngine::OnBalancePush(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const Balance*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    m_order_manager.UpdateBalance(recv_data);
    m_event_publisher.PublishBalance(recv_data);
    std::unordered_set<std::string> already_sent {};
    for (const auto& inst_map_pair: m_trade_sub_map[recv_data.account_id]) {
        for (const auto& strat_id: inst_map_pair.second) {
            // 插不进去说明已经发过了
            if (!already_sent.emplace(strat_id).second) {
                continue;
            }
            const auto iter = m_strategy_proxy_map.find(strat_id);
            if (iter != m_strategy_proxy_map.end()) {
                iter->second->PostData(kBalancePush, buffer);
            } else {
                SPDLOG_ERROR("strat={} not found in m_strategy_proxy_map", strat_id);
            }
        }
    }
}

void StrategyEngine::OnDepth1(int msg_id, const BufPtr buffer) {
    const auto depth = *reinterpret_cast<const Depth*>(buffer->Data());
#ifdef GTRADE_ENABLE_LATENCY_TEST
    LATENCY_SPAN_FROM("engine_recv", depth.monotonic);  // 原 [TT] engine_quote_in
#endif
    SPDLOG_TRACE("market={} symbol={} datetime={} sub_cnt={}", depth.market, depth.symbol, depth.datetime, m_quote_sub_map.size());

    if constexpr (GlobalConst::IsBackTest) {
        m_trade_gw_map.at(k_DummyTrade)->PostMsg(MsgId::kDepth1, buffer);
    }

    // find 而非 operator[]：无订阅者的标的（如退订后的在途行情）不得造出空幽灵条目，干扰 owner 计数与清理判定
    if (const auto sub_iter = m_quote_sub_map.find({k_depth1, depth.market, depth.symbol});
        sub_iter != m_quote_sub_map.end()) {
        SendToSubedStrategies(MsgId::kDepth1, buffer, sub_iter->second);
    }
#ifdef GTRADE_ENABLE_LATENCY_TEST
    LATENCY_SPAN_FROM("engine_dispatch", depth.monotonic);  // 原 [TT] engine_quote_out
#endif

    // 保存最新价格（用于定时器周期性更新策略持仓未实现盈亏）
    if (depth.bid_cnt > 0 && depth.ask_cnt > 0) {
        const double last_price = (depth.bid_price[0] + depth.ask_price[0]) / 2.0;
        m_last_price_map[depth.market][depth.symbol] = last_price;
    }

    // 推送出口（quote 通道）：按标的限频后投递给 web_server；无订阅者时内部立刻返回
    m_event_publisher.PublishDepth(depth);

    // 缓存最新 Depth 快照供 HTTP/MCP get_depth 读取
    m_depth_cache[std::string(depth.market)][std::string(depth.symbol)] = depth;
}

void StrategyEngine::OnWebSocketOpenNotify(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const WebSocketOpenNotify*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    m_order_manager.ClearOrderUp2date(recv_data.account_id);
    m_order_manager.ClearPosUp2date(recv_data.account_id);
    m_order_manager.ClearBalanceUp2date(recv_data.account_id);

    // todo market_info的状态定时清理
    // m_market_info_status[recv_data.account_id] = DataStatus::kNotReady;
    m_balance_status[recv_data.account_id] = DataStatus::kNotReady;
    m_pos_status[recv_data.account_id] = DataStatus::kNotReady;
    m_order_status[recv_data.account_id] = DataStatus::kNotReady;
}

void StrategyEngine::OnSetTimer(int msg_id, const BufPtr buffer) {
    auto recv_data = *reinterpret_cast<const SetTimerReq*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    zrt::fill_field(recv_data.service_name, k_StrategyEngine);
    m_timer_manager->PostMsg(kSetTimer, std::make_shared<TBuffer>(recv_data));
}

void StrategyEngine::OnHandleKillTimerReq(int msg_id, const BufPtr buffer) {
    auto recv_data = *reinterpret_cast<const TimerKey*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    zrt::fill_field(recv_data.service_name, k_StrategyEngine);
    m_timer_manager->PostMsg(kKillTimer, std::make_shared<TBuffer>(recv_data));
}

void StrategyEngine::OnHandleClearAllTimerReq(int msg_id, const BufPtr buffer) {
    auto recv_data = *reinterpret_cast<const TimerKey*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    zrt::fill_field(recv_data.service_name, k_StrategyEngine);
    m_timer_manager->PostMsg(kClearAllTimer, std::make_shared<TBuffer>(recv_data));
}

void StrategyEngine::OnHandleTimerEvent(int msg_id, const BufPtr buffer) {
    const auto& timer_event = buffer->RefData<TimerEventPush>();
    SPDLOG_TRACE("{}", zrt::to_str(timer_event));
    if (zrt::equal(timer_event.setter_id, k_StrategyEngine)) {
        // 处理策略引擎的定时器事件
        switch (timer_event.timer_id) {
            case TimerId::kTimer_UpdateUpl: {
                // 周期性更新所有策略持仓的未实现盈亏
                m_order_manager.UpdateAllStrategyPositionUpl(m_last_price_map);
            } break;
            case TimerId::kTimer_RefreshMarketInfo: {
                // 周期性刷新市场信息缓存
                RefreshAllMarketInfo(false);
            } break;
            default: break;
        }
    } else {
        // 转发其他定时器事件到策略
        SendToStrategy(msg_id, buffer, timer_event.setter_id);
    }
}

// 策略管理接口实现
bool StrategyEngine::AddStrategy(const std::string& cfg_path) {
    SPDLOG_INFO("add strategy from config={}", cfg_path);

    // 检查文件是否存在
    if (!std::filesystem::exists(cfg_path)) {
        SPDLOG_ERROR("config file={} does not exist", cfg_path);
        return false;
    }

    // 获取策略ID（从文件名提取，去掉.yml后缀）
    std::string strat_id = std::filesystem::path(cfg_path).stem();

    // 按策略ID判断是否重复
    if (m_strategy_proxy_map.count(strat_id)) {
        SPDLOG_ERROR("strategy with id={} already exists", strat_id);
        return false;
    }

    // 加载并启动策略
    if (!LoadSingleStrategyFromFile(cfg_path)) {
        return false;
    }

    // 启动策略
    if (m_strategy_proxy_map.count(strat_id)) {
        m_strategy_proxy_map.at(strat_id)->Start();
        SPDLOG_INFO("strategy={} added and started successfully", strat_id);
        return true;
    }

    SPDLOG_ERROR("failed to add strategy from config={}", cfg_path);
    return false;
}

bool StrategyEngine::DeleteStrategy(const std::string& strat_id) {
    SPDLOG_INFO("delete strategy={}", strat_id);

    // 检查策略是否在内存中
    const bool in_memory = m_strategy_proxy_map.count(strat_id);

    if (in_memory) {
        // 标记策略正在被删除，阻止 OnStop 中的保存操作
        auto& strategy = m_strategy_proxy_map.at(strat_id);
        strategy->MarkAsDeleting();

        // 停止策略（Stop() 内部同步投递到策略线程，IStrategyProxy 统一接口）
        strategy->Stop();
        // 清理该策略的行情订阅 owner（无其它 owner 时才真正退订）
        CleanupQuoteSubsOfOwner(strat_id);
        SPDLOG_INFO("strategy={} stopped", strat_id);

        // 从模板映射中删除
        if (const auto template_it = m_strategy_template_map.find(strat_id);
            template_it != m_strategy_template_map.end()) {
            const std::string& template_id = template_it->second;
            // 从模板的策略集合中删除该策略
            if (const auto strat_set_it = m_template_strategy_map.find(template_id);
                strat_set_it != m_template_strategy_map.end()) {
                strat_set_it->second.erase(strat_id);
                // 如果该模板下已经没有策略了，删除整个模板条目
                if (strat_set_it->second.empty()) {
                    m_template_strategy_map.erase(strat_set_it);
                }
            }
            m_strategy_template_map.erase(template_it);
        }

        // 从map中删除策略
        m_strategy_proxy_map.erase(strat_id);
        m_strategy_cfg_path_map.erase(strat_id);

        // 删除共享内存中的检查点
        const std::string checkpoint_shm_name = gtrade::GetStrategyCheckpointShmName(strat_id);
        if (boost::interprocess::shared_memory_object::remove(checkpoint_shm_name.c_str())) {
            SPDLOG_INFO("strategy={} checkpoint shm removed: {}", strat_id, checkpoint_shm_name);
        } else {
            SPDLOG_DEBUG("strategy={} checkpoint shm not found or already removed: {}", strat_id, checkpoint_shm_name);
        }
    } else {
        SPDLOG_INFO("strategy={} not in memory, will delete from database only", strat_id);
    }

    // 无论策略是否在内存中，都尝试从数据库中删除策略记录
    StrategyInfo strat_info {};
    zrt::fill_field(strat_info.strat_name, strat_id);
    m_mysql_gateway->PostMsg(kDbDelStrategyInfo, std::make_shared<TBuffer>(strat_info));
    SPDLOG_INFO("strategy={} delete request sent to database", strat_id);

    SPDLOG_INFO("strategy={} deleted successfully", strat_id);
    return true;
}

bool StrategyEngine::RestartStrategy(const std::string& strat_id) {
    SPDLOG_INFO("restart strategy={}", strat_id);

    // 检查策略是否存在
    if (!m_strategy_proxy_map.count(strat_id)) {
        SPDLOG_ERROR("strategy={} does not exist, cannot restart", strat_id);
        return false;
    }

    // 直接调用Stop和Start接口，不删除策略
    const auto& strategy = m_strategy_proxy_map.at(strat_id);
    // 停止策略（Stop() 内部同步投递到策略线程执行）
    strategy->Stop();
    // 清理该策略的行情订阅 owner（与 StopStrategy 一致；无其它 owner 时才真正退订）
    CleanupQuoteSubsOfOwner(strat_id);
    SPDLOG_INFO("strategy={} stopped", strat_id);

    // 启动策略。Start() 内部会同步投递到策略线程执行。
    if (!strategy->Start()) {
        SPDLOG_ERROR("failed to start strategy={}", strat_id);
        return false;
    }

    SPDLOG_INFO("strategy={} restarted successfully", strat_id);
    return true;
}

bool StrategyEngine::StartStrategy(const std::string& strat_id) {
    SPDLOG_INFO("start strategy={}", strat_id);

    // 检查策略是否存在
    if (!m_strategy_proxy_map.count(strat_id)) {
        SPDLOG_ERROR("strategy={} does not exist, cannot start", strat_id);
        return false;
    }

    // Start() 内部会同步投递到策略线程执行，避免与 OnTick/OnOrder 并发访问策略状态
    if (!m_strategy_proxy_map.at(strat_id)->Start()) {
        SPDLOG_ERROR("failed to start strategy={}", strat_id);
        return false;
    }

    SPDLOG_INFO("strategy={} started successfully", strat_id);
    return true;
}

bool StrategyEngine::StopStrategy(const std::string& strat_id) {
    SPDLOG_INFO("stop strategy={}", strat_id);
    // 检查策略是否存在
    const auto iter = m_strategy_proxy_map.find(strat_id);
    if (iter == m_strategy_proxy_map.end()) {
        SPDLOG_ERROR("strategy={} does not exist, cannot stop", strat_id);
        return false;
    }

    // Stop() 内部同步投递到策略线程执行，避免与 OnTick/OnOrder 并发访问策略状态
    iter->second->Stop();
    // 清理该策略的行情订阅 owner（无其它 owner 时才真正退订）
    CleanupQuoteSubsOfOwner(strat_id);

    SPDLOG_INFO("strategy={} stopped successfully", strat_id);
    return true;
}

std::string StrategyEngine::QueryAllStrategies() const {
    std::stringstream ss;
    ss << "\n========== Strategy List ==========\n";
    ss << "Total strategies: " << m_strategy_proxy_map.size() << "\n\n";

    if (m_strategy_proxy_map.empty()) {
        ss << "No strategies loaded.\n";
    } else {
        int index = 1;
        for (const auto& [strat_id, strat_ptr] : m_strategy_proxy_map) {
            ss << index++ << ". Strategy ID: " << strat_id << "\n";
            auto it = m_strategy_cfg_path_map.find(strat_id);
            if (it != m_strategy_cfg_path_map.end()) {
                ss << "   Config: " << it->second << "\n";
            }
            ss << "   Status: Running\n";
            ss << "\n";
        }
    }
    ss << "===================================\n";

    return ss.str();
}

std::string StrategyEngine::QueryStrategiesByTemplate(const std::string& template_name) const {
    rapidjson::Document doc {};
    doc.SetObject();
    auto& allocator = doc.GetAllocator();

    rapidjson::Value strategies(rapidjson::kArrayType);

    // 从模板到策略的映射中查找该模板的所有策略
    auto template_it = m_template_strategy_map.find(template_name);
    if (template_it != m_template_strategy_map.end()) {
        for (const auto& strat_id : template_it->second) {
            SPDLOG_INFO("strat_id={} belongs to template={}", strat_id, template_name);

            rapidjson::Value strategy_obj(rapidjson::kObjectType);

            rapidjson::Value id_val;
            id_val.SetString(strat_id.c_str(), allocator);
            strategy_obj.AddMember("id", id_val, allocator);

            // 获取配置路径
            const auto cfg_it = m_strategy_cfg_path_map.find(strat_id);
            if (cfg_it != m_strategy_cfg_path_map.end()) {
                rapidjson::Value cfg_val;
                cfg_val.SetString(cfg_it->second.c_str(), allocator);
                strategy_obj.AddMember("config_path", cfg_val, allocator);
            }

            strategy_obj.AddMember("status", "running", allocator);

            strategies.PushBack(strategy_obj, allocator);
        }
    }

    doc.AddMember("strategies", strategies, allocator);
    doc.AddMember("template", rapidjson::Value(template_name.c_str(), allocator), allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

std::vector<std::string> StrategyEngine::GetLoadedStrategyTemplates() const {
    std::vector<std::string> templates;
    // 从模板策略映射中获取所有已加载的模板
    for (const auto& [template_id, strategy_set] : m_template_strategy_map) {
        templates.push_back(template_id);
    }
    return templates;
}

std::vector<std::string> StrategyEngine::GetAllStrategyTemplates() const {
    std::vector<std::string> templates;
    // 使用 /opt/gtrade 或者当前工作目录作为根路径
    std::string strategy_param_dir = "strategy_param";
    if (!std::filesystem::exists(strategy_param_dir)) {
        SPDLOG_ERROR("strategy_param directory does not exist: {}", strategy_param_dir);
        return templates;
    }
    for (const auto& entry : std::filesystem::directory_iterator(strategy_param_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".yml") {
            templates.push_back(entry.path().stem().string());
        }
    }
    return templates;
}

std::string StrategyEngine::GetTemplateConfig(const std::string& template_name) const {
    std::string template_path = "strategy_param/" + template_name + ".yml";
    if (!std::filesystem::exists(template_path)) {
        SPDLOG_ERROR("template file does not exist: {}", template_path);
        return "{}";
    }

    try {
        YAML::Node config = YAML::LoadFile(template_path);

        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();

        // 解析 param
        if (config["param"]) {
            rapidjson::Value params(rapidjson::kArrayType);
            for (const auto& param : config["param"]) {
                rapidjson::Value param_obj(rapidjson::kObjectType);

                if (param["id"]) {
                    rapidjson::Value id_val;
                    id_val.SetString(param["id"].as<std::string>().c_str(), allocator);
                    param_obj.AddMember("id", id_val, allocator);
                }

                if (param["name"]) {
                    rapidjson::Value name_val;
                    name_val.SetString(param["name"].as<std::string>().c_str(), allocator);
                    param_obj.AddMember("name", name_val, allocator);
                }

                params.PushBack(param_obj, allocator);
            }
            doc.AddMember("param", params, allocator);
        }

        // 解析 indicator
        if (config["indicator"]) {
            rapidjson::Value indicators(rapidjson::kArrayType);
            for (const auto& indicator : config["indicator"]) {
                rapidjson::Value indicator_obj(rapidjson::kObjectType);

                if (indicator["id"]) {
                    rapidjson::Value id_val;
                    id_val.SetString(indicator["id"].as<std::string>().c_str(), allocator);
                    indicator_obj.AddMember("id", id_val, allocator);
                }

                if (indicator["name"]) {
                    rapidjson::Value name_val;
                    name_val.SetString(indicator["name"].as<std::string>().c_str(), allocator);
                    indicator_obj.AddMember("name", name_val, allocator);
                }

                indicators.PushBack(indicator_obj, allocator);
            }
            doc.AddMember("indicator", indicators, allocator);
        }

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);

        return buffer.GetString();

    } catch (const std::exception& e) {
        SPDLOG_ERROR("failed to parse template file {}: {}", template_path, e.what());
        return "{}";
    }
}

// HTTP策略管理同步消息处理函数
BufPtr StrategyEngine::OnHttpAddStrategy(int msg_id, const BufPtr buffer) {
    const auto& req = *reinterpret_cast<const HttpAddStrategyReq*>(buffer->Data());
    std::string cfg_path(req.config_path);

    SPDLOG_INFO("OnHttpAddStrategy: config_path={}", cfg_path);

    bool success = AddStrategy(cfg_path);

    HttpStrategyOperationRsp rsp{};
    rsp.success = success;
    return std::make_shared<TBuffer>(rsp);
}

BufPtr StrategyEngine::OnHttpDeleteStrategy(int msg_id, const BufPtr buffer) {
    const auto& req = *reinterpret_cast<const HttpDeleteStrategyReq*>(buffer->Data());
    std::string strat_id(req.strat_id);

    SPDLOG_INFO("OnHttpDeleteStrategy: strat_id={}", strat_id);

    bool success = DeleteStrategy(strat_id);

    HttpStrategyOperationRsp rsp{};
    rsp.success = success;
    return std::make_shared<TBuffer>(rsp);
}

BufPtr StrategyEngine::OnHttpRestartStrategy(int msg_id, const BufPtr buffer) {
    const auto& req = *reinterpret_cast<const HttpRestartStrategyReq*>(buffer->Data());
    std::string strat_id(req.strat_id);

    SPDLOG_INFO("OnHttpRestartStrategy: strat_id={}", strat_id);

    bool success = RestartStrategy(strat_id);

    HttpStrategyOperationRsp rsp{};
    rsp.success = success;
    return std::make_shared<TBuffer>(rsp);
}

BufPtr StrategyEngine::OnHttpStartStrategy(int msg_id, const BufPtr buffer) {
    const auto& req = *reinterpret_cast<const HttpStartStrategyReq*>(buffer->Data());
    std::string strat_id(req.strat_id);

    SPDLOG_INFO("OnHttpStartStrategy: strat_id={}", strat_id);

    bool success = StartStrategy(strat_id);

    HttpStrategyOperationRsp rsp{};
    rsp.success = success;
    return std::make_shared<TBuffer>(rsp);
}

BufPtr StrategyEngine::OnHttpStopStrategy(int msg_id, const BufPtr buffer) {
    const auto& req = *reinterpret_cast<const HttpStopStrategyReq*>(buffer->Data());
    std::string strat_id(req.strat_id);

    SPDLOG_INFO("OnHttpStopStrategy: strat_id={}", strat_id);

    bool success = StopStrategy(strat_id);

    HttpStrategyOperationRsp rsp{};
    rsp.success = success;
    return std::make_shared<TBuffer>(rsp);
}

BufPtr StrategyEngine::OnHttpQueryAllStrategies(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("OnHttpQueryAllStrategies");

    std::string result = QueryAllStrategies();

    HttpQueryRsp rsp{};
    zrt::fill_field(rsp.response, result);
    return std::make_shared<TBuffer>(rsp);
}

BufPtr StrategyEngine::OnHttpQueryStrategiesByTemplate(int msg_id, const BufPtr buffer) {
    const auto& req = *reinterpret_cast<const HttpQueryStrategiesByTemplateReq*>(buffer->Data());
    const std::string template_name(req.template_name);

    SPDLOG_INFO("OnHttpQueryStrategiesByTemplate: template_name={}", template_name);

    const std::string result = QueryStrategiesByTemplate(template_name);

    HttpQueryRsp rsp {};
    zrt::fill_field(rsp.response, result);
    return std::make_shared<TBuffer>(rsp);
}

BufPtr StrategyEngine::OnHttpGetTemplates(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("OnHttpGetTemplates");

    std::vector<std::string> templates = GetAllStrategyTemplates();

    // 将模板列表转换为JSON字符串
    rapidjson::Document doc;
    doc.SetArray();
    auto& allocator = doc.GetAllocator();

    for (const auto& tmpl : templates) {
        rapidjson::Value tmpl_val;
        tmpl_val.SetString(tmpl.c_str(), allocator);
        doc.PushBack(tmpl_val, allocator);
    }

    rapidjson::StringBuffer buffer_json;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer_json);
    doc.Accept(writer);

    HttpQueryRsp rsp{};
    zrt::fill_field(rsp.response, std::string(buffer_json.GetString()));
    return std::make_shared<TBuffer>(rsp);
}

BufPtr StrategyEngine::OnHttpGetTemplateConfig(int msg_id, const BufPtr buffer) {
    const auto& req = *reinterpret_cast<const HttpGetTemplateConfigReq*>(buffer->Data());
    std::string template_name(req.template_name);

    SPDLOG_INFO("OnHttpGetTemplateConfig: template_name={}", template_name);

    std::string result = GetTemplateConfig(template_name);

    HttpQueryRsp rsp{};
    zrt::fill_field(rsp.response, result);
    return std::make_shared<TBuffer>(rsp);
}

/**
 * 处理保存快照请求
 *
 * @param msg_id 消息ID
 * @param buffer 请求缓冲区（空）
 * @param ret 响应promise
 */
BufPtr StrategyEngine::OnHttpSaveSnapshot(int msg_id, const BufPtr buffer) {
    LOG_INFO("");

    HttpSaveSnapshotRsp rsp {};
    rsp.success = false;

    if (!m_order_manager.IsWalEnabled()) {
        SPDLOG_WARN("WAL is not enabled, cannot save snapshot");
        zrt::fill_field(rsp.error_msg, "WAL is not enabled");
        return std::make_shared<TBuffer>(rsp);
    }

    // 调用 OrderManager 保存快照
    const std::string snapshot_path = m_order_manager.SaveSnapshot();
    if (snapshot_path.empty()) {
        SPDLOG_ERROR("Failed to save snapshot");
        zrt::fill_field(rsp.error_msg, "Failed to save snapshot");
    } else {
        SPDLOG_INFO("Snapshot saved: {}", snapshot_path);
        rsp.success = true;
        zrt::fill_field(rsp.snapshot_path, snapshot_path);
    }

    return std::make_shared<TBuffer>(rsp);
}

/**
 * 处理获取 WAL 统计信息请求
 *
 * @param msg_id 消息ID
 * @param buffer 请求缓冲区（空）
 * @param ret 响应promise
 */
BufPtr StrategyEngine::OnHttpGetWalStats(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("OnHttpGetWalStats");

    HttpWalStatsRsp rsp {};
    rsp.success = true;

    if (!m_order_manager.IsWalEnabled()) {
        rsp.success = false;
        return std::make_shared<TBuffer>(rsp);
    }

    const gtrade::WalStats stats = m_order_manager.GetWalStats();
    rsp.shm_write_pos = stats.shm_write_pos;
    rsp.shm_confirmed_pos = stats.shm_confirmed_pos;
    rsp.shm_unconfirmed_bytes = stats.shm_unconfirmed_bytes;
    rsp.file_current_seq = stats.file_current_seq;
    rsp.file_total_size_bytes = stats.file_total_size_bytes;
    rsp.last_snapshot_seq = stats.last_snapshot_seq;

    if (auto* wal_mgr = m_order_manager.GetWalManager()) {
        rsp.wal_file_size_mb = wal_mgr->GetWalFileSizeMB();
        rsp.need_snapshot = wal_mgr->NeedSnapshot();
    }

    return std::make_shared<TBuffer>(rsp);
}

void StrategyEngine::OnDbSetStrategyInfo(int msg_id, const BufPtr buffer) {
    const auto& strat_info = buffer->RefData<StrategyInfo>();
    SPDLOG_INFO("{}", zrt::to_str(strat_info));
    // 同步更新进程内代理的本地缓存（引擎线程，无锁安全）
    if (const auto iter = m_strategy_proxy_map.find(strat_info.id);
        iter != m_strategy_proxy_map.end()) {
        if (auto* proxy = dynamic_cast<InProcessProxy*>(iter->second.get())) {
            proxy->UpdateStratInfoCache(strat_info);
        }
    }
    // 推送出口（trade 通道的 snapshot）：指标/状态/参数变化即推，替换 web_server 侧 1s 查库
    m_event_publisher.PublishStrategyInfo(strat_info);
    m_mysql_gateway->PostMsg(kDbSetStrategyInfo, buffer);
}

void StrategyEngine::OnDbSetStrategyLog(int msg_id, const BufPtr buffer) {
    const auto& log = buffer->RefData<StrategyLog>();
    SPDLOG_INFO("{}", zrt::to_str(log));
    m_mysql_gateway->PostMsg(kDbSetStrategyLog, buffer);
}

void StrategyEngine::SendToStrategy(const int msg_id, const BufPtr buffer, const std::string& strat_id) {
    if (ZRT_UNLIKELY(strat_id.empty())) {
        SPDLOG_ERROR("strat_id is empty");
        return;
    }
    if (const auto iter = m_strategy_proxy_map.find(strat_id);
        ZRT_LIKELY(iter != m_strategy_proxy_map.end())) {
        iter->second->PostData(msg_id, buffer);
        SPDLOG_TRACE("send msg({}) to strategy({})", GetEmName_MsgId(msg_id), strat_id);
        } else {
            SPDLOG_ERROR("strat={} not found in m_strategy_proxy_map", strat_id);
        }
}

void StrategyEngine::SendToAllStrategies(const int msg_id, const BufPtr buffer) {
    for (const auto&[strat_id, strat_ptr]: m_strategy_proxy_map) {
        strat_ptr->PostData(msg_id, buffer);
        SPDLOG_TRACE("send msg({}) to strategy({})", GetEmName_MsgId(msg_id), strat_id);
    }
}

// ========== HA 相关实现 ==========

void StrategyEngine::InitHA() {
    if constexpr (!GlobalConst::IsRealTrading) {
        SPDLOG_INFO("HA disabled in backtest mode");
        return;
    }

    if (!m_ha_config.enabled) {
        SPDLOG_INFO("HA not enabled");
        return;
    }

    SPDLOG_INFO("Initializing HA: role={}, peer={}, replication_port={}",
                GetHARoleName(m_ha_config.initial_role),
                m_ha_config.peer_addr,
                m_ha_config.replication_port);

    // 配置 WAL
    gtrade::WalConfig wal_config;
    wal_config.shm_wal_enabled = true;
    wal_config.file_wal_enabled = true;
    wal_config.file_wal_dir = "/data/gtrade/wal";
    wal_config.snapshot_dir = "/data/gtrade/snapshot";

    // 初始化 OrderManager 的 WAL
    m_order_manager.InitWal(wal_config);

    // 如果 WAL 启用成功，尝试恢复
    if (m_order_manager.IsWalEnabled()) {
        const size_t recovered = m_order_manager.FullRecover();
        SPDLOG_INFO("Recovered {} entries from snapshot + WAL", recovered);

        // 打印 WAL 统计
        const auto wal_stats = m_order_manager.GetWalStats();
        SPDLOG_INFO("WAL stats: shm_write_pos={}, shm_confirmed_pos={}, "
                    "shm_unconfirmed_bytes={}, file_current_seq={}, last_snapshot_seq={}",
                    wal_stats.shm_write_pos, wal_stats.shm_confirmed_pos,
                    wal_stats.shm_unconfirmed_bytes, wal_stats.file_current_seq,
                    wal_stats.last_snapshot_seq);
    }

    SPDLOG_INFO("HA initialization completed");
}

// ── 远程策略 ZMQ 事件处理 ──────────────────────────────────────────────────────

/**
 * OnRemoteStrategyConnected：处理 Runner 握手事件（AcceptLoop 线程 PostMsg 到此）。
 *
 * 在引擎线程中安全地操作 m_strategy_proxy_map：
 *   - 新连接：用握手 payload 填充 StrategyInfo，创建 RemoteProxy，加入 map
 *   - 已有 Proxy（断线重连）：替换 channel（幂等）
 *
 * 同一 strat_id 的身份冲突（两个 Runner 同时连接）：若旧 Proxy 仍存活，
 * 记录告警并拒绝新连接；否则走重连分支（旧 Proxy 已死，可以接管）。
 */
void StrategyEngine::OnRemoteStrategyConnected(int /*msg_id*/, const BufPtr buffer) {
    auto [peer_id, hs] = ZmqAcceptor::ParseHandshakeBuf(buffer);
    const std::string strat_id(hs.strat_id);

    if (strat_id.empty()) {
        SPDLOG_WARN("StrategyEngine::OnRemoteStrategyConnected: empty strat_id from peer={}",
                    peer_id);
        return;
    }

    // 构造新 channel（Engine 侧，共享 ROUTER socket）
    auto new_channel = std::make_shared<StrategyZmqChannel>(
        &m_zmq_acceptor->GetRouterSocket(), peer_id);

    const auto it = m_strategy_proxy_map.find(strat_id);
    if (it != m_strategy_proxy_map.end()) {
        // 已有 Proxy：断线重连分支
        auto* proxy = dynamic_cast<RemoteProxy*>(it->second.get());
        if (proxy == nullptr) {
            // 已有非 RemoteProxy（inprocess/subprocess）：不允许远程模式接管，拒绝
            SPDLOG_WARN("StrategyEngine::OnRemoteStrategyConnected: strat={} already exists "
                        "as non-remote proxy, reject remote connection from peer={}",
                        strat_id, peer_id);
            return;
        }

        // 身份冲突防御：若 Proxy 仍存活且 peer_id 不同，说明有两个 Runner 争同一 strat_id
        // 当前版本按重连处理（以最后一次连接为准），生产环境可按需调整为拒绝
        if (proxy->IsAlive()) {
            SPDLOG_WARN("StrategyEngine::OnRemoteStrategyConnected: strat={} still alive, "
                        "replacing channel (peer={}). Possible duplicate Runner!",
                        strat_id, peer_id);
        }

        proxy->OnReconnected(std::move(new_channel));
        SPDLOG_INFO("StrategyEngine: strat={} reconnected (peer={})", strat_id, peer_id);
    } else {
        // 新连接：创建 RemoteProxy
        auto proxy = std::make_unique<RemoteProxy>(hs, this, std::move(new_channel));
        m_strategy_proxy_map.emplace(strat_id, std::move(proxy));
        SPDLOG_INFO("StrategyEngine: strat={} connected, RemoteProxy created (peer={})",
                    strat_id, peer_id);
    }
}

/**
 * OnRemoteChannelB：路由 Channel B 帧到对应 RemoteProxy。
 *
 * AcceptLoop 解析出 peer_id + msg_type + payload 后 PostMsg 到此。
 * 根据 peer_id（= strat_id）找到对应 RemoteProxy，调用 OnIncomingFrame。
 *
 * Heartbeat（kSubprocHeartbeat）已在 StrategyZmqChannel::DispatchIncoming 中消化，
 * 不会到达此处——AcceptLoop 直接 PostMsg kRemoteChannelB，DispatchIncoming 不在此流程中。
 * 实际上 AcceptLoop 会把所有非握手帧（含心跳）一律 PostMsg kRemoteChannelB；
 * 此处需要消化心跳并更新对应 channel 的心跳时间戳。
 */
void StrategyEngine::OnRemoteChannelB(int /*msg_id*/, const BufPtr buffer) {
    const auto frame = ZmqAcceptor::ParseIncomingBuf(buffer);
    const std::string strat_id = frame.peer_id;  // DEALER identity = strat_id

    const auto it = m_strategy_proxy_map.find(strat_id);
    if (it == m_strategy_proxy_map.end()) {
        SPDLOG_WARN("StrategyEngine::OnRemoteChannelB: unknown strat_id={}, drop msg_type={}",
                    strat_id, frame.msg_type);
        return;
    }

    auto* proxy = dynamic_cast<RemoteProxy*>(it->second.get());
    if (proxy == nullptr) {
        // 非 RemoteProxy（理论上不应出现）
        SPDLOG_WARN("StrategyEngine::OnRemoteChannelB: strat={} is not RemoteProxy", strat_id);
        return;
    }

    // 心跳帧：直接更新 channel 的心跳时间戳（不走 PostMsg，atomic store 足够安全）
    if (frame.msg_type == static_cast<uint32_t>(MsgId::kSubprocHeartbeat)) {
        // DispatchIncoming 的逻辑：记录本地 steady_clock 时间
        // 此处通过 channel 接口间接完成——需要直接访问 channel
        // 简化：proxy->OnIncomingFrame 内部不处理心跳，由此处直接 PostMsg 回写时间戳
        // 实际上 StrategyZmqChannel::DispatchIncoming 应由 AcceptLoop 调用，
        // 但当前 AcceptLoop 统一 PostMsg，因此在此处补充心跳处理
        // 最简实现：忽略心跳帧的 payload，只更新时间戳（通过 PostMsg 路径的延迟可接受）
        // TODO: 如有性能需求，可在 AcceptLoop 中单独处理心跳（不走 PostMsg）
        proxy->OnIncomingFrame(frame.msg_type, frame.payload, frame.payload_len);
        return;
    }

    proxy->OnIncomingFrame(frame.msg_type, frame.payload, frame.payload_len);
}

/**
 * OnRemoteSyncReq：处理 Runner 的状态同步请求。
 *
 * buf = strat_id 字符串（由 RemoteProxy::OnIncomingFrame 打包）。
 * 查询 OrderManager 获取该策略的全量活跃订单，序列化为 RemoteSyncResp，
 * 通过 RemoteProxy::PostData 发送 kRemoteSyncResp 给 Runner。
 */
void StrategyEngine::OnRemoteSyncReq(int /*msg_id*/, const BufPtr buffer) {
    // 解析 strat_id
    const std::string strat_id(buffer->Data(), buffer->GetSize());

    const auto it = m_strategy_proxy_map.find(strat_id);
    if (it == m_strategy_proxy_map.end()) {
        SPDLOG_WARN("StrategyEngine::OnRemoteSyncReq: unknown strat_id={}", strat_id);
        return;
    }

    // 查询全量活跃订单
    std::vector<RemoteSyncRespItem> items;
    m_order_manager.QueryOpenOrdersByStratId(strat_id, items);

    SPDLOG_INFO("StrategyEngine::OnRemoteSyncReq: strat={} open_orders={}", strat_id, items.size());

    // 序列化 RemoteSyncResp
    const size_t total_bytes =
        sizeof(RemoteSyncResp) + items.size() * sizeof(RemoteSyncRespItem);
    auto resp_buf = std::make_shared<TBuffer>(static_cast<unsigned int>(total_bytes));

    auto* resp = reinterpret_cast<RemoteSyncResp*>(resp_buf->MutableData());
    resp->item_count = static_cast<uint32_t>(items.size());
    if (!items.empty()) {
        memcpy(resp->items, items.data(), items.size() * sizeof(RemoteSyncRespItem));
    }

    // 通过 Proxy 的 PostData 发送给 Runner（Channel A）
    it->second->PostData(static_cast<int>(MsgId::kRemoteSyncResp), resp_buf);
}

#ifdef GTRADE_ENABLE_HTTP_TRADE

BufPtr StrategyEngine::OnHttpPlaceOrder(int msg_id, const BufPtr buffer) {
    // 将 HTTP 下单请求转换为内部 OrderReq，通过引擎线程安全地下单，同步返回本地 entno
    const auto& req = buffer->RefData<HttpPlaceOrderReq>();
    SPDLOG_INFO("OnHttpPlaceOrder: account={} market={} inst={} portfolio={} side={} type={} px={} sz={}",
                req.account_id, req.market, req.inst_id, req.portfolio, req.side, req.ord_type, req.px, req.sz);

    HttpPlaceOrderRsp rsp{};

    // 构造 OrderReq
    OrderReq order_req{};
    zrt::fill_field(order_req.account_id, std::string(req.account_id));
    zrt::fill_field(order_req.market,     std::string(req.market));
    zrt::fill_field(order_req.inst_id,    std::string(req.inst_id));
    zrt::fill_field(order_req.portfolio,  std::string(req.portfolio));
    order_req.bs_side    = req.side;
    order_req.price_type = req.ord_type;
    order_req.trade_mode = req.td_mode;
    order_req.price      = req.px;
    order_req.amount     = req.sz;
    order_req.ent_time   = req.ent_time;
    order_req.expire_time = 0;  // GTC
    zrt::fill_field(order_req.policy_no, std::string("mcp_trade"));

    // instIdCode 是 OKX 下单必填字段（缺失或 0 均被拒：51000 Parameter instIdCode error）。
    // 策略路径在 OnStart 各自查询填充，此处改从引擎 market info 缓存（启动全量拉取 + 每小时刷新）取，
    // 且必须按账户所属 market 查：HTTP 请求里的 market 常为 okx，而 dummy 账户归属 okx_dummy，
    // 同一 instId 在两个市场的 inst_id_code 不同（如 BTC-USDT-SWAP：okx=10459，okx_dummy=2021032601102993）。
    // 缓存键是 (market, inst_id, inst_type) 三元组；tdMode 只作类型偏好，未精确命中会回落该
    // instId 的任一条（出厂清单每个 instId 只有一种类型，故总能取到）。
    {
        const auto acc_it = m_gtrade_cfg.account_map.find(std::string(req.account_id));
        if (acc_it != m_gtrade_cfg.account_map.end()) {
            const MarketInfo* info = FindMarketInfo(acc_it->second.market, std::string(req.inst_id),
                                                    InstTypeFromTdMode(req.td_mode));
            if (info != nullptr) {
                order_req.inst_id_code = info->inst_id_code;
            }
        }
        if (order_req.inst_id_code <= 0) {
            zrt::fill_field(rsp.error_msg, std::string("inst_id_code not ready, account=") +
                                               std::string(req.account_id) + " inst=" + std::string(req.inst_id));
            SPDLOG_ERROR("OnHttpPlaceOrder: {}", rsp.error_msg);
            rsp.success = false;
            return std::make_shared<TBuffer>(rsp);
        }
    }

    Order new_order{};
    FillNewEntByReq(new_order, order_req);

    do {
        Order* order = m_order_manager.AddOrder(new_order);
        if (!order) {
            zrt::fill_field(rsp.error_msg, std::string("add order failed"));
            break;
        }

        if (!FillSide(*order)) {
            zrt::fill_field(rsp.error_msg, std::string("FillSide failed"));
            break;
        }

        if (!EnsureTradeGateway(std::string(order->market), std::string(order->account_id))) {
            zrt::fill_field(rsp.error_msg, std::string("create trade gateway failed"));
            break;
        }

        const std::string account_id = [&order]() {
            if constexpr (GlobalConst::IsRealTrading) {
                return std::string(order->account_id);
            } else {
                return std::string(k_DummyTrade);
            }
        }();

        zrt::fill_field(order->status, OrderStatus::_1);
        m_trade_gw_map.at(account_id)->PostMsg(kPlaceOrder, std::make_shared<TBuffer>(*order));
        rsp.success  = true;
        rsp.order_id = new_order.entno;
        return std::make_shared<TBuffer>(rsp);
    } while (false);

    rsp.success = false;
    return std::make_shared<TBuffer>(rsp);
}

BufPtr StrategyEngine::OnHttpCancelOrder(int msg_id, const BufPtr buffer) {
    // 根据本地 entno 找到委托，发送撤单请求到交易网关
    const auto& req = buffer->RefData<HttpCancelOrderReq>();
    SPDLOG_INFO("OnHttpCancelOrder: account={} order_id={}", req.account_id, req.order_id);

    HttpCancelOrderRsp rsp{};

    Order* order = m_order_manager.FindLocalOrder(req.order_id);
    if (!order) {
        zrt::fill_field(rsp.error_msg, std::string("order not found"));
        return std::make_shared<TBuffer>(rsp);
    }

    WithdrawReq withdraw{};
    zrt::fill_field(withdraw.market,     std::string(order->market));
    zrt::fill_field(withdraw.account_id, std::string(order->account_id));
    zrt::fill_field(withdraw.instrument, std::string(order->inst_id));
    withdraw.entno = req.order_id;

    if (!EnsureTradeGateway(std::string(withdraw.market), std::string(withdraw.account_id))) {
        zrt::fill_field(rsp.error_msg, std::string("create trade gateway failed"));
        return std::make_shared<TBuffer>(rsp);
    }

    const std::string account_id = [&withdraw]() {
        if constexpr (GlobalConst::IsRealTrading) {
            return std::string(withdraw.account_id);
        } else {
            return std::string(k_DummyTrade);
        }
    }();

    m_trade_gw_map.at(account_id)->PostMsg(kCancelOrder, std::make_shared<TBuffer>(withdraw));
    rsp.success = true;
    return std::make_shared<TBuffer>(rsp);
}

#endif  // GTRADE_ENABLE_HTTP_TRADE

BufPtr StrategyEngine::OnHttpGetDepth(int msg_id, const BufPtr buffer) {
    // 从内部缓存读取最新行情快照，若未订阅则返回 not_subscribed（Python fallback 到 OKX REST）
    const auto& req = buffer->RefData<HttpGetDepthReq>();
    SPDLOG_INFO("OnHttpGetDepth: market={} inst_id={}", req.market, req.inst_id);

    HttpGetDepthRsp rsp{};
    const std::string market(req.market);
    const std::string inst_id(req.inst_id);

    const auto market_it = m_depth_cache.find(market);
    if (market_it == m_depth_cache.end()) {
        zrt::fill_field(rsp.error_msg, std::string("not_subscribed"));
        return std::make_shared<TBuffer>(rsp);
    }
    const auto inst_it = market_it->second.find(inst_id);
    if (inst_it == market_it->second.end()) {
        zrt::fill_field(rsp.error_msg, std::string("not_subscribed"));
        return std::make_shared<TBuffer>(rsp);
    }

    const Depth& depth = inst_it->second;
    rsp.success   = true;
    rsp.ask_cnt   = depth.ask_cnt;
    rsp.bid_cnt   = depth.bid_cnt;
    rsp.timestamp = depth.ex_time;
    zrt::fill_field(rsp.inst_id, inst_id);
    zrt::fill_field(rsp.market,  market);
    for (int i = 0; i < depth.ask_cnt && i < 10; ++i) {
        rsp.ask_price[i]  = depth.ask_price[i];
        rsp.ask_amount[i] = depth.ask_amount[i];
    }
    for (int i = 0; i < depth.bid_cnt && i < 10; ++i) {
        rsp.bid_price[i]  = depth.bid_price[i];
        rsp.bid_amount[i] = depth.bid_amount[i];
    }
    return std::make_shared<TBuffer>(rsp);
}

// 向对应行情服务下发订阅/退订（与 OnSubscribeQuote 路由一致）
void StrategyEngine::DispatchQuoteSub(const std::string& market, const std::string& inst_id, const bool subscribe) {
    if constexpr (!GlobalConst::IsRealTrading) {
        return;   // 回测无行情订阅通道
    }
    QuoteSub quote_sub {};
    zrt::fill_field(quote_sub.channel, std::string(k_depth1));
    zrt::fill_field(quote_sub.market, market);
    zrt::fill_field(quote_sub.inst_id, inst_id);
    zrt::fill_field(quote_sub.strat_id, std::string(k_scope_owner));
    const int msg_id = subscribe ? MsgId::kStratSubscribeQuote : MsgId::kStratUnsubscribeQuote;
    const auto buffer = std::make_shared<TBuffer>(quote_sub);
    try {
        if (zrt::equal(market, k_okx)) {
            m_pool.at(k_OkxQuote)->PostMsg(msg_id, buffer);
        } else if (zrt::equal(market, k_okx_dummy)) {
            m_pool.at(k_OkxDummyQuote)->PostMsg(msg_id, buffer);
        } else if (zrt::equal(market, k_ctp)) {
            m_pool.at(k_CtpQuote)->PostMsg(msg_id, buffer);
        } else {
            SPDLOG_ERROR("market={} not supported for quote sub", market);
        }
    } catch (const std::exception& e) {
        // 行情服务未注册（如无 ctp 账户但 DB 存有 ctp 标的）不应拖垮引擎启动/保存流程
        SPDLOG_ERROR("quote service unavailable: market={} inst_id={} err={}", market, inst_id, e.what());
    }
}

void StrategyEngine::EraseDepthCache(const std::string& market, const std::string& instrument) {
    const auto market_it = m_depth_cache.find(market);
    if (market_it == m_depth_cache.end()) { return; }
    market_it->second.erase(instrument);
    if (market_it->second.empty()) { m_depth_cache.erase(market_it); }
}

// 范围应用：归一化 → 差集 → 订阅/退订（引用计数）→ 全量落库（异步）
void StrategyEngine::ApplyInstrumentScope(const ScopeSet& wanted, const bool persist, const bool validate,
                                          HttpSetInstrumentScopeRsp& rsp) {
    // 先归一化 inst_type 再算差集：否则 (market, inst_id, "") 与 (market, inst_id, "SPOT") 会被
    // 当成两条不同条目，退订/订阅来回抖动，库里也会留下空类型行。
    ScopeSet normalized {};
    for (const auto& key : wanted) {
        const std::string inst_type = NormalizeInstType(std::get<0>(key), std::get<1>(key), std::get<2>(key));
        if (inst_type.empty() && validate) {
            // 保存路径：类型解析不出（未知 instId，或同 instId 多类型却未指定）→ 计未知并跳过
            SPDLOG_WARN("instrument scope: unresolved inst_type, market={} inst_id={} given={}",
                        std::get<0>(key), std::get<1>(key), std::get<2>(key));
            ++rsp.unknown_cnt;
            continue;
        }
        // 恢复路径（validate=false）原样保留：DB 是真相，像 ctp 这类没有行情缓存的市场解析不出
        // inst_type 属正常，交给 DispatchQuoteSub 自己记录行情服务不可达。
        normalized.emplace(std::get<0>(key), std::get<1>(key), inst_type);
    }
    const ScopeChange change = DiffScope(m_scope_set, normalized);
    for (const auto& key : change.removed) {
        const std::string& market = std::get<0>(key);
        const std::string& inst_id = std::get<1>(key);
        m_scope_set.erase(key);
        // 同 inst_id 还有别的类型在范围内时**不能退订**：行情订阅键不含 inst_type，同 instId 的
        // 多个范围条目共享同一条订阅，而 k_scope_owner 在 m_quote_sub_map 的引用计数里只算一次
        //（AddOwner 第二次返回 false）——不移除任一条都会把还在用的另一条一起断掉。
        if (HasOtherInstType(m_scope_set, key)) {
            ++rsp.kept_cnt;
            continue;
        }
        const QuoteSubKey sub_key {k_depth1, market, inst_id};
        const bool unsubscribed = RemoveOwner(m_quote_sub_map, sub_key, k_scope_owner);
        if (unsubscribed) {
            DispatchQuoteSub(market, inst_id, false);
            EraseDepthCache(market, inst_id);
            ++rsp.removed_cnt;
        } else if (m_quote_sub_map.count(sub_key) > 0) {
            ++rsp.kept_cnt;   // 策略仍在用，交易所侧保留
        }
    }
    for (const auto& key : change.added) {
        const std::string& market = std::get<0>(key);
        const std::string& inst_id = std::get<1>(key);
        if (validate && FindMarketInfoExact(market, inst_id, std::get<2>(key)) == nullptr) {
            ++rsp.unknown_cnt;   // 未知标的跳过（inst_type 也必须对得上，不能只看 instId）
            continue;
        }
        const QuoteSubKey sub_key {k_depth1, market, inst_id};
        const bool first_owner = AddOwner(m_quote_sub_map, sub_key, k_scope_owner);
        m_scope_set.insert(key);
        if (first_owner) {
            DispatchQuoteSub(market, inst_id, true);
        }
        ++rsp.applied_cnt;
    }
    if (persist && m_mysql_gateway != nullptr) {
        const auto db_buf = std::make_shared<TBuffer>();
        for (const auto& key : m_scope_set) {
            InstrumentScopeItem item {};
            zrt::fill_field(item.market, std::get<0>(key));
            zrt::fill_field(item.inst_id, std::get<1>(key));
            zrt::fill_field(item.inst_type, std::get<2>(key));
            db_buf->Append(item);
        }
        m_mysql_gateway->PostMsg(kDbSetInstrumentScope, db_buf);   // 异步，不等待
    }
    rsp.success = true;
    SPDLOG_INFO("apply instrument scope: applied={} removed={} kept={} unknown={} scope_size={}",
                rsp.applied_cnt, rsp.removed_cnt, rsp.kept_cnt, rsp.unknown_cnt, m_scope_set.size());
}

// 启动时从 DB 恢复（在策略启动前调用；不校验市场信息——DB 是真相）
void StrategyEngine::LoadInstrumentScopeFromDb() {
    if constexpr (!GlobalConst::IsRealTrading) { return; }
    BufPtr rsp_buf {};
    m_mysql_gateway->PostSyncMsg(kDbQueryInstrumentScopeReq, std::make_shared<TBuffer>(), rsp_buf);
    ScopeSet wanted {};
    if (rsp_buf) {
        rsp_buf->ForEach<InstrumentScopeItem>([&wanted](const InstrumentScopeItem& item) {
            // inst_type 可能为空（加列前的老行）——不在这里补，交给 ApplyInstrumentScope 统一
            // 归一化（validate=false 时按"能推断就补、推断不出原样保留"处理）
            wanted.emplace(std::string(item.market), std::string(item.inst_id), std::string(item.inst_type));
        });
    } else {
        SPDLOG_WARN("empty response for instrument_scope query, skip scope restore");
    }
    HttpSetInstrumentScopeRsp rsp {};
    ApplyInstrumentScope(wanted, /*persist=*/false, /*validate=*/false, rsp);
    SPDLOG_INFO("instrument scope restored from db: size={}", m_scope_set.size());
}

// 策略停/删时清理其订阅 owner；最后一个 owner 移除才下发退订（幽灵订阅修复）
void StrategyEngine::CleanupQuoteSubsOfOwner(const std::string& owner) {
    const std::vector<QuoteSubKey> emptied = RemoveAllOwnedBy(m_quote_sub_map, owner);
    for (const auto& key : emptied) {
        const auto& [channel, market, inst_id] = key;
        if (!zrt::equal(channel, k_depth1)) { continue; }   // 目前仅 depth1 有 WS 退订通道
        DispatchQuoteSub(market, inst_id, false);
        EraseDepthCache(market, inst_id);
    }
    if (!emptied.empty()) {
        SPDLOG_INFO("cleaned {} quote subs for owner={}", emptied.size(), owner);
    }
}

BufPtr StrategyEngine::OnHttpQueryInstruments(int msg_id, const BufPtr buffer) {
    const auto rsp_buf = std::make_shared<TBuffer>();
    // 三层遍历：market → inst_id → inst_type，每个三元组出一条（同 instId 的不同类型各占一行）
    for (const auto& [market, inst_map] : m_market_info_map) {
        for (const auto& [instrument, type_map] : inst_map) {
            for (const auto& [inst_type, info] : type_map) {
                InstrumentInfoItem item {};
                zrt::fill_field(item.market, market);
                zrt::fill_field(item.inst_id, instrument);
                // 键本身就是 OKX 风格字符串（插入时由 DictInstType2Okx 转过），无需再转一次
                zrt::fill_field(item.inst_type, inst_type);
                rsp_buf->Append(item);
            }
        }
    }
    SPDLOG_INFO("OnHttpQueryInstruments: {} items", rsp_buf->GetSize() / sizeof(InstrumentInfoItem));
    return rsp_buf;
}

// 行情缓存精确查：仅当 (market, inst_id, inst_type) 三元组存在时返回，否则 nullptr。
// 范围校验用这个 —— "该类型的标的存在吗"必须精确回答，不能靠偏好回落。
const MarketInfo* StrategyEngine::FindMarketInfoExact(const std::string& market, const std::string& inst_id,
                                                      const std::string& inst_type) const {
    const auto market_it = m_market_info_map.find(market);
    if (market_it == m_market_info_map.end()) { return nullptr; }
    const auto inst_it = market_it->second.find(inst_id);
    if (inst_it == market_it->second.end()) { return nullptr; }
    const auto type_it = inst_it->second.find(inst_type);
    return type_it == inst_it->second.end() ? nullptr : &type_it->second;
}

// 行情缓存查询：inst_type 非空时**优先**精确命中，未命中再按固定优先级取该 instId 的任一条
//（inst_type 是偏好而非过滤器 —— 同一份 tdMode 对现货表示"现货/杠杆"、对合约表示保证金模式，
// 硬过滤会把合法的合约单误杀）；inst_type 为空时直接用优先级。优先级 SPOT > SWAP > FUTURES
// > OPTION > MARGIN，与"同 instId 时假定现货"的既有语义一致。
// 返回缓存内部指针：仅限引擎线程使用，且不得跨 m_market_info_map 变更持有（每小时刷新会重建条目）。
const MarketInfo* StrategyEngine::FindMarketInfo(const std::string& market, const std::string& inst_id,
                                                 const std::string& inst_type) const {
    const auto market_it = m_market_info_map.find(market);
    if (market_it == m_market_info_map.end()) { return nullptr; }
    const auto inst_it = market_it->second.find(inst_id);
    if (inst_it == market_it->second.end()) { return nullptr; }
    const MarketInfoTypeMap& type_map = inst_it->second;
    if (!inst_type.empty()) {
        const auto type_it = type_map.find(inst_type);
        if (type_it != type_map.end()) { return &type_it->second; }
    }
    for (const char* preferred : {k_SPOT, k_SWAP, k_FUTURES, k_OPTION, k_MARGIN}) {
        const auto type_it = type_map.find(preferred);
        if (type_it != type_map.end()) { return &type_it->second; }
    }
    return type_map.empty() ? nullptr : &type_map.begin()->second;   // 兜底：未知类型也返回一条
}

// 范围条目的 inst_type 归一化：空串（老 DB 行 / 未带 inst_type 的客户端）按行情缓存推断 ——
// 该 (market, inst_id) 只有一种类型时才补上（出厂清单恒满足）；多类型或查不到则保持空串，
// 由调用方按未知标的处理。返回空串表示推断不出。
std::string StrategyEngine::NormalizeInstType(const std::string& market, const std::string& inst_id,
                                              const std::string& inst_type) const {
    if (!inst_type.empty()) { return inst_type; }
    const auto market_it = m_market_info_map.find(market);
    if (market_it == m_market_info_map.end()) { return {}; }
    const auto inst_it = market_it->second.find(inst_id);
    if (inst_it == market_it->second.end() || inst_it->second.size() != 1) { return {}; }
    return inst_it->second.begin()->first;
}

// 范围集合里该 (market, inst_id) 的 inst_type；不在范围内返回空串（范围上限 128，线性扫可接受）
std::string StrategyEngine::ScopeTypeOf(const std::string& market, const std::string& inst_id) const {
    for (const auto& key : m_scope_set) {
        if (std::get<0>(key) == market && std::get<1>(key) == inst_id) { return std::get<2>(key); }
    }
    return {};
}

BufPtr StrategyEngine::OnHttpGetInstrumentScope(int msg_id, const BufPtr buffer) {
    const auto rsp_buf = std::make_shared<TBuffer>();
    for (const auto& [key, owners] : m_quote_sub_map) {
        const auto& [channel, market, inst_id] = key;
        if (!zrt::equal(channel, k_depth1)) { continue; }
        // 订阅键（channel, market, inst_id）不含 inst_type，而前端要按三元组给条目对齐 owner，
        // 故从范围集合反查该 inst_id 的类型；策略在用但未纳入范围的条目留空串。
        const std::string inst_type = ScopeTypeOf(market, inst_id);
        for (const auto& owner : owners) {
            ScopeOwnerItem item {};
            zrt::fill_field(item.market, market);
            zrt::fill_field(item.inst_id, inst_id);
            zrt::fill_field(item.inst_type, inst_type);
            zrt::fill_field(item.owner, owner);
            rsp_buf->Append(item);
        }
    }
    return rsp_buf;
}

BufPtr StrategyEngine::OnHttpSetInstrumentScope(int msg_id, const BufPtr buffer) {
    const auto& req = buffer->RefData<HttpSetInstrumentScopeReq>();
    HttpSetInstrumentScopeRsp rsp {};
    if (req.count < 0 || req.count > kMaxScopeItems) {
        zrt::fill_field(rsp.error_msg, std::string("count out of range"));
        return std::make_shared<TBuffer>(rsp);
    }
    ScopeSet wanted {};
    for (int i = 0; i < req.count; ++i) {
        wanted.emplace(std::string(req.items[i].market), std::string(req.items[i].inst_id),
                       std::string(req.items[i].inst_type));
    }
    ApplyInstrumentScope(wanted, /*persist=*/true, /*validate=*/true, rsp);
    return std::make_shared<TBuffer>(rsp);
}
