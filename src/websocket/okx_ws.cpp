//
// Created by dell on 2025/2/19.
//


#include "okx_ws.h"
#include "tbuffer.h"
#include "i_strategy_engine_dump.h"
#include "strategy_engine.h"
#include "zrtools/zrt_define.h"
#include "zrtools/zrt_fill.h"
#include "rapidjson_helper.h"
#include "i_client.h"
#include "msg_id.h"
#include "zrtools/latency/latency_tracer.h"  // 延时测试打点宏（未启用时编译为空）


bool OkxWs::Init() {
    SPDLOG_INFO("{}", __PRETTY_FUNCTION__);
    ZRT_ADD_HANDLER(kStratSubscribeQuote, OkxWs::OnSubscribeQuote);
    ZRT_ADD_HANDLER(kStratUnsubscribeQuote, OkxWs::OnUnsubscribeQuote);
    return true;
}

bool OkxWs::Start() {
    SPDLOG_INFO("{}", __PRETTY_FUNCTION__);
    run();
    while (!IsOpen()) {
        SPDLOG_INFO("wait for OkxWs ready");
        sleep(1);
    }
    return true;
}

void OkxWs::on_open_impl() {
    SPDLOG_INFO("{}", __PRETTY_FUNCTION__);
    for (const auto& p: m_sub_map) {
        if (zrt::equal(p.second.channel, k_depth1)) {
            Subscribe("bbo-tbt", p.second.inst_id);
        }
    }
}

// void OkxWs::subscribe() {
//     Subscribe("bbo-tbt", "BTC-USDT");
// }

void OkxWs::Subscribe(const std::string& channel, const std::string& inst_id) {
    SPDLOG_INFO("subscribe");
    // {
    //     "op": "subscribe",
    //     "args": [{
    //         "channel": "books",
    //         "instId": "BTC-USDT"
    //     }]
    // }
    rapidjson::Document doc {};
    doc.SetObject();
    doc.AddMember("op", "subscribe", doc.GetAllocator());

    // 创建 "args" 数组
    rapidjson::Value args(rapidjson::kArrayType);
    // 创建 "args" 数组中的对象
    rapidjson::Value arg(rapidjson::kObjectType);
    std::string ch;
    arg.AddMember("channel", rapidjson::StringRef(channel.c_str()), doc.GetAllocator());
    arg.AddMember("instId", rapidjson::StringRef(inst_id.c_str()), doc.GetAllocator());
    args.PushBack(arg, doc.GetAllocator());
    // 将数组添加到文档
    doc.AddMember("args", args, doc.GetAllocator());

    // 序列化为带缩进的 JSON 字符串
    rapidjson::StringBuffer buffer {};
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    SPDLOG_INFO("{}", buffer.GetString());
    Send(buffer.GetString());
}

// 订阅/退订：先把 QuoteSub 从 buffer 按值拷出，再投递到 io 线程执行，
// 与 on_open_impl 的遍历互斥（单线程访问 m_sub_map）
void OkxWs::OnSubscribeQuote(int msg_id, const BufPtr buffer) {
    const QuoteSub quote_sub = *reinterpret_cast<const QuoteSub*>(buffer->Data());
    PostToWsThread([this, quote_sub] {
        SPDLOG_INFO("{}", zrt::to_str(quote_sub));
        if (zrt::equal(quote_sub.channel, k_depth1)) {
            Subscribe("bbo-tbt", quote_sub.inst_id);
        }
        m_sub_map[GetPKey(quote_sub)] = quote_sub;
    });
}

void OkxWs::OnUnsubscribeQuote(int msg_id, const BufPtr buffer) {
    const QuoteSub quote_sub = *reinterpret_cast<const QuoteSub*>(buffer->Data());
    PostToWsThread([this, quote_sub] {
        SPDLOG_INFO("{}", zrt::to_str(quote_sub));
        if (zrt::equal(quote_sub.channel, k_depth1)) {
            Unsubscribe("bbo-tbt", quote_sub.inst_id);
        }
        m_sub_map.erase(GetPKey(quote_sub));
    });
}

// 报文格式：{"op":"unsubscribe","args":[{"channel":"bbo-tbt","instId":"..."}]}
void OkxWs::Unsubscribe(const std::string& channel, const std::string& inst_id) {
    rapidjson::Document doc {};
    doc.SetObject();
    doc.AddMember("op", "unsubscribe", doc.GetAllocator());
    rapidjson::Value args(rapidjson::kArrayType);
    rapidjson::Value arg(rapidjson::kObjectType);
    arg.AddMember("channel", rapidjson::StringRef(channel.c_str()), doc.GetAllocator());
    arg.AddMember("instId", rapidjson::StringRef(inst_id.c_str()), doc.GetAllocator());
    args.PushBack(arg, doc.GetAllocator());
    doc.AddMember("args", args, doc.GetAllocator());
    rapidjson::StringBuffer buffer {};
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    SPDLOG_INFO("{}", buffer.GetString());
    Send(buffer.GetString());
}


void OkxWs::on_message(websocketpp::connection_hdl, client::message_ptr msg) {
#ifdef GTRADE_ENABLE_LATENCY_TEST
    // 延时测试：行情入口改用统一时钟域（rdtsc/clock_gettime 自适应），
    // 与下游所有 [TT] 差值点保持同一时钟基准，避免跨域相减。
    int64_t monotonic = zrt::LatencyClock::Now();
#else
    int64_t monotonic = zrt::get_monotonic19();
#endif
    int64_t local_time = MyUTC().Epoch19();
    const std::string& payload = msg->get_payload();
    SPDLOG_TRACE("{}", payload);
    SetLastMsgRecvTime(local_time);
    if (payload == "pong") {
        SPDLOG_DEBUG("recv pong");
        return;
    }
    rapidjson::Document d {};
    rapidjson::Value &parsed_msg = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(payload.c_str());
    if (d.IsObject()) {
        if (parsed_msg.HasMember("data") && parsed_msg.HasMember("arg")) {
            auto& data = parsed_msg["data"];
            auto& arg = parsed_msg["arg"];
            if (arg.HasMember("channel")) {
                auto& channel = arg["channel"];
                if (zrt::equal(channel.GetString(), "bbo-tbt")) {
                    return OnBboTbt(local_time, monotonic, arg["instId"].GetString(), data);
                }
            }
        }
    }
    else {
        SPDLOG_ERROR("unexpected payload type={}", d.GetType());
    }
}

void OkxWs::OnBboTbt(const int64_t entry_time, const int64_t monotonic, const std::string& symbol, const rapidjson::Value& data) {
    if (ZRT_UNLIKELY(!data.IsArray() || data.Empty())) {
        SPDLOG_ERROR("Invalid data: not array or empty, {}", zrt::to_str(data));
        return;
    }

    const auto& item = data[0];
    if (ZRT_UNLIKELY(!item.IsObject())) {
        SPDLOG_ERROR("Invalid data[0]: not object, {}", zrt::to_str(data));
        return;
    }

    const auto asks_it = item.FindMember("asks");
    const auto bids_it = item.FindMember("bids");
    if (ZRT_UNLIKELY(asks_it == item.MemberEnd() || bids_it == item.MemberEnd())) {
        SPDLOG_ERROR("asks/bids not found, {}", zrt::to_str(data));
        return;
    }

    const auto& asks = asks_it->value;
    const auto& bids = bids_it->value;
    if (ZRT_UNLIKELY(!asks.IsArray() || !bids.IsArray())) {
        SPDLOG_ERROR("Invalid asks/bids, {}", zrt::to_str(data));
        return;
    }

    Depth depth {};
    zrt::fill_field(depth.ex_time, item["ts"]);
    depth.ex_time *= zrt::kMega;
    zrt::fill_field(depth.datetime, MyUTC(depth.ex_time, 19).ToFormat());
    zrt::fill_field(depth.seq_id, item["seqId"]);
    zrt::fill_field(depth.local_time, entry_time);
    zrt::fill_field(depth.monotonic, monotonic);
    zrt::fill_field(depth.ask_cnt, asks.Empty() ? 0 : 1);
    if (ZRT_LIKELY(depth.ask_cnt)) {
        const auto& ask1 = asks[0];
        if (ZRT_UNLIKELY(!ask1.IsArray() || ask1.Empty())) {
            SPDLOG_ERROR("invalid ask1: {}", zrt::to_str(data));
            return;
        }
        zrt::fill_field(depth.ask_price[0], ask1[0]);
        zrt::fill_field(depth.ask_amount[0], ask1[1]);
    }
    zrt::fill_field(depth.bid_cnt, bids.Empty() ? 0 : 1);
    if (ZRT_LIKELY(depth.bid_cnt)) {
        const auto& bid1 = bids[0];
        if (ZRT_UNLIKELY(!bid1.IsArray() || bid1.Empty())) {
            SPDLOG_ERROR("invalid bid1: {}", zrt::to_str(data));
            return;
        }
        zrt::fill_field(depth.bid_price[0], bid1[0]);
        zrt::fill_field(depth.bid_amount[0], bid1[1]);
    }
    zrt::fill_field(depth.symbol, symbol);
    zrt::fill_field(depth.market, m_exchange);
    m_strategy_engine->PostMsg(kDepth1, std::make_shared<TBuffer>(depth));
#ifdef GTRADE_ENABLE_LATENCY_TEST
    // 原 [TT] quote_out：行情解析完成耗时，统一改走直方图收集器（记录 ns 差值）
    LATENCY_SPAN_FROM("quote_parse", monotonic);
    // L1 行情网络下行：OKX 服务端时间戳(ms) → 本地接收时间(ns) 的差值。
    // ex_time 为毫秒 epoch，local_time 为纳秒 epoch，统一到 ns 作差。
    // 注意：含本地与 OKX 时钟偏差（clock skew），负值由直方图 clamp 到 0。
    if (depth.ex_time > 0) {
        LATENCY_RECORD("quote_net_inbound", depth.local_time - depth.ex_time * zrt::kMega);
    }
#endif
}

void OkxWs::on_close_impl() {
    SPDLOG_INFO("OKX WebSocket connection closed");

    // 发送 Slack 断开连接消息
    SendNotifyMsg("OKX WebSocket 连接断开",
                           "OKX WebSocket - URI: " + RefUri());
}

void OkxWs::on_reconnected_impl() {
    SPDLOG_INFO("OKX WebSocket reconnected");

    // 发送 Slack 重连成功消息
    SendNotifyMsg("OKX WebSocket 重连成功",
                           "OKX WebSocket - URI: " + RefUri());
}

void OkxWs::SendNotifyMsg(const std::string& subject, const std::string& content) {
    try {
        // 构造 Slack 消息请求
        NotifyMessageReq req {};
        zrt::fill_field(req.channel, "websocket");  // 使用 websocket 频道
        zrt::fill_field(req.subject, subject);
        zrt::fill_field(req.content, content);
        req.is_async = false;
        m_strategy_engine->PostMsg(MsgId::kNotifyMessage, std::make_shared<TBuffer>(req));
        SPDLOG_DEBUG("Slack notification sent: subject={} content={}", subject, content);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to send Slack notification: {}", e.what());
    }
}