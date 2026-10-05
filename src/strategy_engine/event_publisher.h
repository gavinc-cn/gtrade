//
// EventPublisher —— 引擎侧统一发布出口（引擎 → web_server）
//
// 方案：doc_ai/plan/202610/20261004_1300_二通道推送与补查方案_rev4.md §5
//   - 事件源在**引擎线程**上（kPlaceOrderConfirm/kTradePush/kPositionPush/kBalancePush
//     都已按序到达引擎线程），本类只做"组装帧 + 投递"，不做业务判断；
//   - 传输用 WsPushClient（明文 ws://、不重放、JSON 应用层心跳）；
//   - **无订阅者零开销**：Ready() 不做任何组装工作（先判后建）；
//   - 对端（web_server）通过 subscribe 帧声明要哪些通道；未订阅的事件直接丢弃；
//   - query 帧在读线程到达 → 通过 QueryHandler 回到引擎线程取数（PostSyncMsg）→ 回 result 帧，
//     因此"补查结果"与"推送事件"在引擎侧天然同序（单线程 + 同一条连接）。
//
// 线程模型：Publish* 由引擎线程调用；OnPeerFrame 由 WsPushClient 的 io 线程调用；
//           HandleQuery 会阻塞 io 线程等引擎线程应答（查询只在重连时发生，可接受）。
//
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "db_structures.h"
#include "i_exchange_data.h"     // Depth
#include "i_strategy_engine.h"   // StrategyInfo / HttpQuery*Req
#include "type_define.h"
// 直接包含而非前向声明：unique_ptr<WsPushClient> 的析构需要完整类型，
// 而 StrategyEngine 按值持有 EventPublisher（含 StrategyEngine 的所有 TU 都要能析构）
#include "websocket/ws_push_client.h"

// 对端（web_server）可订阅的通道
enum class PushChannel : uint32_t {
    None  = 0,
    Trade = 1u << 0,   // 委托/成交/持仓/资金
    Quote = 1u << 1,   // 盘口快照（尚未实现，见 rev4 §9 阶段划分）
};

class EventPublisher {
public:
    // 查询回调：把请求投到引擎线程并同步取回响应（由 StrategyEngine 注入 PostSyncMsg 包装）
    using QueryHandler = std::function<BufPtr(int msg_id, const BufPtr& req)>;

    EventPublisher() = default;
    ~EventPublisher();

    EventPublisher(const EventPublisher&) = delete;
    EventPublisher& operator=(const EventPublisher&) = delete;

    void Init(const WebPushConfig& cfg);
    void SetQueryHandler(QueryHandler handler);
    void Start();
    void Stop();

    bool Enabled() const noexcept { return m_cfg.enabled; }

    // 是否可以投递该通道的事件（连接就绪 + 对端已订阅）；false 时调用方应立刻返回
    bool Ready(PushChannel channel) const noexcept;

    // ===== 事件源（引擎线程调用）=====
    void PublishOrder(const Order& order);
    void PublishTrade(const Trade& trade);
    void PublishPosition(const Position& pos);
    void PublishBalance(const Balance& bal);
    // 盘口快照（quote 通道）：按标的限频（quote_min_interval_ms），**只由引擎线程调用**
    void PublishDepth(const Depth& depth);
    // 策略信息/指标（trade 通道的 snapshot）：param/indicator 为 JSON 字符串，由 web_server 解析成对象
    void PublishStrategyInfo(const StrategyInfo& info);

private:
    void OnPeerFrame(const std::string& payload);
    void HandleSubscribe(const std::string& payload);
    void HandleQuery(const std::string& payload);
    void SendRaw(const std::string& json);

    WebPushConfig m_cfg {};
    QueryHandler m_query_handler {};
    std::unique_ptr<WsPushClient> m_client {};
    std::atomic<uint32_t> m_sub_mask {0};      // 对端订阅的通道位掩码
    std::atomic<uint64_t> m_published {0};     // 已投递帧数（诊断）
    std::atomic<uint64_t> m_dropped_no_sub {0};
    // 盘口限频账本：<market:symbol, 上次投递(单调 ms)>；只在引擎线程读写，无需加锁
    std::unordered_map<std::string, int64_t> m_last_depth_ms {};
};
