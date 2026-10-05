//
// Created by dell on 2025/2/20.
//

#pragma once

#include <yaml-cpp/yaml.h>
#include <memory>
#include <atomic>
#include "type_define.h"
#include "i_strategy_engine_dump.h"
#include "i_exchange_data.h"
#include "db_structures_dump.h"
#include "i_client.h"
#include "i_timer_manager.h"
#include "define.h"
#include "sonic_helper.h"
#include "zrtools/fmt_helper.h"
#include "string_keys.h"
#include "msg_id_dump.h"
#include "my_utc.h"
#include "strategy_checkpoint.h"
#include "zrtools/zrt_shm_checkpoint.h"
#include "zrtools/zrt_time.h"
#include "i_strategy_channel.h"  // RemoteSyncResp（远程模式对账结构体）

// 策略日志宏：同时输出到主日志(带策略ID前缀)和策略专用日志
// 主日志故意降级为DEBUG，避免主日志过于冗长
#define RLOG_TRACE(...) do { \
    const auto _rlog_msg_ = zrt::safe_fmt(__VA_ARGS__); \
    SPDLOG_TRACE("[{}] {}", GetStratId(), _rlog_msg_); \
    SPDLOG_LOGGER_TRACE(GetLogger(), "{}", _rlog_msg_); \
} while(0)

#define RLOG_DEBUG(...) do { \
    const auto _rlog_msg_ = zrt::safe_fmt(__VA_ARGS__); \
    SPDLOG_DEBUG("[{}] {}", GetStratId(), _rlog_msg_); \
    SPDLOG_LOGGER_DEBUG(GetLogger(), "{}", _rlog_msg_); \
} while(0)

#define RLOG_INFO(...) do { \
    const auto _rlog_msg_ = zrt::safe_fmt(__VA_ARGS__); \
    SPDLOG_DEBUG("[{}] {}", GetStratId(), _rlog_msg_); \
    SPDLOG_LOGGER_INFO(GetLogger(), "{}", _rlog_msg_); \
} while(0)

#define RLOG_WARN(...) do { \
    const auto _rlog_msg_ = zrt::safe_fmt(__VA_ARGS__); \
    SPDLOG_DEBUG("[{}] {}", GetStratId(), _rlog_msg_); \
    SPDLOG_LOGGER_WARN(GetLogger(), "{}", _rlog_msg_); \
} while(0)

#define RLOG_ERROR(...) do { \
    const auto _rlog_msg_ = zrt::safe_fmt(__VA_ARGS__); \
    SPDLOG_DEBUG("[{}] {}", GetStratId(), _rlog_msg_); \
    SPDLOG_LOGGER_ERROR(GetLogger(), "{}", _rlog_msg_); \
} while(0)

#define RLOG_CRITICAL(...) do { \
    const auto _rlog_msg_ = zrt::safe_fmt(__VA_ARGS__); \
    SPDLOG_DEBUG("[{}] {}", GetStratId(), _rlog_msg_); \
    SPDLOG_LOGGER_CRITICAL(GetLogger(), "{}", _rlog_msg_); \
} while(0)

#define STRAT_NOTIFY_MSG(channel, ...) do { \
    const auto _notify_msg_ = zrt::safe_fmt(__VA_ARGS__); \
    RLOG_INFO("{}", _notify_msg_); \
    SendNotifyMsg(channel, GetStratId(), _notify_msg_); \
} while(0)

/**
 * 策略检查点辅助类
 *
 * 模板参数 UserData 是策略自定义的检查点数据结构
 * 内部使用 CheckpointWrapper<UserData> 包装，自动管理通用字段
 *
 * 使用示例:
 * @code
 * struct MyCheckpointData {
 *     double cur_pos;
 *     int status;
 * };
 *
 * class MyStrategy : public StrategyBase {
 *     StrategyCheckpointHelper<MyCheckpointData> m_checkpoint;
 *
 *     bool OnInit(...) {
 *         m_checkpoint.Init(GetStratId());
 *         if (m_checkpoint.TryRestore()) {
 *             // 恢复状态
 *             auto& data = m_checkpoint.GetData();
 *             m_pos = data.cur_pos;
 *         }
 *     }
 *
 *     void OnOrderFilled() {
 *         auto& data = m_checkpoint.GetData();
 *         data.cur_pos = m_pos;
 *         m_checkpoint.Commit();
 *     }
 * };
 * @endcode
 */
template<typename UserData>
class StrategyCheckpointHelper {
public:
    using Checkpoint = gtrade::CheckpointWrapper<UserData>;

    StrategyCheckpointHelper() = default;

    /**
     * 初始化检查点
     *
     * @param strat_id 策略 ID，用于生成共享内存名称
     * @return true 如果初始化成功
     */
    bool Init(const std::string& strat_id) {
        if constexpr (!GlobalConst::IsRealTrading) {
            SPDLOG_INFO("Checkpoint disabled in backtest mode");
            return false;
        }

        m_strat_id = strat_id;
        const std::string shm_name = gtrade::GetStrategyCheckpointShmName(strat_id);

        try {
            m_shm = std::make_unique<zrt::ShmCheckpoint<Checkpoint>>(shm_name);
            if (m_shm->IsValid()) {
                SPDLOG_INFO("Checkpoint initialized: {}", shm_name);
                return true;
            }
            SPDLOG_ERROR("Checkpoint initialization failed: {}", shm_name);
            m_shm.reset();
            return false;
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Checkpoint initialization exception: {}", e.what());
            m_shm.reset();
            return false;
        }
    }

    /**
     * 尝试从检查点恢复
     *
     * 成功后可通过 GetData() 访问恢复的数据
     *
     * @return true 如果成功恢复
     */
    bool TryRestore() {
        if (!IsValid()) {
            return false;
        }

        if (!m_shm->Read(m_cached)) {
            SPDLOG_INFO("No valid checkpoint data found");
            return false;
        }

        if (!m_cached.ValidateChecksum()) {
            SPDLOG_WARN("Checkpoint checksum validation failed");
            return false;
        }

        if (m_cached.strategy_id != gtrade::HashStrategyId(m_strat_id)) {
            SPDLOG_WARN("Checkpoint strategy ID mismatch");
            return false;
        }

        SPDLOG_INFO("Checkpoint restored: market_seq={}, order_seq={}",
                    m_cached.market_seq, m_cached.order_seq);
        return true;
    }

    /**
     * 提交检查点
     *
     * 将当前缓存的数据写入共享内存
     */
    void Commit() {
        if (!IsValid()) {
            return;
        }

        m_cached.version = 1;
        m_cached.strategy_id = gtrade::HashStrategyId(m_strat_id);

        // 获取当前时间戳（纳秒）
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        m_cached.timestamp_ns = static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
                                static_cast<uint64_t>(ts.tv_nsec);

        m_cached.UpdateChecksum();
        m_shm->Write(m_cached);

        SPDLOG_DEBUG("Checkpoint committed: market_seq={}, order_seq={}",
                     m_cached.market_seq, m_cached.order_seq);
    }

    /**
     * 获取可写的用户数据引用
     *
     * 修改后调用 Commit() 持久化
     */
    UserData& GetData() { return m_cached.data; }

    /**
     * 获取只读的用户数据引用
     */
    const UserData& GetData() const { return m_cached.data; }

    /**
     * 获取/设置行情序号
     */
    uint64_t GetMarketSeq() const { return m_cached.market_seq; }
    void SetMarketSeq(uint64_t seq) { m_cached.market_seq = seq; }

    /**
     * 获取/设置订单序号
     */
    uint64_t GetOrderSeq() const { return m_cached.order_seq; }
    void SetOrderSeq(uint64_t seq) { m_cached.order_seq = seq; }

    /**
     * 获取/设置策略状态
     */
    gtrade::StrategyStatus GetStatus() const { return m_cached.status; }
    void SetStatus(gtrade::StrategyStatus status) { m_cached.status = status; }

    /**
     * 检查是否初始化成功
     */
    bool IsValid() const { return m_shm && m_shm->IsValid(); }

private:
    std::string m_strat_id;
    std::unique_ptr<zrt::ShmCheckpoint<Checkpoint>> m_shm;
    Checkpoint m_cached {};
};

class StrategyBase: public MyHandler {
public:
    StrategyBase(const GTradeConfig& gtrade_cfg, MyHandler* strat_engine, const std::string& strat_id):
    m_gtrade_cfg(gtrade_cfg),
    m_strat_engine(strat_engine)
    {
        zrt::fill_field(m_strat_info.id, strat_id);
        zrt::fill_field(m_strat_info.strat_name, strat_id);
        zrt::fill_field(m_strat_info.status, StrategyEnvStatus::Stopped);
        zrt::fill_field(m_strat_info.indicator, "{}");
        zrt::fill_field(m_strat_info.param, "{}");
        zrt::fill_field(m_strat_info.create_time, MyUTC().Epoch19());
        zrt::fill_field(m_strat_info.update_time, MyUTC().Epoch19());

        if constexpr (!GlobalConst::IsRealTrading) {
            SetThread(zrt::EnginePool::GetInstance().GetNamedThread(k_BackTestThread));
        }
        else {
#ifdef GTRADE_LATENCY_SINGLE_THREAD
            // 方案C：实盘策略绑 k_StrategyEngineThread 指向的 SyncThread（与 Engine/DpdkTradeSink 同）。
            // SyncThread.Post 同步执行 task()，故 Engine↔策略↔DpdkTradeSink 间任意 SyncThread 实例的
            // PostMsg 都在调用方线程同步跑，整条链落在 DpdkQuoteSource 的 rx busy-poll 核上（0 跨线程）。
            SetThread(zrt::EnginePool::GetInstance().GetNamedThread(k_StrategyEngineThread));
#else
            SetThread(zrt::EnginePool::GetInstance().GetSharedThread());
#endif
        }
    }

    ~StrategyBase() override = default;

    bool Init() override {
        SPDLOG_INFO("");
        ZRT_ADD_SYNC_HANDLER(kStratStartSync, StrategyBase::OnStratStartSync);
        ZRT_ADD_SYNC_HANDLER(kStratStopSync, StrategyBase::OnStratStopSync);
        return true;
    }

    bool Start() override {
        SPDLOG_INFO("");
        BufPtr rsp_buf {};
        PostSyncMsg(kStratStartSync, BufPtr{}, rsp_buf);
        return rsp_buf && rsp_buf->RefData<HttpStrategyOperationRsp>().success;
    }

    bool StopSync() {
        SPDLOG_INFO("");
        BufPtr rsp_buf {};
        PostSyncMsg(kStratStopSync, BufPtr{}, rsp_buf);
        return rsp_buf && rsp_buf->RefData<HttpStrategyOperationRsp>().success;
    }

    void Stop() override {
        if (!StopSync()) {
            SPDLOG_ERROR("strategy={} stop failed", GetStratId());
        }
    }

    virtual bool OnInit(const YAML::Node& strat_yml) = 0;
    virtual bool OnStart() = 0;
    virtual bool OnStop() = 0;
    virtual bool OnPause() = 0;
    virtual bool OnResume() = 0;

    /**
     * 远程模式断线重连对账回调（可选实现）。
     *
     * 在 Runner 收到 kRemoteSyncResp 后由主循环调用。
     * resp 包含 Engine OrderManager 中该策略的全量活跃订单（不依赖 checkpoint 过滤），
     * 策略应对比本地状态、处理孤儿订单、补齐缺失的 fill 通知。
     *
     * 默认实现打印警告，远程模式策略应覆盖此方法。
     * 非远程模式（inprocess/subprocess）下此方法不会被调用。
     *
     * @param resp  Engine 返回的全量活跃订单快照
     */
    virtual void OnReconnected(const RemoteSyncResp& resp) {
        RLOG_WARN("OnReconnected not overridden, {} open orders from engine", resp.item_count);
    }

    // 标记策略正在被删除，阻止保存操作（由 StrategyEngine 调用）
    void MarkAsDeleting() { m_is_deleting.store(true, std::memory_order_release); }
    StrategyInfo CopyStratInfo() const {return m_strat_info;}

protected:
    std::string GetStratId() const {return m_strat_info.id;}
    const StrategyInfo& RefStratInfo() const {return m_strat_info;}
    std::shared_ptr<spdlog::logger> GetLogger() const {return m_logger;}
    void InitLogger(const YAML::Node& strat_yml);
    void Post2StratEngine(const int msg_type, const TBufferPtr buffer) const;
    /// 订阅
    void SubscribeQuote(const std::string& channel, const std::string& exchange, const std::string& symbol) const;
    void SubscribeTrade(const std::string& market, const std::string& account_id, const std::string& instrument) const;
    void SubscribeKLine(const std::string& market, const std::string& instrument, const int coefficient, const char scale) const;
    void SubscribeKLineOpen(const std::string& market, const std::string& instrument, const int coefficient, const char scale) const;
    void SubscribeKLineClose(const std::string& market, const std::string& instrument, const int coefficient, const char scale) const;
    /// 交易
    void PlaceOrderReq(OrderReq& entrust_req) const;
    void CancelOrderReq(const WithdrawReq& req) const;
    /// 查询
    void QueryKLineReq(const KLineQryReq& req) const;
    void QueryOrderReq(const EntrustQryReq& req) const;
    void QueryOrderReq(const std::string& private_no) const;
    void QueryOrderReq(const int64_t entno, const std::string& market, const std::string& account_id) const;
    void QueryMarketInfoSync(const StratQryMarketInfoReq& req, std::vector<MarketInfo>& market_infos) const;
    std::vector<MarketInfo> QueryMarketInfoSync(const std::string &market, const std::string &instrument) const;
    /// 定时器
    void SetTimer(const int timer_id, const int delay_ms, const bool repeat) const;
    void KillTimer(const int timer_id) const;
    void ClearAllTimer() const;
    // Slack消息
    void SendNotifyMsg(const std::string& channel, const std::string& head, const std::string& body) const;
    void SaveStrategyEnvStatus(StrategyEnvStatus status);
    // 保存策略参数到数据库 (支持从YAML::Node或JSON字符串)
    void SaveStrategyParam(const YAML::Node &param_yaml);
    void SaveStrategyParam(const std::string &param_json);
    // 保存策略指标到数据库 (仅支持JSON字符串)
    void SaveStrategyIndicator(const std::string& indicator_json);
    void SaveStrategyInfo() const;

    // JSON序列化工具函数
    static std::string YamlToJson(const YAML::Node& node);

    // 将任意可序列化对象转换为JSON (使用sonic) - 内联模板函数
    template<typename Func>
    static std::string SerializeToJson(Func&& serializer) {
        sonic_json::Document doc;
        doc.SetObject();
        auto& alloc = doc.GetAllocator();

        // 调用用户提供的序列化函数
        serializer(doc, alloc);

        sonic_json::WriteBuffer wb;
        doc.Serialize(wb);
        return std::string(wb.ToString());
    }

    const GTradeConfig m_gtrade_cfg {};
    StrategyInfo m_strat_info {};

    bool IsDeleting() const { return m_is_deleting.load(std::memory_order_acquire); }

private:
    // 生命周期同步消息处理（由引擎线程通过 PostSyncMsg 投递到策略线程执行）
    BufPtr OnStratStartSync(int msg_id, const BufPtr buffer);
    BufPtr OnStratStopSync(int msg_id, const BufPtr buffer);

    MyHandler* m_strat_engine {};
    std::shared_ptr<spdlog::logger> m_logger = spdlog::default_logger();
    std::atomic<bool> m_is_deleting{false};  // 策略是否正在被删除
    mutable uint16_t m_log_seq {0};          // 策略日志同纳秒内序号; 仅在策略线程调用，无需原子操作
};
