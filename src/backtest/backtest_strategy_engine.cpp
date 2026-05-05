//
// BacktestStrategyEngine: 独立的回测策略引擎实现
//
// 与实盘 StrategyEngine 完全独立，只复用 StrategyBase 接口。
// 所有消息处理器从实盘引擎的回测分支（if constexpr else）复制而来，
// 去掉了实盘专属逻辑（MySqlGateway、MessageServer、OkxWs、HA、SharedMem 等）。
//

#include <filesystem>
#include <dlfcn.h>
#include "backtest_strategy_engine.h"
#include "strategy_plugin.h"
#include "zrtools/zrt_compare.h"
#include "i_strategy_engine_dump.h"
#include "zrtools/zrt_define.h"
#include "err_code.h"
#include "type_define_dump.h"
#include "i_timer_manager_dump.h"
#include "dummy_trade.h"
#include "global.h"
#include "my_utc.h"
#include "i_client_dump.h"

// 策略通过自注册工厂加载，无需在此 include 各策略头文件
#include "strategy_factory.h"

// =========================================================
// 构造
// =========================================================

BacktestStrategyEngine::BacktestStrategyEngine(ServiceMap& pool, const GTradeConfig& gtrade_cfg):
m_pool(pool),
m_gtrade_cfg(gtrade_cfg)
{
    // 回测引擎始终运行在同步回测线程上
    SetThread(zrt::EnginePool::GetInstance().GetNamedThread(k_BackTestThread));
}

// =========================================================
// Init: 注册所有消息处理器
// =========================================================

bool BacktestStrategyEngine::Init() {
    SPDLOG_INFO("{}", __PRETTY_FUNCTION__);

    m_qry_srv = m_pool.at(k_QueryServer).get();
    m_timer_manager = m_pool.at(k_TimerManager).get();

    // 创建 KLineManager（K线查询缓存与拼接）
    m_kline_manager = std::make_unique<KLineManager>(m_gtrade_cfg, *this, dynamic_cast<QueryServer&>(*m_qry_srv));

    // 创建事件源管理器（回测模式独有）
    m_event_source_manager = std::make_unique<EventSourceManager>(*this, m_gtrade_cfg);

    InstallDefaultHandler(DefaultMsgHandler);

    // 通知消息（回测 no-op）
    ZRT_ADD_HANDLER(kNotifyMessage, BacktestStrategyEngine::OnNotifyMsg);

    // 订阅
    ZRT_ADD_HANDLER(kStratSubscribeQuote, BacktestStrategyEngine::OnSubscribeQuote);
    ZRT_ADD_HANDLER(kStratSubscribeTrade, BacktestStrategyEngine::OnSubscribeTrade);
    ZRT_ADD_HANDLER(kPlaceOrder, BacktestStrategyEngine::OnPlaceOrderReq);
    ZRT_ADD_HANDLER(kCancelOrder, BacktestStrategyEngine::OnCancelOrderReq);

    // 行情推送
    ZRT_ADD_HANDLER(kDepth1, BacktestStrategyEngine::OnDepth1);

    // 交易推送
    ZRT_ADD_HANDLER(kPlaceOrderRsp, BacktestStrategyEngine::OnPlaceOrderRsp);
    ZRT_ADD_HANDLER(kCancelOrderRsp, BacktestStrategyEngine::OnCancelOrderRsp);
    ZRT_ADD_HANDLER(kPlaceOrderConfirm, BacktestStrategyEngine::OnPlaceOrderConfirm);
    ZRT_ADD_HANDLER(kTradePush, BacktestStrategyEngine::OnTradePush);
    ZRT_ADD_HANDLER(kPositionPush, BacktestStrategyEngine::OnPosPush);
    ZRT_ADD_HANDLER(kBalancePush, BacktestStrategyEngine::OnBalancePush);

    // K线查询
    ZRT_ADD_HANDLER(kQueryKLineReq, BacktestStrategyEngine::OnQueryKLineReq);
    ZRT_ADD_HANDLER(kQueryKLinePatchRsp, BacktestStrategyEngine::OnQueryKLinePatchRsp);
    ZRT_ADD_HANDLER(kQueryKLineRsp, BacktestStrategyEngine::OnQueryKLineRsp);

    // 委托查询
    ZRT_ADD_HANDLER(kQueryOrderReq, BacktestStrategyEngine::OnHandleQueryOrderReq);
    ZRT_ADD_HANDLER(kQueryOrderByPrivateNoReq, BacktestStrategyEngine::OnQueryOrderByPrivateNoReq);

    // 定时器
    ZRT_ADD_HANDLER(kTimerEvent, BacktestStrategyEngine::OnHandleTimerEvent);
    ZRT_ADD_HANDLER(kSetTimer, BacktestStrategyEngine::OnSetTimer);
    ZRT_ADD_HANDLER(kKillTimer, BacktestStrategyEngine::OnHandleKillTimerReq);
    ZRT_ADD_HANDLER(kClearAllTimer, BacktestStrategyEngine::OnHandleClearAllTimerReq);

    // 指标订阅
    ZRT_ADD_HANDLER(kStratSubscribeKLine, BacktestStrategyEngine::OnSubscribeKLine);
    ZRT_ADD_HANDLER(kStratSubscribeKLineOpen, BacktestStrategyEngine::OnSubscribeKLineOpen);
    ZRT_ADD_HANDLER(kStratSubscribeKLineClose, BacktestStrategyEngine::OnSubscribeKLineClose);

    // 指标推送
    ZRT_ADD_HANDLER(kIndicatorKLinePush, BacktestStrategyEngine::OnIndicatorKlinePush);
    ZRT_ADD_HANDLER(kIndicatorKLineOpenPush, BacktestStrategyEngine::OnIndicatorKlineOpenPush);
    ZRT_ADD_HANDLER(kIndicatorKLineClosePush, BacktestStrategyEngine::OnIndicatorKlineClosePush);

    // 策略信息（回测 no-op）
    ZRT_ADD_HANDLER(kDbSetStrategyInfo, BacktestStrategyEngine::OnDbSetStrategyInfo);
    ZRT_ADD_HANDLER(kDbSetStrategyLog, BacktestStrategyEngine::OnDbSetStrategyLog);

    return true;
}

// =========================================================
// Start: 加载策略、初始化事件源、运行回测
// =========================================================

bool BacktestStrategyEngine::Start() {
    SPDLOG_INFO("");

    // 从配置文件加载策略
    SPDLOG_INFO("Loading strategies from config files");
    LoadStrategyCfgFromFile();

    // 初始化事件源管理器
    if (!m_event_source_manager->Initialize()) {
        SPDLOG_ERROR("Failed to initialize EventSourceManager");
        return false;
    }

    if (!m_event_source_manager->Start()) {
        SPDLOG_ERROR("Failed to start EventSourceManager");
        return false;
    }

    // 启动所有策略
    for (const auto& p : m_strategy_map) {
        p.second->Start();
        SPDLOG_INFO("strat={} started", p.first);
    }

    // 运行回测主循环
    m_event_source_manager->RunBacktest();
    SPDLOG_INFO("Backtest completed via EventSourceManager");

    // 回测结束，通知所有策略
    for (const auto& [strat_id, strat_ptr] : m_strategy_map) {
        strat_ptr->Stop();
        SPDLOG_INFO("strat={} stopped", strat_id);
    }

    GlobalControl::is_running = false;

    return true;
}

// =========================================================
// 策略加载
// =========================================================

std::shared_ptr<StrategyBase> BacktestStrategyEngine::LoadStrategyFromSo(
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

bool BacktestStrategyEngine::LoadSingleStrategyFromFile(const std::string& strat_cfg_path) {
    SPDLOG_INFO("load strategy from file: {}", strat_cfg_path);
    try {
        YAML::Node strat_yml = YAML::LoadFile(strat_cfg_path);
        // 策略ID从文件名提取（去掉.yml后缀）
        std::string strat_id = std::filesystem::path(strat_cfg_path).stem();
        const std::string strat_template_id = strat_yml[k_strat_template_id].as<std::string>();

        // 按策略ID判断是否已存在
        if (m_strategy_map.count(strat_id)) {
            SPDLOG_WARN("strategy with id={} already exists, cannot add duplicate", strat_id);
            return false;
        }

        bool loaded = false;

        // 优先检查 so_path 字段：存在则走动态插件加载路径，无需修改引擎代码
        if (strat_yml[k_so_path]) {
            const std::string so_path = strat_yml[k_so_path].as<std::string>();
            auto strat_ptr = LoadStrategyFromSo(so_path, strat_id);
            if (!strat_ptr) {
                return false;
            }
            loaded = LoadStrategy(strat_id, strat_ptr, strat_yml);
        }
        else if (StrategyFactory::Contains(strat_template_id)) {
            // 已通过自注册机制注册的策略，由工厂统一创建
            auto strat_ptr = StrategyFactory::Create(strat_template_id, m_gtrade_cfg, this, strat_id);
            loaded = LoadStrategy(strat_id, strat_ptr, strat_yml);
        }
        else {
            SPDLOG_ERROR("strat_template_id={} not found in config={}", strat_template_id, strat_cfg_path);
            return false;
        }

        if (loaded) {
            m_strategy_cfg_path_map[strat_id] = strat_cfg_path;
            m_strategy_template_map[strat_id] = strat_template_id;
            m_template_strategy_map[strat_template_id].insert(strat_id);
        }
        return loaded;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("load strategy config={} failed: {}", strat_cfg_path, e.what());
        return false;
    }
}

bool BacktestStrategyEngine::LoadStrategyCfgFromFile() {
    for (const auto& strat_cfg_path : m_gtrade_cfg.strat_cfg_path_vec) {
        LoadSingleStrategyFromFile(strat_cfg_path);
    }
    return true;
}

// =========================================================
// 消息转发
// =========================================================

void BacktestStrategyEngine::OnDefaultMsg(int msg_id, const BufPtr buffer) {
    SPDLOG_INFO("{}", msg_id);
}

void BacktestStrategyEngine::SendToStrategy(const int msg_id, const BufPtr buffer, const std::string& strat_id) {
    if (ZRT_UNLIKELY(strat_id.empty())) {
        SPDLOG_ERROR("strat_id is empty");
        return;
    }
    if (const auto iter = m_strategy_map.find(strat_id);
        ZRT_LIKELY(iter != m_strategy_map.end())) {
        iter->second->PostMsg(msg_id, buffer);
        SPDLOG_TRACE("send msg({}) to strategy({})", GetEmName_MsgId(msg_id), strat_id);
    } else {
        SPDLOG_ERROR("strat={} not found in m_strategy_map", strat_id);
    }
}

void BacktestStrategyEngine::SendToAllStrategies(const int msg_id, const BufPtr buffer) {
    for (const auto& [strat_id, strat_ptr] : m_strategy_map) {
        strat_ptr->PostMsg(msg_id, buffer);
        SPDLOG_TRACE("send msg({}) to strategy({})", GetEmName_MsgId(msg_id), strat_id);
    }
}

// =========================================================
// 订阅
// =========================================================

void BacktestStrategyEngine::OnSubscribeQuote(int msg_id, const BufPtr buffer) {
    const QuoteSub quote_sub = *reinterpret_cast<const QuoteSub*>(buffer->Data());
    m_quote_sub_map[{quote_sub.channel, quote_sub.market, quote_sub.inst_id}].emplace(quote_sub.strat_id);
    SPDLOG_INFO("{} sub_cnt={}", zrt::to_str(quote_sub), m_quote_sub_map.size());
    // 回测模式：通过事件源管理器订阅 Depth 数据
    m_event_source_manager->SubscribeDepth({quote_sub.market, quote_sub.inst_id});
}

void BacktestStrategyEngine::OnSubscribeKLine(int msg_id, const BufPtr buffer) {
    const KLineSub recv_data = *reinterpret_cast<const KLineSub*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    m_kline_sub_map[{recv_data.market, recv_data.instrument, recv_data.coefficient, recv_data.scale}].emplace(recv_data.strat_id);
    // 回测模式：通过事件源管理器订阅 K线 数据
    m_event_source_manager->SubscribeKLine({recv_data.market, recv_data.instrument, recv_data.coefficient, recv_data.scale});
}

void BacktestStrategyEngine::OnSubscribeKLineOpen(int msg_id, const BufPtr buffer) {
    const KLineSub recv_data = *reinterpret_cast<const KLineSub*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    m_kline_open_sub_map[{recv_data.market, recv_data.instrument, recv_data.coefficient, recv_data.scale}].emplace(recv_data.strat_id);
    m_event_source_manager->SubscribeKLine({recv_data.market, recv_data.instrument, recv_data.coefficient, recv_data.scale});
}

void BacktestStrategyEngine::OnSubscribeKLineClose(int msg_id, const BufPtr buffer) {
    const KLineSub recv_data = *reinterpret_cast<const KLineSub*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    m_kline_close_sub_map[{recv_data.market, recv_data.instrument, recv_data.coefficient, recv_data.scale}].emplace(recv_data.strat_id);
    m_event_source_manager->SubscribeKLine({recv_data.market, recv_data.instrument, recv_data.coefficient, recv_data.scale});
}

bool BacktestStrategyEngine::EnsureTradeGateway(const std::string& market, const std::string& account_id) {
    // 回测模式：始终使用 DummyTrade
    if (!m_trade_gw_map.count(k_DummyTrade)) {
        m_trade_gw_map.emplace(k_DummyTrade, std::make_shared<DummyTrade>(m_gtrade_cfg, k_DummyTrade, *this));
        m_trade_gw_map.at(k_DummyTrade)->Init();
        m_trade_gw_map.at(k_DummyTrade)->Start();
        SPDLOG_INFO("create trade_gateway {}", k_DummyTrade);
    }
    return true;
}

void BacktestStrategyEngine::OnSubscribeTrade(int msg_id, const BufPtr buffer) {
    const TradeSub trade_sub = buffer->RefData<TradeSub>();
    SPDLOG_INFO("{}", zrt::to_str(trade_sub));
    m_trade_sub_map[trade_sub.account_id][trade_sub.inst_id].emplace(trade_sub.strat_id);
    if (!EnsureTradeGateway(trade_sub.market, trade_sub.account_id)) {
        SPDLOG_ERROR("create trade gateway failed, market={}, account={}", trade_sub.market, trade_sub.account_id);
        return;
    }
    // 回测模式不需要查询 market_info, balance, position
}

// =========================================================
// 行情
// =========================================================

void BacktestStrategyEngine::OnDepth1(int msg_id, const BufPtr buffer) {
    const auto depth = *reinterpret_cast<const Depth*>(buffer->Data());
    SPDLOG_TRACE("market={} symbol={} datetime={} sub_cnt={}", depth.market, depth.symbol, depth.datetime, m_quote_sub_map.size());

    // 回测模式：将行情转发给 DummyTrade 进行模拟撮合
    if (m_trade_gw_map.count(k_DummyTrade)) {
        m_trade_gw_map.at(k_DummyTrade)->PostMsg(MsgId::kDepth1, buffer);
    }

    // 分发给订阅的策略
    SendToSubedStrategies(MsgId::kDepth1, buffer, m_quote_sub_map[{k_depth1, depth.market, depth.symbol}]);
}

// =========================================================
// 下单 / 撤单
// =========================================================

void BacktestStrategyEngine::FillNewEntByReq(Order& dst, const OrderReq& src) {
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
    zrt::fill_field(dst.fmt_time, MyUTC(dst.ent_time, 19).GetYmdHMS());
    zrt::fill_field(dst.entno, OrderManager::CreateOrderId());
    // 先置为废单, 如果发出去就改为正报
    zrt::fill_field(dst.status, OrderStatus::_9);
    zrt::fill_field(dst.remain, src.amount);
    zrt::fill_field(dst.update_time, src.ent_time);
    zrt::fill_field(dst.source, OrderSource::Local);
    zrt::fill_field(dst.portfolio, zrt::is_empty(src.portfolio) ? src.policy_no : src.portfolio);
}

void BacktestStrategyEngine::FillOrderByOrder(Order& dst, const Order& src) {
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

// 自动开平仓（带参数版本）
bool BacktestStrategyEngine::FillSide(Order& order, const std::string_view account_id, const std::string_view instrument,
                                       const char trade_mode, const char pos_side, const double entamt) {
    if (!order.bs_side) {
        SPDLOG_ERROR("bs_side is required");
        return false;
    }
    if (!order.oc_side && !order.pos_side) {
        const Position& hold = m_order_manager.GetPos(account_id, instrument, trade_mode, pos_side);
        if (zrt::greater_equal(hold.available, entamt)) {
            zrt::fill_field(order.oc_side, PosEffect::Close);
        } else {
            zrt::fill_field(order.oc_side, PosEffect::Open);
        }
    }
    if (order.bs_side == TradeSide::Buy) {
        if (!order.pos_side) {
            zrt::fill_field(order.pos_side, order.oc_side == PosEffect::Open ? PosSide::Long : PosSide::Short);
        } else if (!order.oc_side) {
            zrt::fill_field(order.oc_side, order.pos_side == PosSide::Long ? PosEffect::Open : PosEffect::Close);
        } else {
            if (order.oc_side == PosEffect::Open && order.pos_side != PosSide::Long) {
                return false;
            } else if (order.oc_side == PosEffect::Close && order.pos_side != PosSide::Short) {
                return false;
            }
        }
    } else if (order.bs_side == TradeSide::Sell) {
        if (!order.pos_side) {
            zrt::fill_field(order.pos_side, order.oc_side == PosEffect::Open ? PosSide::Short : PosSide::Long);
        } else if (!order.oc_side) {
            zrt::fill_field(order.oc_side, order.pos_side == PosSide::Long ? PosEffect::Close : PosEffect::Open);
        } else {
            if (order.oc_side == PosEffect::Open && order.pos_side != PosSide::Short) {
                return false;
            } else if (order.oc_side == PosEffect::Close && order.pos_side != PosSide::Long) {
                return false;
            }
        }
    }
    return true;
}

// 自动开平仓（简化版本，从委托自身字段推断）
bool BacktestStrategyEngine::FillSide(Order& order) {
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

void BacktestStrategyEngine::OnPlaceOrderReq(int msg_id, const BufPtr buffer) {
    const auto& recv_data = buffer->RefData<OrderReq>();
    SPDLOG_INFO("EntrustReq={}", zrt::to_str(recv_data));

    Order new_entrust {};
    FillNewEntByReq(new_entrust, recv_data);
    Order* entrust {};
    do {
        entrust = m_order_manager.AddOrder(new_entrust);
        if (!entrust) {
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
            // 回测模式：始终路由到 DummyTrade
            zrt::fill_field(entrust->status, OrderStatus::_1);
            m_trade_gw_map.at(k_DummyTrade)->PostMsg(kPlaceOrder, std::make_shared<TBuffer>(*entrust));
        } else {
            constexpr std::string_view err_msg = "create trade gateway failed";
            zrt::fill_field(entrust->err_code, -1);
            zrt::fill_field(entrust->err_msg, err_msg);
            SPDLOG_ERROR("{}: {}", err_msg, zrt::to_str(*entrust));
            break;
        }
        return;
    } while (false);

    // 走到这里的都是废单
    SendToStrategy(kPlaceOrderConfirm, std::make_shared<TBuffer>(*entrust), entrust->policy_no);
}

void BacktestStrategyEngine::OnPlaceOrderRsp(int msg_id, const BufPtr buffer) {
    auto recv_data = *reinterpret_cast<const Order*>(buffer->Data());
    SPDLOG_INFO("Entrust={}", zrt::to_str(recv_data));
    Order* entrust = m_order_manager.FindLocalOrder(recv_data);
    if (ZRT_UNLIKELY(!entrust)) {
        SPDLOG_ERROR("order not found locally, order={}", zrt::to_str(recv_data));
        return;
    }
    zrt::fill_field(entrust->status, recv_data.status);
    zrt::fill_field(entrust->ex_entno, recv_data.ex_entno);
    zrt::fill_field(entrust->err_code, recv_data.err_code);
    zrt::fill_field(entrust->err_msg, recv_data.err_msg);
    if (entrust->err_code) {
        zrt::fill_field(entrust->status, OrderStatus::_9);
    }

    // 回测模式不写共享内存

    // 废单才需要再通知策略
    if (entrust->status == OrderStatus::_9) {
        const auto iter = m_strategy_map.find(entrust->policy_no);
        if (iter != m_strategy_map.end()) {
            iter->second->PostMsg(kPlaceOrderConfirm, std::make_shared<TBuffer>(*entrust));
        } else {
            SPDLOG_ERROR("strat={} not found in m_strategy_map", entrust->policy_no);
        }
    }
}

void BacktestStrategyEngine::OnCancelOrderReq(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const WithdrawReq*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    if (EnsureTradeGateway(recv_data.market, recv_data.account_id)) {
        // 回测模式：始终路由到 DummyTrade
        m_trade_gw_map.at(k_DummyTrade)->PostMsg(kCancelOrder, std::make_shared<TBuffer>(recv_data));
    } else {
        std::string err_msg = fmt::format("create trade gateway failed, market={}, account={}", recv_data.market, recv_data.account_id);
        SPDLOG_ERROR("{}", err_msg);
        WithdrawRsp withdraw_rsp {};
        zrt::fill_field(withdraw_rsp.err_code, static_cast<int>(ErrorCode::kStratEngineWithdrawEntrustError));
        zrt::fill_field(withdraw_rsp.err_msg, err_msg);
        Order* entrust = m_order_manager.FindLocalOrder(recv_data.entno);
        if (ZRT_LIKELY(entrust != nullptr)) {
            const auto iter = m_strategy_map.find(entrust->policy_no);
            if (iter != m_strategy_map.end()) {
                iter->second->PostMsg(kCancelOrderRsp, std::make_shared<TBuffer>(withdraw_rsp));
                return;
            }
            SPDLOG_ERROR("strat={} not found in m_strategy_map", entrust->policy_no);
        } else {
            SPDLOG_ERROR("entno={} not found in m_order_manager", recv_data.entno);
        }
        // 找不到委托就给每个策略都发一遍
        for (const auto& p : m_strategy_map) {
            p.second->PostMsg(kCancelOrderRsp, std::make_shared<TBuffer>(withdraw_rsp));
        }
    }
}

void BacktestStrategyEngine::OnCancelOrderRsp(int msg_id, const BufPtr buffer) {
    auto recv_data = *reinterpret_cast<const WithdrawRsp*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
}

// =========================================================
// 交易推送
// =========================================================

void BacktestStrategyEngine::OnPlaceOrderConfirm(int msg_id, const BufPtr buffer) {
    const auto& recv_data = buffer->RefData<Order>();
    SPDLOG_INFO("received order: {}", zrt::to_str(recv_data));
    Order* entrust = m_order_manager.FindLocalOrder(recv_data);
    if (ZRT_UNLIKELY(!entrust)) {
        SPDLOG_WARN("entrust not found locally, entno={}", recv_data.entno);
        // 回测模式不处理外系统委托
        if (ZRT_LIKELY(!recv_data.entno)) {
            Order order = recv_data;
            zrt::fill_field(order.entno, m_order_manager.CreateOrderId());
            zrt::fill_field(order.ent_time, order.confirm_time);
            zrt::fill_field(order.fmt_time, MyUTC(order.ent_time, 19).GetYmdHMS());
            zrt::fill_field(order.source, OrderSource::Foreign);
            entrust = m_order_manager.AddOrder(order);
            SPDLOG_WARN("add foreign order={}", zrt::to_str(order));
        }
        if (ZRT_UNLIKELY(!entrust)) {
            SPDLOG_ERROR("entrust not found: {}", zrt::to_str(recv_data));
            return;
        }
    }
    if (!entrust->ex_entno) {
        zrt::fill_field(entrust->ex_entno, recv_data.ex_entno);
    } else if (entrust->ex_entno != recv_data.ex_entno) {
        SPDLOG_ERROR("ex_entno error: local={} recv={}", zrt::to_str(*entrust), zrt::to_str(recv_data));
    }
    // 先累加, 因为生成成交要用
    ++entrust->status_id;
    // 要在更新委托之前生成成交
    CreateTrade(*entrust, recv_data);
    FillOrderByOrder(*entrust, recv_data);
    SPDLOG_INFO("updated order: {}", zrt::to_str(*entrust));

    // 回测模式不写共享内存
    m_order_manager.SetUp2date(entrust->account_id, entrust->entno);

    // 记录 private_no 到 entno 的映射
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

void BacktestStrategyEngine::CreateTrade(const Order& local_order, const Order& recv_order) {
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

    const auto trade_buffer = std::make_shared<TBuffer>(trade);

    // 回测模式不写数据库

    for (const auto& strat_id : m_trade_sub_map[trade.account_id][trade.instrument]) {
        SendToStrategy(kTradePush, trade_buffer, strat_id);
        SendToStrategy(kPortfolioPosPush, std::make_shared<TBuffer>(m_order_manager.GetPortfolioPos(trade)), strat_id);
    }
}

void BacktestStrategyEngine::OnTradePush(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const Trade*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    m_order_manager.AddTrade(recv_data);

    for (const auto& strat_id : m_trade_sub_map[recv_data.account_id][recv_data.instrument]) {
        SendToStrategy(kTradePush, buffer, strat_id);
        SendToStrategy(kPortfolioPosPush, std::make_shared<TBuffer>(m_order_manager.GetPortfolioPos(recv_data)), strat_id);
    }
}

void BacktestStrategyEngine::OnPosPush(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const Position*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    m_order_manager.UpdatePos(recv_data);
    for (const auto& strat_id : m_trade_sub_map[recv_data.account_id][recv_data.instrument]) {
        const auto iter = m_strategy_map.find(strat_id);
        if (iter != m_strategy_map.end()) {
            iter->second->PostMsg(kPositionPush, buffer);
        } else {
            SPDLOG_ERROR("strat={} not found in m_strategy_map", strat_id);
        }
    }
}

void BacktestStrategyEngine::OnBalancePush(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const Balance*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    m_order_manager.UpdateBalance(recv_data);
    std::unordered_set<std::string> already_sent {};
    for (const auto& inst_map_pair : m_trade_sub_map[recv_data.account_id]) {
        for (const auto& strat_id : inst_map_pair.second) {
            if (!already_sent.emplace(strat_id).second) {
                continue;
            }
            const auto iter = m_strategy_map.find(strat_id);
            if (iter != m_strategy_map.end()) {
                iter->second->PostMsg(kBalancePush, buffer);
            } else {
                SPDLOG_ERROR("strat={} not found in m_strategy_map", strat_id);
            }
        }
    }
}

// =========================================================
// K线查询
// =========================================================

void BacktestStrategyEngine::OnQueryKLineReq(int msg_id, const BufPtr buffer) {
    auto recv_data = *reinterpret_cast<const KLineQryReq*>(buffer->Data());
    const int64_t req_id = zrt::get_random<int64_t>();
    zrt::fill_field(recv_data.req_id, req_id);
    m_req_id_map[req_id] = recv_data.strat_id;
    SPDLOG_INFO("req kline, req_id={}, strat_id={}", req_id, recv_data.strat_id);
    m_kline_manager->QryKLine(recv_data);
}

void BacktestStrategyEngine::OnQueryKLinePatchRsp(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const KLineRange*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    m_kline_manager->AddKLine(recv_data);
}

void BacktestStrategyEngine::OnQueryKLineRsp(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const KLineRange*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    std::string strat_id = m_req_id_map[recv_data.req_id];
    const auto iter = m_strategy_map.find(strat_id);
    if (iter != m_strategy_map.end()) {
        SPDLOG_DEBUG("post kline to strat_id={}", strat_id);
        iter->second->PostMsg(msg_id, buffer);
    } else {
        SPDLOG_ERROR("strat_id not found, strat_id={}", strat_id);
    }
}

// =========================================================
// 委托查询
// =========================================================

void BacktestStrategyEngine::OnHandleQueryOrderReq(int msg_id, const BufPtr buffer) {
    EntrustQryReq recv_data = buffer->RefData<EntrustQryReq>();
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    if (!recv_data.entno) {
        zrt::fill_field(recv_data.entno, zrt::GetUmapVal(m_private_no_map[recv_data.strat_id], recv_data.private_no));
    }
    const Order* entrust = m_order_manager.FindLocalOrder(recv_data.entno);
    if (entrust) {
        SendToStrategy(kQueryOrderRsp, std::make_shared<TBuffer>(*entrust), entrust->policy_no);
    } else {
        SPDLOG_ERROR("entrust({}) not found", recv_data.entno);
    }
}

void BacktestStrategyEngine::OnQueryOrderByPrivateNoReq(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const EntrustQryByPrivateNoReq*>(buffer->Data());
    SPDLOG_INFO("Query entrust by private_no={} from strategy", recv_data.private_no);

    Order* local_entrust = m_order_manager.FindLocalOrderByPrivateNo(recv_data.private_no);
    if (local_entrust) {
        SPDLOG_INFO("Found entrust locally: private_no={}, entno={}, status={}",
                   local_entrust->private_no, local_entrust->entno, local_entrust->status);
        TBufferPtr rsp_buf = std::make_shared<TBuffer>(*local_entrust);
        PostMsg(kQueryOrderByPrivateNoRsp, rsp_buf);
    } else {
        SPDLOG_WARN("Entrust not found locally: private_no={}", recv_data.private_no);
        TBufferPtr err_buf = std::make_shared<TBuffer>();
        PostMsg(kQueryOrderByPrivateNoErr, err_buf);
    }
}

// =========================================================
// 定时器
// =========================================================

void BacktestStrategyEngine::OnSetTimer(int msg_id, const BufPtr buffer) {
    auto recv_data = *reinterpret_cast<const SetTimerReq*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    zrt::fill_field(recv_data.service_name, k_StrategyEngine);
    m_timer_manager->PostMsg(kSetTimer, std::make_shared<TBuffer>(recv_data));
}

void BacktestStrategyEngine::OnHandleKillTimerReq(int msg_id, const BufPtr buffer) {
    auto recv_data = *reinterpret_cast<const TimerKey*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    zrt::fill_field(recv_data.service_name, k_StrategyEngine);
    m_timer_manager->PostMsg(kKillTimer, std::make_shared<TBuffer>(recv_data));
}

void BacktestStrategyEngine::OnHandleClearAllTimerReq(int msg_id, const BufPtr buffer) {
    auto recv_data = *reinterpret_cast<const TimerKey*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    zrt::fill_field(recv_data.service_name, k_StrategyEngine);
    m_timer_manager->PostMsg(kClearAllTimer, std::make_shared<TBuffer>(recv_data));
}

void BacktestStrategyEngine::OnHandleTimerEvent(int msg_id, const BufPtr buffer) {
    const auto& timer_event = buffer->RefData<TimerEventPush>();
    SPDLOG_TRACE("{}", zrt::to_str(timer_event));
    // 回测模式没有引擎级别的定时器（UPL 更新、市场信息刷新），直接转发给策略
    SendToStrategy(msg_id, buffer, timer_event.setter_id);
}

// =========================================================
// 指标推送
// =========================================================

void BacktestStrategyEngine::OnIndicatorKlinePush(int msg_id, const BufPtr buffer) {
    const auto recv_data = *reinterpret_cast<const KLine*>(buffer->Data());
    SPDLOG_TRACE("market={} symbol={}", recv_data.market, recv_data.instrument);
    SendToSubedStrategies(MsgId::kIndicatorKLinePush, buffer, m_kline_sub_map[{recv_data.market, recv_data.instrument, recv_data.coefficient, recv_data.scale}]);
}

void BacktestStrategyEngine::OnIndicatorKlineOpenPush(int msg_id, const BufPtr buffer) {
    const auto recv_data = *reinterpret_cast<const KLine*>(buffer->Data());
    SPDLOG_TRACE("market={} symbol={}", recv_data.market, recv_data.instrument);
    SendToSubedStrategies(MsgId::kIndicatorKLineOpenPush, buffer, m_kline_open_sub_map[{recv_data.market, recv_data.instrument, recv_data.coefficient, recv_data.scale}]);
}

void BacktestStrategyEngine::OnIndicatorKlineClosePush(int msg_id, const BufPtr buffer) {
    const auto recv_data = *reinterpret_cast<const KLine*>(buffer->Data());
    SPDLOG_TRACE("market={} symbol={}", recv_data.market, recv_data.instrument);
    SendToSubedStrategies(MsgId::kIndicatorKLineClosePush, buffer, m_kline_close_sub_map[{recv_data.market, recv_data.instrument, recv_data.coefficient, recv_data.scale}]);
}

// =========================================================
// 策略信息 & 通知（回测模式 no-op）
// =========================================================

void BacktestStrategyEngine::OnDbSetStrategyInfo(int msg_id, const BufPtr buffer) {
    // 回测模式不写数据库，仅打印日志
    const auto& strat_info = buffer->RefData<StrategyInfo>();
    SPDLOG_DEBUG("backtest no-op: {}", zrt::to_str(strat_info));
}

void BacktestStrategyEngine::OnDbSetStrategyLog(int msg_id, const BufPtr buffer) {
    // 回测模式不写数据库，仅打印日志
    const auto& log = buffer->RefData<StrategyLog>();
    SPDLOG_DEBUG("backtest no-op: {}", zrt::to_str(log));
}

void BacktestStrategyEngine::OnNotifyMsg(int msg_id, const BufPtr buffer) {
    // 回测模式不发送通知
    SPDLOG_DEBUG("backtest no-op: notify message");
}
