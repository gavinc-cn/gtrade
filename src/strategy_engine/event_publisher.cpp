#include "event_publisher.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "db_struct_json.h"
#include "i_strategy_engine.h"   // HttpQueryOrdersReq/HttpQueryTradesReq + kQueryMax*
#include "msg_id.h"
#include "my_utc.h"
#include "sonic_helper.h"
#include "spdlog/spdlog.h"
#include "websocket/ws_push_client.h"

namespace {

// 从 JSON 数字或字符串里取 19 位大整数（浏览器按字符串传，避免精度丢失；本机也可能是数字）
int64_t NodeToInt64(const sonic_json::Node& node) {
    if (node.IsString()) {
        return std::atoll(node.GetString().c_str());
    }
    if (node.IsInt64()) {
        return node.GetInt64();
    }
    if (node.IsNumber()) {
        return static_cast<int64_t>(node.GetDouble());
    }
    return 0;
}

}  // namespace

EventPublisher::~EventPublisher() {
    Stop();
}

void EventPublisher::Init(const WebPushConfig& cfg) {
    m_cfg = cfg;
    if (m_cfg.ping_ms <= 0) {
        m_cfg.ping_ms = 10000;
    }
}

void EventPublisher::SetQueryHandler(QueryHandler handler) {
    m_query_handler = std::move(handler);
}

void EventPublisher::Start() {
    if (!m_cfg.enabled) {
        SPDLOG_INFO("[EventPublisher] disabled by config (web_push.enabled=false), skip");
        return;
    }
    if (!m_client) {
        // 首帧 hello 带共享密钥（不走 URL，避免凭据进访问日志）
        const std::string hello = fmt::format(
            R"({{"type":"hello","role":"engine","secret":"{}","protocol":1}})", m_cfg.secret);
        m_client = std::make_unique<WsPushClient>(m_cfg.url, hello, m_cfg.ping_ms);
        m_client->SetMessageHandler([this](const std::string& json) { OnPeerFrame(json); });
    }
    m_client->Start();
    SPDLOG_INFO("[EventPublisher] started, url={}, ping={}ms", m_cfg.url, m_cfg.ping_ms);
}

void EventPublisher::Stop() {
    if (m_client) {
        m_client->Stop();
    }
    m_sub_mask.store(0, std::memory_order_relaxed);
}

bool EventPublisher::Ready(const PushChannel channel) const noexcept {
    if (!m_cfg.enabled || !m_client) {
        return false;
    }
    if ((m_sub_mask.load(std::memory_order_relaxed) & static_cast<uint32_t>(channel)) == 0) {
        return false;
    }
    return m_client->IsReady();
}

void EventPublisher::SendRaw(const std::string& json) {
    if (m_client && m_client->SendJson(json)) {
        m_published.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_dropped_no_sub.fetch_add(1, std::memory_order_relaxed);
    }
}

// ── 事件源：组装 event 帧（channel=trade）────────────────────────────────────

void EventPublisher::PublishOrder(const Order& order) {
    // 无订阅者零开销：先判后建，不进入任何序列化
    if (!Ready(PushChannel::Trade)) {
        return;
    }
    JsonObj json;
    json.AddMember("type", std::string("event"));
    json.AddMember("channel", std::string("trade"));
    json.AddMember("topic", std::string("order"));
    json.AddMember("ts", MyUTC().Epoch13());
    // 游标：客户端用它做实体守卫（只接受更新的 status_id）与断线补查
    json.AddMember("cursor", std::to_string(order.entno));
    json.AddMember("status_id", order.status_id);
    JsonObj data = json.AddObject("data");
    zrt::ToJson(data, order);
    SendRaw(static_cast<std::string>(json));
}

void EventPublisher::PublishTrade(const Trade& trade) {
    if (!Ready(PushChannel::Trade)) {
        return;
    }
    JsonObj json;
    json.AddMember("type", std::string("event"));
    json.AddMember("channel", std::string("trade"));
    json.AddMember("topic", std::string("trade"));
    json.AddMember("ts", MyUTC().Epoch13());
    json.AddMember("cursor", std::to_string(trade.tdno));
    JsonObj data = json.AddObject("data");
    zrt::ToJson(data, trade);
    SendRaw(static_cast<std::string>(json));
}

void EventPublisher::PublishPosition(const Position& pos) {
    if (!Ready(PushChannel::Trade)) {
        return;
    }
    // 持仓是"最新态"语义（快照帧，可按主键覆盖），因此不进增量补查
    JsonObj json;
    json.AddMember("type", std::string("snapshot"));
    json.AddMember("channel", std::string("trade"));
    json.AddMember("topic", std::string("position"));
    json.AddMember("ts", MyUTC().Epoch13());
    // 快照主键：客户端据此覆盖同一条持仓
    json.AddMember("key", fmt::format("{}|{}|{}|{}|{}", std::string(pos.market), std::string(pos.account_id),
                                      std::string(pos.instrument), pos.margin_mode.get(),
                                      pos.pos_side.get()));
    JsonObj data = json.AddObject("data");
    zrt::ToJson(data, pos);
    SendRaw(static_cast<std::string>(json));
}

void EventPublisher::PublishBalance(const Balance& bal) {
    if (!Ready(PushChannel::Trade)) {
        return;
    }
    JsonObj json;
    json.AddMember("type", std::string("snapshot"));
    json.AddMember("channel", std::string("trade"));
    json.AddMember("topic", std::string("balance"));
    json.AddMember("ts", MyUTC().Epoch13());
    json.AddMember("key", fmt::format("{}|{}|{}", std::string(bal.market),
                                      std::string(bal.account_id), std::string(bal.currency)));
    JsonObj data = json.AddObject("data");
    zrt::ToJson(data, bal);
    SendRaw(static_cast<std::string>(json));
}

void EventPublisher::PublishDepth(const Depth& depth) {
    if (!Ready(PushChannel::Quote)) {
        return;
    }
    // 按标的限频：盘口是"最新态"语义，中间 tick 没有价值（与 SSE 侧 depth_min_push_ms 一致）。
    // 引擎线程独占 m_last_depth_ms，无需加锁。
    const int64_t now = MyUTC().Epoch13();
    const std::string key = fmt::format("{}:{}", std::string(depth.market), std::string(depth.symbol));
    const auto iter = m_last_depth_ms.find(key);
    if (iter != m_last_depth_ms.end() && now - iter->second < m_cfg.quote_min_interval_ms) {
        return;
    }
    m_last_depth_ms[key] = now;

    JsonObj json;
    json.AddMember("type", std::string("snapshot"));
    json.AddMember("channel", std::string("quote"));
    json.AddMember("topic", fmt::format("depth:{}:{}", std::string(depth.market), std::string(depth.symbol)));
    json.AddMember("ts", now);
    JsonObj data = json.AddObject("data");
    data.AddMember("timestamp", depth.ex_time);
    JsonArray asks = data.AddArray("asks");
    for (int i = 0; i < depth.ask_cnt; ++i) {
        JsonArray pair = asks.PushBackArray();
        pair.PushBack(depth.ask_price[i]);
        pair.PushBack(depth.ask_amount[i]);
    }
    JsonArray bids = data.AddArray("bids");
    for (int i = 0; i < depth.bid_cnt; ++i) {
        JsonArray pair = bids.PushBackArray();
        pair.PushBack(depth.bid_price[i]);
        pair.PushBack(depth.bid_amount[i]);
    }
    // 兜底清理：限频账本随标的数增长，超过阈值时清掉久未更新的条目
    if (m_last_depth_ms.size() > 512) {
        for (auto it = m_last_depth_ms.begin(); it != m_last_depth_ms.end();) {
            it = (now - it->second > 60000) ? m_last_depth_ms.erase(it) : std::next(it);
        }
    }
    SendRaw(static_cast<std::string>(json));
}

void EventPublisher::PublishStrategyInfo(const StrategyInfo& info) {
    if (!Ready(PushChannel::Trade)) {
        return;
    }
    // param/indicator 是"JSON 字符串"（DB 列），这里按字符串透传，由 web_server 解析成对象后再给前端，
    // 保证与既有 REST/SSE 的 strategies 行同形（前端不需要区分两条来源）。
    JsonObj json;
    json.AddMember("type", std::string("snapshot"));
    json.AddMember("channel", std::string("trade"));
    json.AddMember("topic", std::string("strategy"));
    json.AddMember("ts", MyUTC().Epoch13());
    json.AddMember("key", std::string(info.id));
    JsonObj data = json.AddObject("data");
    data.AddMember("id", std::string(info.id));
    data.AddMember("strat_name", std::string(info.strat_name));
    data.AddMember("strat_template", std::string(info.strat_template));
    data.AddMember("status", static_cast<int>(info.status));
    data.AddMember("param", std::string(info.param));
    data.AddMember("indicator", std::string(info.indicator));
    data.AddMember("create_time", std::to_string(info.create_time));
    data.AddMember("update_time", std::to_string(info.update_time));
    SendRaw(static_cast<std::string>(json));
}

// ── 对端帧处理（io 线程）────────────────────────────────────────────────────
//
// 契约（rev4 §3）：hello_ack / subscribe / ping / query / closing
void EventPublisher::OnPeerFrame(const std::string& payload) {
    sonic_json::Document doc {};
    if (doc.Parse(payload.c_str()).HasParseError() || !doc.IsObject()) {
        SPDLOG_WARN("[EventPublisher] non-object frame from peer: {}", payload.substr(0, 200));
        return;
    }
    if (!doc.HasMember("type")) {
        return;
    }
    const std::string type = doc["type"].GetString();

    if (type == "hello_ack") {
        SPDLOG_INFO("[EventPublisher] handshake ok: {}", payload);
        return;
    }
    if (type == "subscribe" || type == "unsubscribe") {
        HandleSubscribe(payload);
        return;
    }
    if (type == "ping") {
        // 对端心跳：回带自己的收帧计数，供对端做同样的背压估算
        if (m_client) {
            if (doc.HasMember("rx")) {
                m_client->NotePeerRx(NodeToInt64(doc["rx"]));
            }
            SendRaw(fmt::format(R"({{"type":"pong","ts":{},"rx":{}}})", MyUTC().Epoch13(),
                                m_client->GetStats().recv));
        }
        return;
    }
    if (type == "query") {
        HandleQuery(payload);
        return;
    }
    if (type == "closing") {
        SPDLOG_WARN("[EventPublisher] peer closed the channel: {}", payload);
        return;
    }
    SPDLOG_DEBUG("[EventPublisher] ignore frame type={}", type);
}

void EventPublisher::HandleSubscribe(const std::string& payload) {
    sonic_json::Document doc {};
    doc.Parse(payload.c_str());
    uint32_t mask = 0;
    std::vector<std::string> topics {};
    if (doc.HasMember("topics") && doc["topics"].IsArray()) {
        const auto& arr = doc["topics"];
        for (size_t i = 0; i < arr.Size(); ++i) {
            const std::string name = arr[i].GetString();
            topics.push_back(name);
            if (name == "trade") {
                mask |= static_cast<uint32_t>(PushChannel::Trade);
            } else if (name == "quote") {
                mask |= static_cast<uint32_t>(PushChannel::Quote);
            } else {
                SPDLOG_WARN("[EventPublisher] unknown topic={}", name);
            }
        }
    }
    m_sub_mask.store(mask, std::memory_order_relaxed);

    JsonObj ack;
    ack.AddMember("type", std::string("ack"));
    ack.AddMember("ts", MyUTC().Epoch13());
    ack.AddMember("ok", true);
    JsonArray arr = ack.AddArray("topics");
    for (const auto& t : topics) {
        arr.PushBack(t);
    }
    // 说明：watermark（当前最大 entno/tdno）本版未回带——rev4 §3 明确它是可选项，
    // 正确性由客户端实体守卫（status_id/tdno 单调）兜底，不依赖水位。
    SendRaw(static_cast<std::string>(ack));
    SPDLOG_INFO("[EventPublisher] peer subscribed, topics={}, mask={}", topics.size(), mask);
}

void EventPublisher::HandleQuery(const std::string& payload) {
    sonic_json::Document doc {};
    doc.Parse(payload.c_str());
    const int64_t req_id = doc.HasMember("req_id") ? NodeToInt64(doc["req_id"]) : 0;
    const std::string what = doc.HasMember("what") ? doc["what"].GetString() : "";

    const int limit = doc.HasMember("limit") ? static_cast<int>(NodeToInt64(doc["limit"])) : 0;

    if (!m_query_handler) {
        SendRaw(fmt::format(R"({{"type":"result","req_id":{},"ok":false,"error":"no_query_handler"}})", req_id));
        return;
    }

    if (what == "orders") {
        HttpQueryOrdersReq req {};
        req.limit = limit;
        if (doc.HasMember("cursor")) {
            req.cursor_entno = NodeToInt64(doc["cursor"]);
        } else if (doc.HasMember("entnos") && doc["entnos"].IsArray()) {
            const auto& arr = doc["entnos"];
            for (size_t i = 0; i < arr.Size() && req.entno_cnt < kQueryMaxEntnos; ++i) {
                req.entnos[req.entno_cnt++] = NodeToInt64(arr[i]);
            }
        }
        const BufPtr rsp = m_query_handler(kHttpQueryOrdersReq, std::make_shared<TBuffer>(req));
        const int eff_limit = (limit > 0) ? std::min(limit, kQueryMaxRows) : kQueryDefaultRows;

        JsonObj json;
        json.AddMember("type", std::string("result"));
        json.AddMember("req_id", req_id);
        json.AddMember("what", std::string("orders"));
        json.AddMember("ok", true);
        json.AddMember("ts", MyUTC().Epoch13());
        JsonArray data = json.AddArray("data");
        int count = 0;
        if (rsp) {
            count = rsp->ForEach<Order>([&data](const Order& order) {
                JsonObj obj = data.PushBackObject();
                zrt::ToJson(obj, order);
            });
        }
        json.AddMember("count", count);
        json.AddMember("has_more", req.entno_cnt == 0 && count >= eff_limit);
        SendRaw(static_cast<std::string>(json));
        SPDLOG_INFO("[EventPublisher] query orders req_id={} cursor={} batch={} → {} rows",
                    req_id, req.cursor_entno, req.entno_cnt, count);
        return;
    }

    if (what == "trades") {
        HttpQueryTradesReq req {};
        req.limit = limit;
        if (doc.HasMember("cursor")) {
            req.cursor_tdno = NodeToInt64(doc["cursor"]);
        }
        const BufPtr rsp = m_query_handler(kHttpQueryTradesReq, std::make_shared<TBuffer>(req));
        const int eff_limit = (limit > 0) ? std::min(limit, kQueryMaxRows) : kQueryDefaultRows;

        JsonObj json;
        json.AddMember("type", std::string("result"));
        json.AddMember("req_id", req_id);
        json.AddMember("what", std::string("trades"));
        json.AddMember("ok", true);
        json.AddMember("ts", MyUTC().Epoch13());
        JsonArray data = json.AddArray("data");
        int count = 0;
        if (rsp) {
            count = rsp->ForEach<Trade>([&data](const Trade& trade) {
                JsonObj obj = data.PushBackObject();
                zrt::ToJson(obj, trade);
            });
        }
        json.AddMember("count", count);
        json.AddMember("has_more", count >= eff_limit);
        SendRaw(static_cast<std::string>(json));
        SPDLOG_INFO("[EventPublisher] query trades req_id={} cursor={} → {} rows", req_id, req.cursor_tdno, count);
        return;
    }

    SPDLOG_WARN("[EventPublisher] unsupported query what={}", what);
    SendRaw(fmt::format(R"({{"type":"result","req_id":{},"ok":false,"error":"unsupported_what"}})", req_id));
}
