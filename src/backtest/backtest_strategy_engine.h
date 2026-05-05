//
// BacktestStrategyEngine: 独立的回测策略引擎
//
// 与实盘 StrategyEngine 完全独立，不共享基类。
// 策略通过 StrategyBase（接受 MyHandler*）与引擎交互，
// 不依赖实盘专属组件（MySqlGateway、MessageServer、OkxWs、HTTP 等）。
//

#pragma once

#include "pch.h"
#include <yaml-cpp/yaml.h>
#include "strategy_base.h"
#include "zrtools/io_pool_v2/io_pool.h"
#include "type_define.h"
#include "i_exchange_data_dump.h"
#include "order_manager.h"
#include "kline_manager.h"
#include "qry_srv.h"
#include "event_source_manager.h"
#include "msg_id_dump.h"
#include "global.h"
#include "logger_config.h"
#include "service_map.h"

class BacktestStrategyEngine final : public MyHandler {
public:
    BacktestStrategyEngine(ServiceMap& pool, const GTradeConfig& gtrade_cfg);
    ~BacktestStrategyEngine() override = default;

    bool Init() override;
    bool Start() override;

private:
    // 策略加载
    template<typename T>
    bool LoadStrategy(const std::string& strat_id, const T& strat_ptr, const YAML::Node& strat_yml) {
        SPDLOG_INFO("load strategy={}", strat_id);
        if (!m_strategy_map.count(strat_id)) {
            m_strategy_map.emplace(strat_id, strat_ptr);
            m_strategy_map.at(strat_id)->OnInit(strat_yml);
        } else {
            SPDLOG_ERROR("strat={} already exist", strat_id);
            return false;
        }
        return true;
    }
    bool LoadSingleStrategyFromFile(const std::string& cfg_path);
    bool LoadStrategyCfgFromFile();
    /**
     * 从 .so 动态库加载策略实例（回测版本）。
     * 与实盘引擎逻辑相同，但校验构建模式为回测（gtrade_get_build_mode() == 1）。
     * @param so_path   .so 文件路径（相对路径以可执行文件目录为基准）
     * @param strat_id  策略 ID
     * @return 成功返回非空 shared_ptr，失败返回 nullptr
     */
    std::shared_ptr<StrategyBase> LoadStrategyFromSo(
        const std::string& so_path,
        const std::string& strat_id);

    // 消息转发
    void SendToStrategy(int msg_id, const BufPtr buffer, const std::string& strat_id);
    void SendToAllStrategies(int msg_id, const BufPtr buffer);
    template<typename T>
    void SendToSubedStrategies(const int msg_id, const BufPtr buffer, const T& container) {
        for (const auto& strat_id : container) {
            SendToStrategy(msg_id, buffer, strat_id);
        }
    }

    // 消息处理器
    void OnDefaultMsg(int msg_id, const BufPtr buffer);

    // 订阅
    void OnSubscribeQuote(int msg_id, const BufPtr buffer);
    void OnSubscribeKLine(int msg_id, const BufPtr buffer);
    void OnSubscribeKLineOpen(int msg_id, const BufPtr buffer);
    void OnSubscribeKLineClose(int msg_id, const BufPtr buffer);
    void OnSubscribeTrade(int msg_id, const BufPtr buffer);
    bool EnsureTradeGateway(const std::string& market, const std::string& account_id);

    // 行情
    void OnDepth1(int msg_id, const BufPtr buffer);

    // 下单 / 撤单
    void FillNewEntByReq(Order& dst, const OrderReq& src);
    static void FillOrderByOrder(Order& dst, const Order& src);
    bool FillSide(Order& order, std::string_view account_id, std::string_view instrument,
                  char trade_mode, char pos_side, double entamt);
    bool FillSide(Order& order);
    void OnPlaceOrderReq(int msg_id, const BufPtr buffer);
    void OnPlaceOrderRsp(int msg_id, const BufPtr buffer);
    void OnCancelOrderReq(int msg_id, const BufPtr buffer);
    void OnCancelOrderRsp(int msg_id, const BufPtr buffer);

    // 交易推送
    void OnPlaceOrderConfirm(int msg_id, const BufPtr buffer);
    void CreateTrade(const Order& local_order, const Order& recv_order);
    void OnTradePush(int msg_id, const BufPtr buffer);
    void OnPosPush(int msg_id, const BufPtr buffer);
    void OnBalancePush(int msg_id, const BufPtr buffer);

    // K线查询
    void OnQueryKLineReq(int msg_id, const BufPtr buffer);
    void OnQueryKLinePatchRsp(int msg_id, const BufPtr buffer);
    void OnQueryKLineRsp(int msg_id, const BufPtr buffer);

    // 委托查询
    void OnHandleQueryOrderReq(int msg_id, const BufPtr buffer);
    void OnQueryOrderByPrivateNoReq(int msg_id, const BufPtr buffer);

    // 定时器
    void OnSetTimer(int msg_id, const BufPtr buffer);
    void OnHandleKillTimerReq(int msg_id, const BufPtr buffer);
    void OnHandleClearAllTimerReq(int msg_id, const BufPtr buffer);
    void OnHandleTimerEvent(int msg_id, const BufPtr buffer);

    // 指标推送
    void OnIndicatorKlinePush(int msg_id, const BufPtr buffer);
    void OnIndicatorKlineOpenPush(int msg_id, const BufPtr buffer);
    void OnIndicatorKlineClosePush(int msg_id, const BufPtr buffer);

    // 策略信息（回测模式 no-op，不写数据库）
    void OnDbSetStrategyInfo(int msg_id, const BufPtr buffer);
    void OnDbSetStrategyLog(int msg_id, const BufPtr buffer);
    // 通知消息（回测模式 no-op）
    void OnNotifyMsg(int msg_id, const BufPtr buffer);

    // =========================================================
    // 状态
    // =========================================================

    ServiceMap& m_pool;
    MyHandler* m_qry_srv {};
    MyHandler* m_timer_manager {};
    const GTradeConfig m_gtrade_cfg {};

    // 策略管理
    std::unordered_map<std::string, std::shared_ptr<StrategyBase>> m_strategy_map {};
    std::unordered_map<std::string, std::shared_ptr<MyHandler>> m_trade_gw_map {};
    std::unordered_map<std::string, std::string> m_strategy_cfg_path_map {};
    std::unordered_map<std::string, std::string> m_strategy_template_map {};
    std::unordered_map<std::string, std::unordered_set<std::string>> m_template_strategy_map {};

    // 委托与持仓
    OrderManager m_order_manager {};
    std::unique_ptr<KLineManager> m_kline_manager {};

    // 回测专属
    std::unique_ptr<EventSourceManager> m_event_source_manager {};

    // 订阅映射
    std::unordered_map<std::tuple<std::string,std::string,std::string>,
                       std::unordered_set<std::string>, zrt::TupleHasher> m_quote_sub_map {};
    std::unordered_map<std::tuple<std::string,std::string,int,char>,
                       std::unordered_set<std::string>, zrt::TupleHasher> m_kline_sub_map {};
    std::unordered_map<std::tuple<std::string,std::string,int,char>,
                       std::unordered_set<std::string>, zrt::TupleHasher> m_kline_open_sub_map {};
    std::unordered_map<std::tuple<std::string,std::string,int,char>,
                       std::unordered_set<std::string>, zrt::TupleHasher> m_kline_close_sub_map {};
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::unordered_set<std::string>>> m_trade_sub_map {};

    // private_no 映射
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> m_private_no_map {};
    // <req_id,strat_id> K线查询请求映射
    std::unordered_map<int64_t, std::string> m_req_id_map {};
};
