//
// Created by dell on 2025/2/20.
//

#include "pch.h"
#include "strategy_base.h"
#include "strategy_engine.h"
#include "i_strategy_engine_dump.h"
#include "logger_config.h"
#include "i_client_dump.h"
#include "i_timer_manager_dump.h"
#include "sonic/sonic.h"
#include "sonic_helper.h"

void StrategyBase::InitLogger(const YAML::Node& strat_yml) {
    zrt::logger_config logger_config {};
    logger_config.m_log_file = fmt::format("log/{}/{}", m_strat_info.id, m_strat_info.id);
    logger_config.m_async = true;
    logger_config.m_show_level = "";
    logger_config.m_log_level = "trace,info,error";
    logger_config.m_set_default = false;
    try {
        if (strat_yml["logger"]) {
            auto& l = strat_yml["logger"];
            if (l["log_file"]) logger_config.m_log_file = l["log_file"].as<std::string>();
            if (l["async"]) logger_config.m_async = l["async"].as<bool>();
            if (l["log_level"]) logger_config.m_log_level = l["log_level"].as<std::string>();
            if constexpr (GlobalConst::IsRealTrading) {
                if (l["max_files"]) logger_config.m_max_files = l["max_files"].as<int>();
            } else {
                if (l["max_files"]) logger_config.m_max_files = 99;
            }
        }
    } catch (const std::exception& e) {
        RLOG_ERROR("strategy logger config parse failed, use default logger config, err: {}", e.what());
    }
    m_logger = zrt::create_logger2(logger_config);
    RLOG_INFO("create logger: {}", m_logger->name());
}

// void StrategyBase::Post2StratEngine(const int msg_type, const TBufferPtr buffer) const {
//     const auto sp = m_strat_engine.lock();
//     if (!sp) {
//         RLOG_ERROR("m_strat_engine not exist");
//         return;
//     }
//     sp->PostMsg(msg_type, buffer);
// }

void StrategyBase::SubscribeQuote(const std::string& channel, const std::string& exchange, const std::string& symbol) const {
    QuoteSub quote_sub {};
    zrt::fill_field(quote_sub.channel, channel);
    zrt::fill_field(quote_sub.market, exchange);
    zrt::fill_field(quote_sub.inst_id, symbol);
    zrt::fill_field(quote_sub.strat_id, m_strat_info.id);
    TBufferPtr buf = std::make_shared<TBuffer>(quote_sub);
    m_strat_engine->PostMsg(kStratSubscribeQuote, buf);
    RLOG_INFO("{}", zrt::to_str(quote_sub));
}

void StrategyBase::SubscribeTrade(const std::string& market, const std::string& account_id, const std::string& instrument) const {
    TradeSub trade_sub {};
    zrt::fill_field(trade_sub.market, market);
    zrt::fill_field(trade_sub.account_id, account_id);
    zrt::fill_field(trade_sub.inst_id, instrument);
    zrt::fill_field(trade_sub.strat_id, m_strat_info.id);
    const auto buf = std::make_shared<TBuffer>(trade_sub);
    m_strat_engine->PostMsg(kStratSubscribeTrade, buf);
    RLOG_INFO("{}", zrt::to_str(trade_sub));
}

void StrategyBase::SubscribeKLine(const std::string& market, const std::string& instrument, const int coefficient, const char scale) const {
    KLineSub kline_sub {};
    zrt::fill_field(kline_sub.market, market);
    zrt::fill_field(kline_sub.instrument, instrument);
    zrt::fill_field(kline_sub.coefficient, coefficient);
    zrt::fill_field(kline_sub.scale, scale);
    zrt::fill_field(kline_sub.strat_id, m_strat_info.id);
    TBufferPtr buf = std::make_shared<TBuffer>(kline_sub);
    m_strat_engine->PostMsg(kStratSubscribeKLine, buf);
    RLOG_INFO("{}", zrt::to_str(kline_sub));
}

void StrategyBase::SubscribeKLineOpen(const std::string& market, const std::string& instrument, const int coefficient, const char scale) const {
    KLineSub kline_sub {};
    zrt::fill_field(kline_sub.market, market);
    zrt::fill_field(kline_sub.instrument, instrument);
    zrt::fill_field(kline_sub.coefficient, coefficient);
    zrt::fill_field(kline_sub.scale, scale);
    zrt::fill_field(kline_sub.strat_id, m_strat_info.id);
    m_strat_engine->PostMsg(kStratSubscribeKLineOpen, std::make_shared<TBuffer>(kline_sub));
    RLOG_INFO("{}", zrt::to_str(kline_sub));
}

void StrategyBase::SubscribeKLineClose(const std::string& market, const std::string& instrument, const int coefficient, const char scale) const {
    KLineSub kline_sub {};
    zrt::fill_field(kline_sub.market, market);
    zrt::fill_field(kline_sub.instrument, instrument);
    zrt::fill_field(kline_sub.coefficient, coefficient);
    zrt::fill_field(kline_sub.scale, scale);
    zrt::fill_field(kline_sub.strat_id, m_strat_info.id);
    m_strat_engine->PostMsg(kStratSubscribeKLineClose, std::make_shared<TBuffer>(kline_sub));
    RLOG_INFO("{}", zrt::to_str(kline_sub));
}

void StrategyBase::PlaceOrderReq(OrderReq& entrust_req) const {
    zrt::fill_field(entrust_req.ent_time, MyUTC().Epoch19());
    // 从时间间隔ms转换成epoch19
    zrt::fill_field(entrust_req.expire_time, entrust_req.ent_time + zrt::kMega * entrust_req.expire_time);
    m_strat_engine->PostMsg(kPlaceOrder, std::make_shared<TBuffer>(entrust_req));
    RLOG_INFO("OrderReq={}", zrt::to_str(entrust_req));
}

void StrategyBase::CancelOrderReq(const WithdrawReq& req) const {
    RLOG_INFO("WithdrawReq={}", zrt::to_str(req));
    m_strat_engine->PostMsg(kCancelOrder, std::make_shared<TBuffer>(req));
}

void StrategyBase::QueryKLineReq(const KLineQryReq& req) const {
    RLOG_INFO("KLineQryReq={}", zrt::to_str(req));
    m_strat_engine->PostMsg(kQueryKLineReq, std::make_shared<TBuffer>(req));
}

void StrategyBase::QueryOrderReq(const EntrustQryReq& req) const {
    RLOG_INFO("EntrustQryReq={}", zrt::to_str(req));
    m_strat_engine->PostMsg(kQueryOrderReq, std::make_shared<TBuffer>(req));
}

void StrategyBase::QueryOrderReq(const int64_t entno, const std::string& market, const std::string& account_id) const {
    EntrustQryReq entrust_qry_req {};
    zrt::fill_field(entrust_qry_req.market, market);
    zrt::fill_field(entrust_qry_req.account_id, account_id);
    zrt::fill_field(entrust_qry_req.entno, entno);
    RLOG_INFO("{}", zrt::to_str(entrust_qry_req));
    QueryOrderReq(entrust_qry_req);
}

void StrategyBase::QueryOrderReq(const std::string& private_no) const {
    EntrustQryReq entrust_qry_req {};
    // zrt::fill_field(entrust_qry_req.market, market);
    // zrt::fill_field(entrust_qry_req.account_id, account_id);
    // zrt::fill_field(entrust_qry_req.entno, entno);
    zrt::fill_field(entrust_qry_req.strat_id, GetStratId());
    zrt::fill_field(entrust_qry_req.private_no, private_no);
    RLOG_INFO("{}", zrt::to_str(entrust_qry_req));
    QueryOrderReq(entrust_qry_req);
}

// 同步请求市场信息
void StrategyBase::QueryMarketInfoSync(const StratQryMarketInfoReq& req, std::vector<MarketInfo>& market_infos) const {
    RLOG_INFO("req={}", zrt::to_str(req));
    BufPtr rsp_buf {};
    m_strat_engine->PostSyncMsg(kQueryMarketInfoSync, std::make_shared<TBuffer>(req), rsp_buf);
    rsp_buf->ForEach<MarketInfo>([&market_infos](const auto& x) {
        market_infos.emplace_back(x);
    });
}

// 同步请求市场信息
std::vector<MarketInfo> StrategyBase::QueryMarketInfoSync(const std::string& market, const std::string& instrument) const {
    StratQryMarketInfoReq req {};
    zrt::fill_field(req.market, market);
    zrt::fill_field(req.instrument, instrument);

    std::vector<MarketInfo> market_infos {};
    QueryMarketInfoSync(req, market_infos);
    return market_infos;
}

void StrategyBase::Post2StratEngine(const int msg_type, const TBufferPtr buffer) const {
    m_strat_engine->PostMsg(msg_type, buffer);
}

void StrategyBase::SetTimer(const int timer_id, const int delay_ms, const bool repeat) const {
    SetTimerReq set_timer_req {};
    zrt::fill_field(set_timer_req.setter_id, GetStratId());
    zrt::fill_field(set_timer_req.timer_id, timer_id);
    zrt::fill_field(set_timer_req.delay_ms, delay_ms);
    zrt::fill_field(set_timer_req.repeat, repeat);
    RLOG_INFO("SetTimerReq={}", zrt::to_str(set_timer_req));
    m_strat_engine->PostMsg(kSetTimer, std::make_shared<TBuffer>(set_timer_req));
}

void StrategyBase::KillTimer(const int timer_id) const {
    TimerKey req {};
    zrt::fill_field(req.setter_id, GetStratId());
    zrt::fill_field(req.timer_id, timer_id);
    RLOG_INFO("{}", zrt::to_str(req));
    m_strat_engine->PostMsg(kKillTimer, std::make_shared<TBuffer>(req));
}

void StrategyBase::ClearAllTimer() const {
    TimerKey req {};
    zrt::fill_field(req.setter_id, GetStratId());
    RLOG_INFO("{}", zrt::to_str(req));
    m_strat_engine->PostMsg(kClearAllTimer, std::make_shared<TBuffer>(req));
}

void StrategyBase::SendNotifyMsg(const std::string& channel, const std::string& head, const std::string& body) const {
    RLOG_INFO(LOG_PREFIX_SLACK "[{}|{}] {}", channel, head, body);
    if constexpr (GlobalConst::IsRealTrading) {
        NotifyMessageReq slack_req {};
        zrt::fill_field(slack_req.channel, channel);
        zrt::fill_field(slack_req.subject, head);
        zrt::fill_field(slack_req.content, body);
        zrt::fill_field(slack_req.is_async, false);
        m_strat_engine->PostMsg(kNotifyMessage, std::make_shared<TBuffer>(slack_req));

        // 异步写入策略运行日志
        StrategyLog log_req {};
        zrt::fill_field(log_req.strat_id, GetStratId());
        zrt::fill_field(log_req.log_level, 'I');
        log_req.log_time = MyUTC().Epoch19();
        log_req.seq = m_log_seq++;
        zrt::fill_field(log_req.content, body);
        m_strat_engine->PostMsg(kDbSetStrategyLog, std::make_shared<TBuffer>(log_req));
    }
}

// YAML转JSON的递归转换函数 (使用sonic)
std::string StrategyBase::YamlToJson(const YAML::Node& node) {
    sonic_json::Document doc;
    auto& alloc = doc.GetAllocator();

    std::function<sonic_json::Node(const YAML::Node&)> convert = [&](const YAML::Node& n) -> sonic_json::Node {
        sonic_json::Node value;

        if (n.IsScalar()) {
            try {
                // 尝试解析为数字
                if (n.Tag() == "!") {
                    value.SetString(n.as<std::string>(), alloc);
                } else {
                    try {
                        auto d = n.as<double>();
                        value.SetDouble(d);
                    } catch (...) {
                        try {
                            auto i = n.as<int64_t>();
                            value.SetInt64(i);
                        } catch (...) {
                            try {
                                auto b = n.as<bool>();
                                value.SetBool(b);
                            } catch (...) {
                                value.SetString(n.as<std::string>(), alloc);
                            }
                        }
                    }
                }
            } catch (...) {
                value.SetString(n.as<std::string>(), alloc);
            }
        } else if (n.IsSequence()) {
            value.SetArray();
            for (const auto& item : n) {
                value.PushBack(convert(item), alloc);
            }
        } else if (n.IsMap()) {
            value.SetObject();
            for (const auto& kv : n) {
                std::string key = kv.first.as<std::string>();
                value.AddMember(key, convert(kv.second), alloc);
            }
        }

        return value;
    };

    sonic_json::Node result = convert(node);
    sonic_json::WriteBuffer wb;
    result.Serialize(wb);
    return std::string(wb.ToString());
}

// 保存策略状态
void StrategyBase::SaveStrategyEnvStatus(const StrategyEnvStatus status) {
    // 如果策略正在被删除，跳过保存操作，避免删除后又被写回数据库
    if (IsDeleting()) {
        RLOG_DEBUG("skip saving status={} because strategy is being deleted", status);
        return;
    }
    RLOG_INFO("status={}", status);
    zrt::fill_field(m_strat_info.status, status);
    m_strat_engine->PostMsg(kDbSetStrategyInfo, std::make_shared<TBuffer>(m_strat_info));
}

// 从YAML::Node保存策略参数
void StrategyBase::SaveStrategyParam(const YAML::Node& param_yaml) {
    const std::string param_json = YamlToJson(param_yaml);
    SaveStrategyParam(param_json);
}

// 从JSON字符串保存策略参数
void StrategyBase::SaveStrategyParam(const std::string& param_json) {
    if (IsDeleting()) {
        RLOG_DEBUG("skip saving param because strategy is being deleted");
        return;
    }
    RLOG_INFO("{}", param_json);
    zrt::fill_field(m_strat_info.param, param_json);
    m_strat_engine->PostMsg(kDbSetStrategyInfo, std::make_shared<TBuffer>(m_strat_info));
}

// 保存策略指标
void StrategyBase::SaveStrategyIndicator(const std::string& indicator_json) {
    if (IsDeleting()) {
        RLOG_DEBUG("skip saving indicator because strategy is being deleted");
        return;
    }
    RLOG_INFO("{}", indicator_json);
    zrt::fill_field(m_strat_info.indicator, indicator_json);
    m_strat_engine->PostMsg(kDbSetStrategyInfo, std::make_shared<TBuffer>(m_strat_info));
}

// 保存策略信息
void StrategyBase::SaveStrategyInfo() const {
    if (IsDeleting()) {
        RLOG_DEBUG("skip saving strategy info because strategy is being deleted");
        return;
    }
    m_strat_engine->PostMsg(kDbSetStrategyInfo, std::make_shared<TBuffer>(m_strat_info));
}

// 生命周期同步 handler：在策略自身线程执行 OnStart/OnStop，避免引擎线程直接调用的数据竞争
BufPtr StrategyBase::OnStratStartSync(int msg_id, const BufPtr buffer) {
    HttpStrategyOperationRsp rsp{};
    rsp.success = OnStart();
    return std::make_shared<TBuffer>(rsp);
}

BufPtr StrategyBase::OnStratStopSync(int msg_id, const BufPtr buffer) {
    HttpStrategyOperationRsp rsp{};
    rsp.success = OnStop();
    return std::make_shared<TBuffer>(rsp);
}

