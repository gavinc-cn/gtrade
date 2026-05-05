//
// Created by dell on 2024/4/11.
//

#pragma once

#include "pch.h"
#include <yaml-cpp/yaml.h>
#include <atomic>
#include "strategy_base.h"
#include "i_strategy_proxy.h"
#include "in_process_proxy.h"
#include "out_process_proxy.h"
#include "zmq_acceptor.h"
#include "remote_proxy.h"
#include "zrtools/io_pool_v2/io_pool.h"
#include "type_define.h"
#include "i_exchange_data_dump.h"
#include "zrtools/zrt_bmic_hashed.h"
#include "order_manager.h"
#include "kline_manager.h"
#include "qry_srv.h"
#include "timer_manager.h"
#include "event_source_manager.h"
#include "msg_id_dump.h"
#include "global.h"
#include "logger_config.h"

enum class DataStatus {
    kNotReady,
    kReqSent,
    kReady,
};


class StrategyEngine final : public MyHandler {
public:
    StrategyEngine(ServiceMap& pool, const GTradeConfig& gtrade_cfg):
    m_pool(pool),
    m_gtrade_cfg(gtrade_cfg)
    {
        // 回测
        if constexpr (!GlobalConst::IsRealTrading) {
            SetThread(zrt::EnginePool::GetInstance().GetNamedThread(k_BackTestThread));
        }
        // 实盘
        else {
            SetThread(zrt::EnginePool::GetInstance().GetNamedThread(k_StrategyEngineThread));
        }
    }
    ~StrategyEngine() override = default;

    bool Init() override;
    bool Start() override;
    void Stop() override;

    template<typename T>
    bool LoadStrategy(const std::string& strat_id, const T& strat_ptr, const YAML::Node& strat_yml) {
        SPDLOG_INFO("load strategy={}", strat_id);
        if (m_strategy_proxy_map.count(strat_id)) {
            SPDLOG_ERROR("strat={} already exist", strat_id);
            return false;
        }
        // 安装生命周期处理器（kStratStartSync / kStratStopSync），然后执行策略自身的初始化
        strat_ptr->Init();
        if (!static_cast<StrategyBase*>(strat_ptr.get())->OnInit(strat_yml)) {
            SPDLOG_ERROR("strat={} OnInit failed", strat_id);
            return false;
        }
        // 包装为 InProcessProxy 并插入代理映射
        auto proxy = std::make_unique<InProcessProxy>(strat_ptr, strat_id);
        m_strategy_proxy_map.emplace(strat_id, std::move(proxy));
        return true;
    }

    bool LoadStrategyCfgFromFile();

    // 策略管理接口
    bool AddStrategy(const std::string& cfg_path);
    bool DeleteStrategy(const std::string& strat_id);
    bool RestartStrategy(const std::string& strat_id);
    bool StartStrategy(const std::string& strat_id);
    bool StopStrategy(const std::string& strat_id);
    std::string QueryAllStrategies() const;
    std::string QueryStrategiesByTemplate(const std::string& template_name) const;
    std::vector<std::string> GetLoadedStrategyTemplates() const;
    std::vector<std::string> GetAllStrategyTemplates() const;
    std::string GetTemplateConfig(const std::string& template_name) const;

private:
    // 定时器 ID 定义
    struct TimerId {
        enum {
            kTimer_UpdateUpl = 1001,         // 策略持仓 UPL 更新定时器
            kTimer_RefreshMarketInfo = 1002, // 市场信息刷新定时器（每小时）
        };
    };

    bool LoadSingleStrategyFromFile(const std::string& cfg_path);
    /**
     * 从 .so 动态库加载策略实例。
     * 使用 dlopen(RTLD_LAZY | RTLD_GLOBAL) 加载，通过 C 工厂接口创建对象，
     * 返回带自定义 deleter（自动 dlclose）的 shared_ptr。
     * @param so_path   .so 文件路径（相对路径以可执行文件目录为基准）
     * @param strat_id  策略 ID
     * @return 成功返回非空 shared_ptr，失败返回 nullptr
     */
    std::shared_ptr<StrategyBase> LoadStrategyFromSo(
        const std::string& so_path,
        const std::string& strat_id);
    void LoadRecentOrdersFromDb();

    // 从共享内存扫描并加载策略
    // 扫描 /dev/shm/ 目录中的检查点文件，根据策略ID查找配置文件并加载
    void LoadStrategiesFromShm();
    void SetupUplUpdateTimer();  // 设置 UPL 定时更新
    void SetupMarketInfoRefreshTimer() const;  // 设置市场信息定时刷新（每小时）
    void RefreshAllMarketInfo(bool is_sync);  // 刷新所有已知市场的信息
    void OnDefaultMsg(int msg_id, const BufPtr buffer);
    void OnDefaultSyncMsg(int msg_id, const BufPtr buffer, std::promise<BufPtr >& ret);
    std::string BuildQueryMarketInfoBuf(const BufPtr& buffer, BufPtr& rsp_buffer);
    void OnStratQueryMarketInfoReq(int msg_id, BufPtr buffer);
    void OnStratQueryMarketInfoSync(int msg_id, const BufPtr buffer, std::promise<BufPtr>& ret);
    // 外发通知消息
    void OnNotifyMsg(int msg_id, const BufPtr buffer) const;
    // 订阅
    void OnSubscribeQuote(int msg_id, const BufPtr buffer);
    void OnSubscribeKLine(int msg_id, const BufPtr buffer);
    void OnSubscribeKLineOpen(int msg_id, const BufPtr buffer);
    void OnSubscribeKLineClose(int msg_id, const BufPtr buffer);
    bool EnsureTradeGateway(const std::string& market, const std::string& account_id);
    void OnSubscribeTrade(int msg_id, const BufPtr buffer);
    // 下单
    void FillNewEntByReq(Order& dst, const OrderReq& src);
    static void FillOrderByOrder(Order& dst, const Order& src);
    bool FillSide(Order& order, const std::string_view account_id, const std::string_view instrument, const char trade_mode, const char pos_side, const double entamt);
    bool FillSide(Order& order);
    void OnPlaceOrderReq(int msg_id, const BufPtr buffer);
    void OnPlaceOrderRsp(int msg_id, const BufPtr buffer);
    // 撤单
    void OnCancelOrderReq(int msg_id, const BufPtr buffer);
    void OnCancelOrderRsp(int msg_id, const BufPtr buffer);
    // 交易推送
    void OnPlaceOrderConfirm(int msg_id, const BufPtr buffer);
    void OnOrderRecovery(int msg_id, const BufPtr buffer);  // websocket重连后的委托恢复
    void CreateTrade(const Order& local_order, const Order& recv_order);
    void OnTradePush(int msg_id, const BufPtr buffer);
    void OnPosPush(int msg_id, const BufPtr buffer);
    void OnBalancePush(int msg_id, const BufPtr buffer);
    // 行情
    void OnDepth1(int msg_id, const BufPtr buffer);
    // 策略查询
    void OnQueryKLineReq(int msg_id, const BufPtr buffer);
    void OnHandleQueryOrderReq(int msg_id, const BufPtr buffer);
    void OnHandleQueryServerReq(int msg_id, const BufPtr buffer);
    // 查询应答
    void OnQueryMarketInfoRsp(int msg_id, const BufPtr buffer);
    void OnQueryOrderRsp(int msg_id, const BufPtr buffer);
    void OnQueryOrderByPrivateNoReq(int msg_id, const BufPtr buffer);
    void OnQueryTradeRsp(int msg_id, const BufPtr buffer);
    void OnQueryPosRsp(int msg_id, const BufPtr buffer);
    void OnQueryBalanceRsp(int msg_id, const BufPtr buffer);
    void OnQueryKLinePatchRsp(int msg_id, const BufPtr buffer);
    void OnQueryKLineRsp(int msg_id, const BufPtr buffer);
    void OnQueryHisOrdersRsp(int msg_id, const BufPtr buffer);
    // 控制消息
    void OnWebSocketOpenNotify(int msg_id, const BufPtr buffer);
    void OnSetTimer(int msg_id, const BufPtr buffer);
    void OnHandleKillTimerReq(int msg_id, const BufPtr buffer);
    void OnHandleClearAllTimerReq(int msg_id, const BufPtr buffer);
    void OnHandleTimerEvent(int msg_id, const BufPtr buffer);
    // 指标
    void OnIndicatorKlinePush(int msg_id, const BufPtr buffer);
    void OnIndicatorKlineOpenPush(int msg_id, const BufPtr buffer);
    void OnIndicatorKlineClosePush(int msg_id, const BufPtr buffer);
    // HTTP策略管理
    void OnHttpAddStrategy(int msg_id, const BufPtr buffer, std::promise<BufPtr>& ret);
    void OnHttpDeleteStrategy(int msg_id, const BufPtr buffer, std::promise<BufPtr>& ret);
    void OnHttpRestartStrategy(int msg_id, const BufPtr buffer, std::promise<BufPtr>& ret);
    void OnHttpStartStrategy(int msg_id, const BufPtr buffer, std::promise<BufPtr>& ret);
    void OnHttpStopStrategy(int msg_id, const BufPtr buffer, std::promise<BufPtr>& ret);
    void OnHttpQueryAllStrategies(int msg_id, const BufPtr buffer, std::promise<BufPtr>& ret);
    void OnHttpQueryStrategiesByTemplate(int msg_id, const BufPtr buffer, std::promise<BufPtr>& ret);
    void OnHttpGetTemplates(int msg_id, const BufPtr buffer, std::promise<BufPtr>& ret);
    void OnHttpGetTemplateConfig(int msg_id, const BufPtr buffer, std::promise<BufPtr>& ret);
    // HTTP系统管理
    void OnHttpSaveSnapshot(int msg_id, const BufPtr buffer, std::promise<BufPtr>& ret);
    void OnHttpGetWalStats(int msg_id, const BufPtr buffer, std::promise<BufPtr>& ret);
    // 数据库操作
    void OnDbSetStrategyInfo(int msg_id, const BufPtr buffer);
    void OnDbSetStrategyLog(int msg_id, const BufPtr buffer);

    // 远程策略 ZMQ 事件处理（AcceptLoop 线程 PostMsg 到引擎线程）
    void OnRemoteStrategyConnected(int msg_id, const BufPtr buffer);
    void OnRemoteChannelB(int msg_id, const BufPtr buffer);
    void OnRemoteSyncReq(int msg_id, const BufPtr buffer);

    // 转发给特定的策略
    void SendToStrategy(int msg_id, const BufPtr buffer, const std::string& strat_id);
    // 转发给所有策略
    void SendToAllStrategies(int msg_id, const BufPtr buffer);
    // 发给订阅的策略
    template<typename T>
    void SendToSubedStrategies(const int msg_id, const BufPtr buffer, const T& container) {
        for (const auto& strat_id: container) {
            SendToStrategy(msg_id, buffer, strat_id);
        }
    }

    ServiceMap& m_pool;
    MyHandler* m_qry_srv {};
    MyHandler* m_timer_manager {};
    MyHandler* m_csv_quote {};
    MyHandler* m_msg_srv {};
    MyHandler* m_mysql_gateway {};

    const GTradeConfig m_gtrade_cfg {};

    // ── 远程策略 ZMQ 接受器 ──────────────────────────────────────────────────
    // ZMQ context 独立于 EnginePool，只在 remote_engine_port > 0 时创建
    std::unique_ptr<zmq::context_t>  m_zmq_context;
    std::unique_ptr<ZmqAcceptor>     m_zmq_acceptor;

    // 策略管理（Proxy 架构：InProcessProxy / OutProcessProxy / RemoteProxy）
    std::unordered_map<std::string,std::unique_ptr<IStrategyProxy>> m_strategy_proxy_map {};
    // 交易网关管理
    std::unordered_map<std::string,std::shared_ptr<MyHandler>> m_trade_gw_map {};
    // 策略ID到配置文件路径的映射
    std::unordered_map<std::string,std::string> m_strategy_cfg_path_map {};
    // 策略ID到模板ID的映射
    std::unordered_map<std::string,std::string> m_strategy_template_map {};
    // 模板ID到策略ID集合的映射
    std::unordered_map<std::string,std::unordered_set<std::string>> m_template_strategy_map {};
    // 订单管理
    OrderManager m_order_manager {};
    // K线管理
    std::unique_ptr<KLineManager> m_kline_manager {};
    // 回测事件管理
    std::unique_ptr<EventSourceManager> m_event_source_manager {};

    /// 订阅信息管理 ///
    // <channel,market,inst_id>,<strat_id>> 行情订阅信息
    std::unordered_map<std::tuple<std::string,std::string,std::string>,std::unordered_set<std::string>,zrt::TupleHasher> m_quote_sub_map {};
    // <market,instrument,coefficient,scale>,<strat_id>> K线订阅信息
    std::unordered_map<std::tuple<std::string,std::string,int,char>,std::unordered_set<std::string>,zrt::TupleHasher> m_kline_sub_map {};
    // <market,instrument,coefficient,scale>,<strat_id>> K线open订阅信息
    std::unordered_map<std::tuple<std::string,std::string,int,char>,std::unordered_set<std::string>,zrt::TupleHasher> m_kline_open_sub_map {};
    // <market,instrument,coefficient,scale>,<strat_id>> K线close订阅信息
    std::unordered_map<std::tuple<std::string,std::string,int,char>,std::unordered_set<std::string>,zrt::TupleHasher> m_kline_close_sub_map {};
    // <account_id,<instrument,<strat_id>>> 交易订阅信息
    std::unordered_map<std::string,std::unordered_map<std::string,std::unordered_set<std::string>>> m_trade_sub_map {};
    ///

    // <market,<instrument,MarketInfo>>
    std::unordered_map<std::string,std::unordered_map<std::string,MarketInfo>> m_market_info_map {};

    // <market,<instrument,last_price>> 最新行情价格（用于更新未实现收益）
    std::unordered_map<std::string,std::unordered_map<std::string,double>> m_last_price_map {};

    /// 数据状态管理 ///
    // <market,DataStatus>
    std::unordered_map<std::string,DataStatus> m_market_info_status {};
    // <account_id,DataStatus>
    std::unordered_map<std::string,DataStatus> m_balance_status {};
    // <account_id,DataStatus>
    std::unordered_map<std::string,DataStatus> m_pos_status {};
    // <account_id,DataStatus>
    std::unordered_map<std::string,DataStatus> m_order_status {};
    ///

    // <req_id,strat_id> 策略请求的映射关系
    std::unordered_map<int64_t,std::string> m_req_id_map {};
    // <strat_id,<private_no,entno>> private_no到entno的映射
    std::unordered_map<std::string,std::unordered_map<std::string,int64_t>> m_private_no_map {};

    // ========== HA 相关 ==========
    // 序号追踪（用于策略检查点）
    std::atomic<uint64_t> m_market_seq {0};  // 行情序号
    std::atomic<uint64_t> m_order_seq {0};   // 订单序号

    // HA 配置
    HAConfig m_ha_config {};

public:
    // ========== HA 相关方法 ==========

    /**
     * 设置 HA 配置
     */
    void SetHAConfig(const HAConfig& config) { m_ha_config = config; }

    /**
     * 获取 HA 配置
     */
    const HAConfig& GetHAConfig() const { return m_ha_config; }

    /**
     * 获取当前行情序号
     */
    uint64_t GetMarketSeq() const { return m_market_seq.load(std::memory_order_relaxed); }

    /**
     * 获取当前订单序号
     */
    uint64_t GetOrderSeq() const { return m_order_seq.load(std::memory_order_relaxed); }

    /**
     * 获取 OrderManager 引用（用于 HA 服务）
     */
    OrderManager& GetOrderManager() { return m_order_manager; }

    /**
     * 初始化 HA 相关组件
     */
    void InitHA();

private:
    /**
     * 分配行情序号
     */
    uint64_t AllocateMarketSeq() {
        return m_market_seq.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    /**
     * 分配订单序号
     */
    uint64_t AllocateOrderSeq() {
        return m_order_seq.fetch_add(1, std::memory_order_relaxed) + 1;
    }
};
