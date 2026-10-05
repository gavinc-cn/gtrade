//
// Created by dell on 2025/2/20.
//

#include "pch.h"
#include "dict.h"
#include "httplib.h"
#include "i_exchange_data_dump.h"
#include "i_strategy_engine_dump.h"
#include "my_utc.h"
#include "zrtools/zrt_file.h"
#include "type_define_dump.h"
#include "i_client_dump.h"
#include "db_structures_dump.h"
#include "sonic/sonic.h"
#include "i_timer_manager_dump.h"
#include "strategy_demo.h"
#include "order_manager.h"


#define SET_STRAT_STATUS(status) do { \
int old_status = m_indicator->strat_status; \
m_indicator->strat_status = status; \
zrt::fill_field(m_indicator->status_str, #status); \
RLOG_INFO("status: {}=>{}", StratStatus::Dump(old_status), StratStatus::Dump(m_indicator->strat_status)); \
} while (false)


bool StratDemo::OnInit(const YAML::Node& strat_yml) {
    RLOG_INFO("");
    InitLogger(strat_yml);
    zrt::fill_field(m_strat_info.strat_template, k_StratDemo);

    m_config.account_id = strat_yml["account_id"].as<std::string>();
    m_config.market = strat_yml["market"].as<std::string>();
    m_config.instrument = strat_yml["instrument"].as<std::string>();
    m_config.min_entamt = strat_yml["min_entamt"].as<double>();
    m_config.kline_window_size = strat_yml["kline_window_size"].as<size_t>();
    m_config.fee = strat_yml["fee"].as<double>();

    InstallDefaultHandler([this](const int msg_id, const BufPtr buffer) {
        RLOG_WARN("default handler: msg_id={} buf_sz={}", GetEmName_MsgId(msg_id), buffer->GetSize());
    });

    ZRT_ADD_HANDLER(kQueryKLineRsp, StratDemo::OnQryKLineRsp);
    ZRT_ADD_HANDLER(kQueryOrderRsp, StratDemo::OnQryOrderRsp);
    ZRT_ADD_HANDLER(kIndicatorKLineClosePush, StratDemo::OnIndicatorKLineClosePush);
    ZRT_ADD_HANDLER(kDepth1, StratDemo::OnDepth1);
    ZRT_ADD_HANDLER(kPlaceOrderRsp, StratDemo::OnPlaceOrderRsp);
    ZRT_ADD_HANDLER(kPlaceOrderConfirm, StratDemo::OnPlaceOrderConfirm);
    ZRT_ADD_HANDLER(kTradePush, StratDemo::OnTradePush);
    ZRT_ADD_HANDLER(kPositionPush, StratDemo::OnPosPush);
    ZRT_ADD_HANDLER(kPortfolioPosPush, StratDemo::OnPortfolioPosPush);
    ZRT_ADD_HANDLER(kBalancePush, StratDemo::OnBalancePush);
    ZRT_ADD_HANDLER(kTimerEvent, StratDemo::OnTimerEvent);

    // 现在使用checkpoint恢复状态, indicator可以不用
    m_indicator.Reset();

    // 初始化检查点并尝试恢复
    m_checkpoint.Init(GetStratId());
    if (m_checkpoint.TryRestore()) {
        RestoreFromCheckpoint();
        RLOG_INFO("Restored from checkpoint: cur_pos={}, strat_status={}",
                  m_indicator->cur_pos, StratStatus::Dump(m_indicator->strat_status));
    }

    if (!m_indicator.IsValid()) {
        RLOG_ERROR("Failed to initialize shared memory for indicator");
        return false;
    }
    if constexpr (GlobalConst::IsBackTest) {
        m_indicator.Reset();
    }
    m_buy_ord_info = &(m_indicator->buy_ent_info);
    m_sell_ord_info = &(m_indicator->sell_ent_info);
    RLOG_INFO("Indicator initialized from shared memory: {}", m_indicator->ToStr());

    // 保存策略参数到数据库 (直接从YAML转换)
    SaveStrategyParam(strat_yml);
    RLOG_INFO("Strategy parameters saved to database");

    return true;
}

bool StratDemo::OnStart() {
    zrt::fill_field(m_buy_ord_info->market, m_config.market);
    zrt::fill_field(m_buy_ord_info->inst_id, m_config.instrument);
    zrt::fill_field(m_buy_ord_info->account_id, m_config.account_id);
    zrt::fill_field(m_buy_ord_info->policy_no, GetStratId());
    zrt::fill_field(m_buy_ord_info->price_type, PriceType::Limit);
    zrt::fill_field(m_buy_ord_info->trade_mode, TradeMode::Cash);
    m_indicator->sell_ent_info = m_indicator->buy_ent_info;

    // 查询市场信息, 获取inst_id_code
    if constexpr (GlobalConst::IsRealTrading) {
        RLOG_INFO("Validating instrument (sync mode): {}", m_config.instrument);
        const std::vector<MarketInfo> market_info_vec = QueryMarketInfoSync(m_config.market, m_config.instrument);
        if (market_info_vec.empty()) {
            RLOG_ERROR("{} {} not exist", m_config.market, m_config.instrument);
            return false;
        } else if (market_info_vec.size() > 1) {
            RLOG_ERROR("{} {} not unique", m_config.market, m_config.instrument);
            return false;
        }
        zrt::fill_field(m_buy_ord_info->inst_id_code, market_info_vec[0].inst_id_code);
        zrt::fill_field(m_sell_ord_info->inst_id_code, market_info_vec[0].inst_id_code);
        RLOG_INFO("load inst_id_code={}|{}", m_buy_ord_info->inst_id_code, m_sell_ord_info->inst_id_code);
    }

    SubscribeQuote(k_depth1, m_config.market, m_config.instrument);
    RLOG_INFO("sub {} market={} inst={}", k_depth1, m_config.market, m_config.instrument);

    SubscribeTrade(m_config.market, m_config.account_id, m_config.instrument);
    RLOG_INFO("sub trade market={} account={} inst={}", m_config.market, m_config.account_id, m_config.instrument);

    SubscribeKLineClose(m_config.market, m_config.instrument, 1, KLineScale::Hour);
    RLOG_INFO("qry kline inst={}", m_config.instrument);
    QryKLine(m_config.instrument);

    SaveStrategyIndicator(m_indicator->ToJson());
    RLOG_INFO("{}", m_indicator->ToStr());

    // 启动定时器，每10秒保存一次指标数据到MySQL
    SetTimer(TimerId::SaveIndicator, zrt::kKilo * 10, true);
    RLOG_INFO("Timer started: saving indicators to MySQL every 10 seconds");

    SaveStrategyEnvStatus(StrategyEnvStatus::Running);
    SendNotifyMsg(k_notice, GetStratId(), "Strat Start Success");
    return true;
}

void StratDemo::QryKLine(const std::string& inst_id) {
    // 请求1小时K线
    KLineQryReq kline_req {};
    zrt::fill_field(kline_req.strat_id, GetStratId());
    zrt::fill_field(kline_req.market, m_config.market);
    zrt::fill_field(kline_req.account_id, m_config.account_id);
    zrt::fill_field(kline_req.instrument, inst_id);
    zrt::fill_field(kline_req.coefficient, 1);
    zrt::fill_field(kline_req.scale, KLineScale::Hour);
    MyUTC now {};
    RLOG_INFO("now={}", now.ToFormat());
    zrt::fill_field(kline_req.end_time, now.Epoch19());
    zrt::fill_field(kline_req.start_time, now.OffsetHour(-m_config.kline_window_size).OffsetMSec(-1).Epoch19());
    RLOG_INFO("{}", zrt::to_str(kline_req));
    QueryKLineReq(kline_req);
}

void StratDemo::ProcessKLine(const KLine& kline) {
    if (zrt::equal(kline.instrument, m_config.instrument)) {
        RLOG_INFO("{}", zrt::to_str(kline));
    }
}

void StratDemo::OnQryKLineRsp(int msg_id, const BufPtr buffer) {
    using RecvData = KLineRange;
    const RecvData& recv_data = buffer->RefData<RecvData>();
    RLOG_INFO("{}", zrt::to_str(recv_data));

    if (recv_data.count < static_cast<int>(m_config.kline_window_size)) {
        RLOG_ERROR("kline count not match, expect={}, actual={}", m_config.kline_window_size, recv_data.count);
        return;
    }
    // 如果返回的K线数量多, 就取最新的
    for (int i = recv_data.count - m_config.kline_window_size; i < recv_data.count; ++i) {
        const KLine& kline = recv_data.klines[i];
        ProcessKLine(kline);
    }

    if (zrt::equal(recv_data.market, m_config.market) &&
        zrt::equal(recv_data.instrument, m_config.instrument)) {
        m_data_status.set(DataStatus::KlineReady);
        RLOG_INFO("{}", ZRT_VAR2STR(EmDataStatus::FirstKlineReady));
    }

    RLOG_INFO("m_data_status={}", m_data_status.to_string());
    if (m_data_status.all()) {
        SET_STRAT_STATUS(StratStatus::Ready2Order);
    }
}

void StratDemo::OnQryOrderRsp(int msg_id, const BufPtr buffer) {
    using RecvData = Order;
    RLOG_INFO("");
    buffer->ForEach<RecvData>([this](const RecvData& entrust){
        RLOG_INFO("qry entrust rsp: {}", zrt::to_str(entrust));

        // 通过private_no匹配委托
        if (zrt::equal(entrust.private_no, m_buy_ord_info->private_no)) {
            RLOG_INFO("matched buy_ent_info.private_no={}, status={}", m_buy_ord_info->private_no, entrust.status);
            HandleOrderUpdate(m_indicator->buy_ent_info, entrust);
        }
        else if (zrt::equal(entrust.private_no, m_sell_ord_info->private_no)) {
            RLOG_INFO("matched sell_ent_info.private_no={}, status={}", m_sell_ord_info->private_no, entrust.status);
            HandleOrderUpdate(m_indicator->sell_ent_info, entrust);
        }
    });
}

void StratDemo::OnIndicatorKLineClosePush(int msg_id, const BufPtr buffer) {
    using RecvData = KLine;
    const RecvData& recv_data = buffer->RefData<RecvData>();
    RLOG_INFO("{}", zrt::to_str(recv_data));
    ProcessKLine(recv_data);
}

bool StratDemo::OnStop() {
    RLOG_INFO("");
    // 停止定时器
    KillTimer(TimerId::SaveIndicator);
    RLOG_INFO("Timer stopped: kTimer_SaveIndicator");
    SET_STRAT_STATUS(StratStatus::Initializing);
    SaveStrategyEnvStatus(StrategyEnvStatus::Stopped);
    return true;
}

bool StratDemo::OnPause() {
    RLOG_INFO("");
    SET_STRAT_STATUS(StratStatus::Initializing);
    SaveStrategyEnvStatus(StrategyEnvStatus::Paused);
    return true;
}

bool StratDemo::OnResume() {
    RLOG_INFO("");
    SaveStrategyEnvStatus(StrategyEnvStatus::Running);
    return true;
}



bool StratDemo::BuySpread() {
    if (m_buy_cnt) {
        RLOG_INFO("meet max buy cnt");
        return true;
    }
    m_indicator->cur_pos += m_config.min_entamt;
    ++m_buy_cnt;
    SET_STRAT_STATUS(StratStatus::Wait4Fill);
    PlaceOrder(m_indicator->buy_ent_info, TradeSide::Buy, m_quote.ask_price[0], m_config.min_entamt);
    // 记录private_no到indicator（在收到entno之前委托可能已发出）
    RLOG_INFO("recorded buy_ent_info.private_no={}", m_buy_ord_info->private_no);
    RLOG_INFO("indicator={}", m_indicator->ToStr());
    // 提交检查点
    CommitCheckpoint();
    return true;
}

bool StratDemo::SellSpread() {
    if (m_sell_cnt) {
        RLOG_INFO("meet max sell cnt");
        return true;
    }
    m_indicator->cur_pos -= m_config.min_entamt;
    ++m_sell_cnt;
    SET_STRAT_STATUS(StratStatus::Wait4Fill);
    PlaceOrder(m_indicator->sell_ent_info, TradeSide::Sell, m_quote.bid_price[0], m_config.min_entamt);
    // 记录private_no到indicator（在收到entno之前委托可能已发出）
    RLOG_INFO("recorded sell_ent_info.private_no={}", m_sell_ord_info->private_no);
    RLOG_INFO("indicator={}", m_indicator->ToStr());
    // 提交检查点
    CommitCheckpoint();
    return true;
}


void StratDemo::OnDepth1(int msg_id, const BufPtr buffer) {
    if (m_indicator->strat_status == StratStatus::Initializing) {
        RLOG_TRACE("strat is not started");
        return;
    }
    const auto& depth = buffer->RefData<Depth>();
    if (depth.market != m_config.market) return;
    RLOG_TRACE("{}", zrt::to_str(depth));

    if (zrt::equal(depth.symbol, m_config.instrument)) {
        m_quote = depth;
    }
    else {
        RLOG_INFO("unexpected instrument={}", depth.symbol);
        return;
    }

    // 检查空值
    const double fst_bid1_px = m_quote.bid_price[0] * (1 - m_config.fee);
    const double fst_ask1_px = m_quote.ask_price[0] * (1 + m_config.fee);
    if (zrt::equal_any_of(0, fst_bid1_px, fst_ask1_px)) {
        RLOG_DEBUG("quote not ready, first={}, second={}", zrt::to_str(m_quote));
        return;
    }

    switch (m_indicator->strat_status) {
        case StratStatus::Ready2Order: {
            m_buy_ord_info->quote_monotonic = depth.monotonic;
            BuySpread();
            m_sell_ord_info->quote_monotonic = depth.monotonic;
            SellSpread();
        }
            break;
        default:
            break;
    }
}

void StratDemo::PlaceOrder(OrderInfo& ent_info, const char bs_side, const double price, const double amount) const {
    zrt::fill_field(ent_info.bs_side, bs_side);
    zrt::fill_field(ent_info.price, price);
    zrt::fill_field(ent_info.amount, m_config.min_entamt);
    zrt::fill_field(ent_info.private_no, zrt::get_uuid());
    zrt::fill_field(ent_info.expire_time, 60 * 1000);
    RLOG_INFO("{}", zrt::to_str(ent_info));
    PlaceOrderReq(ent_info);
}

void StratDemo::OnPlaceOrderRsp(int msg_id, const BufPtr buffer) {
    RLOG_INFO("{}", zrt::to_str(buffer->RefData<Order>()));
}

void StratDemo::OnPlaceOrderConfirm(int msg_id, const BufPtr buffer) {
    const auto& entrust = buffer->RefData<Order>();
    if (!zrt::equal(entrust.policy_no, GetStratId())) return;
    RLOG_INFO("Entrust={}", zrt::to_str(entrust));
    if (zrt::is_empty(entrust.private_no)) return;

    if (zrt::equal(entrust.private_no, m_buy_ord_info->private_no)) {
        HandleOrderUpdate(m_indicator->buy_ent_info, entrust);
    }
    else if (zrt::equal(entrust.private_no, m_sell_ord_info->private_no)) {
        HandleOrderUpdate(m_indicator->sell_ent_info, entrust);
    }
    else {
        RLOG_DEBUG("private_no({}) not match this({}) ", entrust.private_no, m_sell_ord_info->private_no);
        return;
    }

    SendNotifyMsg(k_notice, GetStratId(), GetOrderStr(entrust));
}

void StratDemo::CancelOrder(OrderInfo& ent_info) {
    WithdrawReq withdraw_req {};
    zrt::fill_field(withdraw_req.entno, ent_info.entno);
    zrt::fill_field(withdraw_req.market, ent_info.market);
    zrt::fill_field(withdraw_req.account_id, ent_info.account_id);
    zrt::fill_field(withdraw_req.instrument, ent_info.inst_id);
    RLOG_INFO("{}", zrt::to_str(withdraw_req));
    CancelOrderReq(withdraw_req);
}

void StratDemo::HandleOrderUpdate(OrderInfo& ent_info, const Order& order) {
    if (!OrderManager::IsForwardOrder(ent_info.update_time, ent_info.filled, ent_info.status, order)) {
        RLOG_WARN("order duplicated or backward");
        return;
    }

    zrt::fill_field(ent_info.status, order.status);
    zrt::fill_field(ent_info.filled, order.filled);
    zrt::fill_field(ent_info.update_time, order.update_time);

    switch (order.status[0]) {
        case OrderStatus::_6:
        case OrderStatus::_8:
        {
            RevertPos(order.bs_side[0], order.draw_amt);
        }
            break;
        case OrderStatus::_9: {
            RevertPos(order.bs_side, order.amount);
        }
            break;
        default:
            break;
    }

    if (BothClosed()) {
        OnStop();
    }

    // 订单状态更新后提交检查点
    CommitCheckpoint();
}

void StratDemo::AdvancePos(const char bs_side, const double amount) {
    if (bs_side == TradeSide::Buy) {
        m_indicator->cur_pos += amount;
    }
    if (bs_side == TradeSide::Sell) {
        m_indicator->cur_pos -= amount;
    }
}

void StratDemo::RevertPos(const char bs_side, const double amount) {
    if (bs_side == TradeSide::Buy) {
        m_indicator->cur_pos -= amount;
    }
    if (bs_side == TradeSide::Sell) {
        m_indicator->cur_pos += amount;
    }
    RLOG_INFO("indicator={}", m_indicator->ToStr());
}

void StratDemo::OnTradePush(int msg_id, const BufPtr buffer) {
    RLOG_INFO("{}", zrt::to_str(buffer->RefData<Trade>()));
}

void StratDemo::OnPosPush(int msg_id, const BufPtr buffer) {
    RLOG_INFO("{}", zrt::to_str(buffer->RefData<Position>()));
}

void StratDemo::OnPortfolioPosPush(int msg_id, const BufPtr buffer) {
    RLOG_INFO("{}", zrt::to_str(buffer->RefData<Position>()));
}

void StratDemo::OnBalancePush(int msg_id, const BufPtr buffer) {
    RLOG_INFO("{}", zrt::to_str(buffer->RefData<Balance>()));
}

void StratDemo::OnTimerEvent(int msg_id, const BufPtr buffer) {
    using RecvData = TimerEventPush;
    const RecvData& recv_data = buffer->RefData<RecvData>();
    RLOG_INFO("TimerEventPush={}", zrt::to_str(recv_data));

    switch (recv_data.timer_id) {
        case TimerId::SaveIndicator: {
            SaveStrategyIndicator(m_indicator->ToJson());
            RLOG_INFO("{}", m_indicator->ToStr());
        }
            break;
        default: {
            RLOG_ERROR("Unknown timer_id: {}", recv_data.timer_id);
        }
            break;
    }
}

void StratDemo::CommitCheckpoint() {
    if (!m_checkpoint.IsValid()) {
        return;
    }

    // 填充策略自定义数据
    m_checkpoint.GetData() = m_indicator.Read();

    // 设置策略状态
    if (m_indicator->strat_status == StratStatus::Initializing) {
        m_checkpoint.SetStatus(gtrade::StrategyStatus::kStopped);
    } else {
        m_checkpoint.SetStatus(gtrade::StrategyStatus::kRunning);
    }

    // 提交到共享内存
    m_checkpoint.Commit();
    RLOG_INFO("CommitCheckpoint: {}", m_indicator->ToStr());
}

void StratDemo::RestoreFromCheckpoint() {
    // 从检查点恢复策略自定义数据
    const auto& data = m_checkpoint.GetData();
    m_indicator.Write(data);
    RLOG_INFO("RestoreFromCheckpoint: {}", m_indicator->ToStr());
}

// ── 自注册 ──────────────────────────────────────────────────────────────────
// 利用静态变量初始化在 main() 之前执行的特性，将本策略注册到全局工厂。
// StrategyFactory::GetRegistry() 使用 Construct On First Use，保证 map 已就绪。
#include "strategy_factory.h"
#include "string_keys.h"
namespace {
    const bool _registered = StrategyFactory::Register(
        k_StratDemo,
        [](const GTradeConfig& cfg, MyHandler* engine, const std::string& id) {
            return std::make_shared<StratDemo>(cfg, engine, id);
        });
}
