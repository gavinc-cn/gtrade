//
// Created by dell on 2025/2/19.
//

#include <string>
#include <cryptopp/hmac.h>
#include <cryptopp/sha.h>
#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include "okx_trade.h"
#include "dict_mapping.h"
#include "tbuffer.h"
#include "i_strategy_engine_dump.h"
#include "type_define.h"
#include "sonic_helper.h"
#include "err_code.h"
#include "strategy_engine.h"
#include "OkexClient.h"
#include "i_client_dump.h"
#include "zrtools/latency/latency_tracer.h"  // 延时测试打点宏（未启用时编译为空）

OkxTrade::OkxTrade(const GTradeConfig& gtrade_cfg, const std::string& account_id, StrategyEngine& strat_engine):
WebSocketBase(GetWsPrivateUrl(gtrade_cfg, account_id), gtrade_cfg.proxy_config.http),
m_strategy_engine(strat_engine),
m_gtrade_cfg(gtrade_cfg),
m_account_id(account_id),
m_market(gtrade_cfg.account_map.at(account_id).market)
{
    SetThread(zrt::EnginePool::GetInstance().GetSharedThread());
}

bool OkxTrade::Init() {
    SPDLOG_INFO("");

    // 除了这里注册的函数, 其他函数都是在websocket的线程执行, 注意线程安全
    // ZRT_ADD_HANDLER(kStratSubscribeTrade, OkxTrade::OnSubscribe);
    ZRT_ADD_HANDLER(kPlaceOrder, OkxTrade::OnPlaceOrder);
    ZRT_ADD_HANDLER(kCancelOrder, OkxTrade::OnCancelOrder);

    // 初始化OkexClient作为成员变量
    m_okex_client = std::make_unique<OkexClient>(m_gtrade_cfg, m_gtrade_cfg.account_map[m_account_id]);
    SPDLOG_INFO("OkexClient initialized for account={}", m_account_id);

    return true;
}

bool OkxTrade::Start() {
    run();
    while (!IsOpen()) {
        SPDLOG_INFO("wait for OkxTrade ready");
        sleep(1);
    }
    return true;
}

void OkxTrade::Login() {
    SPDLOG_INFO("");
    /*
    {
     "op": "login",
     "args":
      [
         {
           "apiKey": "985d5b66-57ce-40fb-b714-afc0b9787083",
           "passphrase": "123456",
           "timestamp": "1538054050",
           "sign": "7L+zFQ+CEgGu5rzCj4+BdV2/uUHGqddA9pI6ztsRRPs="
          }
       ]
    }
     */
    rapidjson::Document doc {};
    doc.SetObject();
    doc.AddMember("op", "login", doc.GetAllocator());

    // 创建 "args" 数组
    rapidjson::Value args(rapidjson::kArrayType);
    // 创建 "args" 数组中的对象
    rapidjson::Value arg(rapidjson::kObjectType);
    // std::string ch;
    arg.AddMember("apiKey", rapidjson::StringRef(m_gtrade_cfg.account_map[m_account_id].key.c_str()), doc.GetAllocator());
    arg.AddMember("passphrase", rapidjson::StringRef(m_gtrade_cfg.account_map[m_account_id].passphrase.c_str()), doc.GetAllocator());
    std::string timestamp = std::to_string(zrt::get_epoch10());
    arg.AddMember("timestamp", rapidjson::StringRef(timestamp.c_str()), doc.GetAllocator());
    // arg.AddMember("sign", rapidjson::StringRef(GetSign(m_gtrade_cfg.account_map[m_account_id].secret, timestamp).c_str()), doc.GetAllocator());

    rapidjson::Value sign_val;
    std::string signature = GetSign(m_gtrade_cfg.account_map[m_account_id].secret, timestamp);
    sign_val.SetString(signature.c_str(), signature.size(), doc.GetAllocator());
    arg.AddMember("sign", sign_val, doc.GetAllocator());

    args.PushBack(arg, doc.GetAllocator());
    // 将数组添加到文档
    doc.AddMember("args", args, doc.GetAllocator());

    // 序列化为带缩进的 JSON 字符串
    rapidjson::StringBuffer buffer {};
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    // 登录报文脱敏打印：apiKey/passphrase 仅保留前 4 位、sign 仅保留长度，禁止明文凭据落盘
    const std::string& api_key = m_gtrade_cfg.account_map[m_account_id].key;
    const std::string& passphrase = m_gtrade_cfg.account_map[m_account_id].passphrase;
    auto mask_head = [](const std::string& s) { return s.size() > 4 ? s.substr(0, 4) + "****" : "****"; };
    SPDLOG_INFO("login report: apiKey={}, passphrase={}, timestamp={}, sign_len={}", mask_head(api_key),
                mask_head(passphrase), timestamp, signature.size());
    Send(buffer.GetString());
}

void OkxTrade::on_open_impl() {
    Login();
    // 通知策略引擎
    WebSocketOpenNotify ws_open_notify {};
    zrt::fill_field(ws_open_notify.account_id, m_account_id);
    m_strategy_engine.PostMsg(kWebSocketOpenNotify, std::make_shared<TBuffer>(ws_open_notify));
}

void OkxTrade::SubscribeOrder(const std::string& channel, const std::string& inst_type) {
    SPDLOG_INFO("");
    /*
    {
        "op": "subscribe",
        "args": [{
            "channel": "orders",
            "instType": "FUTURES",
        }]
    }
    */
    rapidjson::Document doc {};
    doc.SetObject();
    doc.AddMember("op", "subscribe", doc.GetAllocator());

    // 创建 "args" 数组
    rapidjson::Value args(rapidjson::kArrayType);
    // 创建 "args" 数组中的对象
    rapidjson::Value arg(rapidjson::kObjectType);
    // std::string ch;
    arg.AddMember("channel", rapidjson::StringRef(channel.c_str()), doc.GetAllocator());
    arg.AddMember("instType", rapidjson::StringRef(inst_type.c_str()), doc.GetAllocator());
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

void OkxTrade::SubscribeBalanceAndHold() {
    SPDLOG_INFO("");
    /*
    {
        "op": "subscribe",
        "args": [{
            "channel": "balance_and_position"
        }]
    }
    */
    JsonObj json {};
    auto args = json.AddArray("args");
    auto arg = args.PushBackObject();
    arg.AddMember("channel", "balance_and_position");
    json.AddMember("op", "subscribe");

    std::string send_msg = json;
    SPDLOG_INFO("{}", send_msg);
    Send(send_msg);
}

void OkxTrade::SubscribePositions() {
    SPDLOG_INFO("");
    /*
    {
        "op": "subscribe",
        "args": [{
            "channel": "positions",
            "instType": "ANY"
        }]
    }
    */
    JsonObj json {};
    auto args = json.AddArray("args");
    auto arg = args.PushBackObject();
    arg.AddMember("channel", "positions");
    arg.AddMember("instType", "ANY");
    json.AddMember("op", "subscribe");

    std::string send_msg = json;
    SPDLOG_INFO("{}", send_msg);
    Send(send_msg);
}

void OkxTrade::SubscribeTrade(const std::string& channel, const std::string& inst_id) {
    SPDLOG_INFO("");
    /*
    {
        "op": "subscribe",
        "args": [
            {
                "channel": "fills",
                "instId": "BTC-USDT-SWAP"
            }
        ]
    }
     */
    rapidjson::Document doc {};
    doc.SetObject();
    doc.AddMember("op", "subscribe", doc.GetAllocator());

    // 创建 "args" 数组
    rapidjson::Value args(rapidjson::kArrayType);
    // 创建 "args" 数组中的对象
    rapidjson::Value arg(rapidjson::kObjectType);
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

// void OkxTrade::OnSubscribe(int msg_id, const BufPtr buffer) {
//     SPDLOG_INFO("");
//     SubscribeOrder("orders", "ANY");
//     // SubscribeTrade("fills", trade_sub.symbol);
//     SubscribeBalanceAndHold();
// }

void OkxTrade::SubscribeAll() {
    SubscribeOrder("orders", "ANY");
    SubscribeBalanceAndHold();
    SubscribePositions();
}

// void OkxTrade::FillStruct(Entrust& dst, const EntrustReq& src) {
//     zrt::fill_field(dst.market, src.market);
//     zrt::fill_field(dst.account_id, src.account_id);
//     zrt::fill_field(dst.inst_id, src.inst_id);
//     zrt::fill_field(dst.policy_no, src.policy_no);
//     zrt::fill_field(dst.private_no, src.private_no);
//     zrt::fill_field(dst.entno, src.entno);
//     zrt::fill_field(dst.bs_side, src.bs_side);
//     zrt::fill_field(dst.pos_side, src.pos_side);
//     zrt::fill_field(dst.price_type, src.price_type);
//     zrt::fill_field(dst.price, src.price);
//     zrt::fill_field(dst.amount, src.amount);
//     zrt::fill_field(dst.expire_time, src.expire_time);
//     zrt::fill_field(dst.ent_time, src.ent_time);
// }

void OkxTrade::OnPlaceOrder(int msg_id, const BufPtr buffer) {
    const Order& recv_data = buffer->RefData<Order>();
#ifdef GTRADE_ENABLE_LATENCY_TEST
    LATENCY_SPAN_FROM("trade_recv", recv_data.quote_monotonic);  // 原 [TT] trade_in
#endif
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    sonic_json::Node::AllocatorType alloc {};

    sonic_json::Node arg(sonic_json::kObject);
    AddMember(arg, alloc, "instId", recv_data.inst_id);
    // instIdCode 为 OKX 必填参数，0/缺失会被拒（51000 Parameter instIdCode error）。
    // 正常路径（策略/HTTP 下单）应在下单前填充正确值，此处仅对异常值告警，不改变发送行为。
    if (recv_data.inst_id_code <= 0) {
        SPDLOG_ERROR("OnPlaceOrder: invalid inst_id_code={} entno={} inst={}",
                     recv_data.inst_id_code, recv_data.entno, recv_data.inst_id);
    }
    AddMember(arg, alloc, "instIdCode", recv_data.inst_id_code);
    AddMember(arg, alloc, "tdMode", DictTradeMode2Okx(recv_data.trade_mode));
    AddMember(arg, alloc, "clOrdId", recv_data.entno);
    AddMember(arg, alloc, "side", DictBsSide2Okx(recv_data.bs_side));
    AddMember(arg, alloc, "posSide", DictPosSide2Okx(recv_data.pos_side));
    AddMember(arg, alloc, "ordType", DictPriceType2Okx(recv_data.price_type));
    AddMember(arg, alloc, "sz", recv_data.amount);
    AddMember(arg, alloc, "px", recv_data.price);

    sonic_json::Node args(sonic_json::kArray);
    args.PushBack(std::move(arg), alloc);

    sonic_json::Node node(sonic_json::kObject);
    AddMember(node, alloc, "id", recv_data.entno);
    AddMember(node, alloc, "op", "order");
    // expTime 仅在设置了超时时间时携带：expire_time=0 表示 GTC（长期有效），
    // 原样序列化为 expTime:0 会被 OKX 拒绝（51000 Parameter expTime error）
    if (recv_data.expire_time > 0) {
        AddMember(node, alloc, "expTime", recv_data.expire_time / zrt::kMega);
    }
    node.AddMember("args", std::move(args), alloc);

    Order entrust = recv_data;
    // FillStruct(entrust, recv_data);
    zrt::fill_field(entrust.status, OrderStatus::_1);
    m_strategy_engine.PostMsg(kPlaceOrderRsp, std::make_shared<TBuffer>(entrust));

    std::string send_msg = node.Dump();
#ifdef GTRADE_ENABLE_LATENCY_TEST
    LATENCY_SPAN_FROM("before_send", recv_data.quote_monotonic);  // 原 [TT] before_send
#endif
    Send(send_msg);
#ifdef GTRADE_ENABLE_LATENCY_TEST
    LATENCY_SPAN_FROM("tick2order", recv_data.quote_monotonic);   // 原 [TT] tick2order ★端到端
    // 记录订单发出网卡的时刻，供 L9(ack往返)/L10(成交往返) 关联
    zrt::LatencyCorrelator::Instance().RecordSend(recv_data.entno, zrt::LatencyClock::Now());
#endif
    SPDLOG_INFO("sent to okx: {}", send_msg);
}

void OkxTrade::OnCancelOrder(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const WithdrawReq*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    sonic_json::Node::AllocatorType alloc {};

    sonic_json::Node arg(sonic_json::kObject);
    AddMember(arg, alloc, "instId", recv_data.instrument);
    AddMember(arg, alloc, "clOrdId", recv_data.entno);

    sonic_json::Node args(sonic_json::kArray);
    args.PushBack(std::move(arg), alloc);

    sonic_json::Node node(sonic_json::kObject);
    AddMember(node, alloc, "id", recv_data.entno);
    AddMember(node, alloc, "op", "cancel-order");
    node.AddMember("args", std::move(args), alloc);

    std::string send_msg = node.Dump();
    Send(send_msg);
    SPDLOG_INFO("{}", send_msg);
}

void OkxTrade::on_message(websocketpp::connection_hdl, client::message_ptr msg) {
    int64_t local_time = MyUTC().Epoch19();
    const std::string& payload = msg->get_payload();
    SetLastMsgRecvTime(local_time);
    if (payload == "pong") {
        SPDLOG_DEBUG("recv pong");
        return;
    }
    SPDLOG_TRACE("{}", payload);
    sonic_json::Document d {};
    if (d.Parse(payload.c_str()).HasParseError()) {
        SPDLOG_ERROR("parse json failed: {}", payload);
        return;
    }
    if (!d.IsObject()) {
        SPDLOG_ERROR("root is not object");
        return;
    }
    if (d.HasMember("op")) {
        auto& op = d["op"];
        if (op == "order") {
            return OnPlaceOrderRsp(d);
        }
        else if (op == "cancel-order") {
            return OnCancelOrderRsp(d);
        }
    }
    else if (d.HasMember("arg") && d.HasMember("data")) {
        auto& arg = d["arg"];
        if (arg.HasMember("channel")) {
            if (arg["channel"] == "orders") {
                return OnPlaceOrderConfirm(d["data"]);
            }
            else if (arg["channel"] == "balance_and_position") {
                return OnBalAndPos(d["data"]);
            }
            else if (arg["channel"] == "positions") {
                return OnPositions(d["data"]);
            }
        }
    }
    else if (d.HasMember("event")) {
        if (d["event"] == "login" && d["code"] == "0") {
            return OnLogin();
        }
        else if (d["event"] == "error") {
            SPDLOG_ERROR("{}", payload);
        }
    }
}

void OkxTrade::OnLogin() {
    SPDLOG_INFO("OKX Trade login success for account={}", m_account_id);

    m_received_from_ws.clear();
    SubscribeAll();
    // 登录成功后查询所有未关闭委托
    QueryAllOpenEntrusts();
}

void OkxTrade::OnPlaceOrderRsp(const sonic_json::Node& node) {
    Order entrust {};
    zrt::fill_field(entrust.market, GetMarket());
    try {
        auto& data_lst = node["data"];
        for (size_t i=0; i<data_lst.Size(); ++i) {
            auto& data = data_lst[i];
            zrt::fill_field(entrust.entno, data["clOrdId"].GetString());
            zrt::fill_field(entrust.ex_entno, data["ordId"].GetString());
            zrt::fill_field(entrust.confirm_time, data["ts"].GetString());
            if (data["sCode"] == "0") {
                zrt::fill_field(entrust.status, OrderStatus::_2);
            } else {
                zrt::fill_field(entrust.status, OrderStatus::_9);
                zrt::fill_field(entrust.err_code, ErrorCode::OkxOrderFailed);
                zrt::fill_field(entrust.err_msg, fmt::format("{}: {}", data["sCode"].GetString(), data["sMsg"].GetString()));
            }
            m_received_from_ws.emplace(entrust.entno);
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("parse json data failed, err={} data={}", e.what(), node.Dump());
        zrt::fill_field(entrust.status, OrderStatus::_9);
        entrust.err_code = ErrorCode::kJsonParseError;
        zrt::fill_field(entrust.err_msg, e.what());
    }
    m_strategy_engine.PostMsg(kPlaceOrderRsp, std::make_shared<TBuffer>(entrust));
}

void OkxTrade::OnCancelOrderRsp(const sonic_json::Node& node) const {
    WithdrawRsp withdraw_rsp {};
    try {
        auto& data_lst = node["data"];
        bool sent_flag = false;
        for (size_t i=0; i<data_lst.Size(); ++i) {
            auto& data = data_lst[i];
            zrt::fill_field(withdraw_rsp.entno, data["clOrdId"].GetString());
            zrt::fill_field(withdraw_rsp.ex_time, data["ts"].GetString());
            zrt::fill_field(withdraw_rsp.err_code, data["sCode"].GetString());
            zrt::fill_field(withdraw_rsp.err_msg, data["sMsg"].GetString());
            m_strategy_engine.PostMsg(kCancelOrderRsp, std::make_shared<TBuffer>(withdraw_rsp));
            sent_flag = true;
        }
        if (!sent_flag) {
            zrt::fill_field(withdraw_rsp.err_code, node["code"].GetString());
            zrt::fill_field(withdraw_rsp.err_msg, node["msg"].GetString());
            m_strategy_engine.PostMsg(kCancelOrderRsp, std::make_shared<TBuffer>(withdraw_rsp));
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("parse json data failed, err={} data={}", e.what(), node.Dump());
        withdraw_rsp.err_code = ErrorCode::kJsonParseError;
        zrt::fill_field(withdraw_rsp.err_msg, e.what());
        m_strategy_engine.PostMsg(kCancelOrderRsp, std::make_shared<TBuffer>(withdraw_rsp));
    }
}

void OkxTrade::OnPlaceOrderConfirm(const sonic_json::Node& data) {
    SPDLOG_INFO("");
    for (size_t i=0; i<data.Size(); ++i) {
        Order entrust {};
        auto& ex_order = data[i];
        try {
            // 填充委托 (使用账户的market字段, 可能是okx或okx_dummy)
            zrt::fill_field(entrust.market, GetMarket());
            zrt::fill_field(entrust.entno, ex_order["clOrdId"].GetString());
            zrt::fill_field(entrust.ex_entno, ex_order["ordId"].GetString());
            zrt::fill_field(entrust.filled, ex_order["accFillSz"].GetString());
            zrt::fill_field(entrust.status, DictStatusFromOkx(ex_order["state"].GetString()));
            zrt::fill_field(entrust.pos_side, DictPosSideFromOkx(ex_order["posSide"].GetString()));
            zrt::fill_field(entrust.filled_px, ex_order["avgPx"].GetString());
            zrt::fill_field(entrust.confirm_time, ex_order["cTime"].GetString());
            zrt::fill_field(entrust.update_time, ex_order["uTime"].GetString());
            zrt::fill_field(entrust.filled_time, ex_order["fillTime"].GetString());
            entrust.confirm_time *= zrt::kMega;
            entrust.update_time *= zrt::kMega;
            entrust.filled_time *= zrt::kMega;
            zrt::fill_field(entrust.trd_px, ex_order["fillPx"].GetString());
            zrt::fill_field(entrust.trd_qty, ex_order["fillSz"].GetString());
            zrt::fill_field(entrust.err_code, ex_order["code"].GetString());
            zrt::fill_field(entrust.err_msg, ex_order["msg"].GetString());

            if (ZRT_UNLIKELY(zrt::is_empty(entrust.entno))) {
                // 说明不是本系统下的单, 需要补充更多信息
                zrt::fill_field(entrust.inst_type, DictInstTypeFromOkx(ex_order["instType"].GetString()));
                zrt::fill_field(entrust.inst_id, ex_order["instId"].GetString());
                zrt::fill_field(entrust.bs_side, DictBsSideFromOkx(ex_order["side"].GetString()));
                zrt::fill_field(entrust.trade_mode, DictTradeModeFromOkx(ex_order["tdMode"].GetString()));
                zrt::fill_field(entrust.price_type, DictPriceTypeFromOkx(ex_order["ordType"].GetString()));
                zrt::fill_field(entrust.price, ex_order["px"].GetString());
                zrt::fill_field(entrust.amount, ex_order["sz"].GetString());

                // AccountIdCs account_id;  // 账户ID
                // PolicyNoCs policy_no;  // 策略编号
                // PrivateNoCs private_no;  // 私有号
                // CharCs oc_side;  // 开平方向
                // int64_t ent_time;  // 委托时间(纳秒)
                // int64_t expire_time;  // 委托超时时间(纳秒)
                // double remain;  // 剩余数量
                // CharCs source;  // 委托来源
                // int64_t drawno;  // 撤单号
                // double draw_amt;  // 撤单数量
                // int64_t withdraw_time;  // 撤单时间(纳秒)
                // int status_id;  // 订单状态id
                // int64_t status_gid;  // 订单状态全局id
            }

            m_received_from_ws.emplace(entrust.entno);

            // Trade trade {};
            // zrt::fill_field(trade.trade_no, OrderManager::CreateTradeId());
            // zrt::fill_field(trade.market, entrust.market);
            // zrt::fill_field(trade.account_id, entrust.account_id);
            // zrt::fill_field(trade.inst_id, entrust.inst_id);
            // zrt::fill_field(trade.policy_no, entrust.policy_no);
            // zrt::fill_field(trade.private_no, entrust.private_no);
            // zrt::fill_field(trade.entno, entrust.entno);
            // zrt::fill_field(trade.bs_side, entrust.bs_side);
            // zrt::fill_field(trade.pos_side, entrust.pos_side);
            // zrt::fill_field(trade.price_type, entrust.price_type);
            // zrt::fill_field(trade.ord_status_id, entrust.status_id);
            // zrt::fill_field(trade.margin_mode, entrust.trade_mode);
            // zrt::fill_field(trade.filled_time, entrust.filled_time);
            // zrt::fill_field(trade.done_px, ex_order["fillPx"].GetString());
            // zrt::fill_field(trade.done_amt, ex_order["fillSz"].GetString());
            // zrt::fill_field(trade.trade_val, trade.done_px * trade.done_amt);
            // m_strategy_engine.PostMsg(kDonePush, std::make_shared<TBuffer>(trade));

        } catch (const std::exception& e) {
            SPDLOG_ERROR("parse json data failed, err={} data={}", e.what(), ex_order.Dump());
            zrt::fill_field(entrust.status, OrderStatus::_9);
            zrt::fill_field(entrust.err_code, ErrorCode::kJsonParseError);
            zrt::fill_field(entrust.err_msg, e.what());
        }
        m_strategy_engine.PostMsg(kPlaceOrderConfirm, std::make_shared<TBuffer>(entrust));
    }
}

void OkxTrade::OnBalAndPos(const sonic_json::Node& data) const {
    SPDLOG_INFO("");
    try {
        for (size_t data_i=0; data_i<data.Size(); ++data_i) {
            auto& d = data[data_i];
            auto& bal_vec = data[data_i]["balData"];
            for (size_t bal_i=0; bal_i<bal_vec.Size(); ++bal_i) {
                auto& bal_data = bal_vec[bal_i];
                Balance balance {};
                zrt::fill_field(balance.ex_time, bal_data["uTime"].GetString());
                balance.ex_time *= zrt::kMega;  // ms → ns
                balance.local_time = MyUTC().Epoch19();
                // todo 这里的datetime字段应该是iso格式的字符串
                zrt::fill_field(balance.datetime, MyUTC(balance.ex_time, 19).GetYmdHMS());
                zrt::fill_field(balance.market, GetMarket());
                zrt::fill_field(balance.account_id, m_account_id);
                zrt::fill_field(balance.currency, bal_data["ccy"].GetString());
                // todo 检查这里的逻辑
                // balance_and_position 频道 balData 只提供 cashBal（总余额）
                // 可用余额和冻结余额需通过 account 频道获取，此处用 cashBal 填充 total 和 available
                zrt::fill_field(balance.total, bal_data["cashBal"].GetString());
                zrt::fill_field(balance.available, bal_data["cashBal"].GetString());
                balance.frozen = 0.0;
                m_strategy_engine.PostMsg(kBalancePush, std::make_shared<TBuffer>(balance));
                SPDLOG_INFO("push balance to engine");
            }
            auto& pos_vec = data[data_i]["posData"];
            for (size_t pos_i=0; pos_i<pos_vec.Size(); ++pos_i) {
                auto& pos_data = pos_vec[pos_i];
                Position hold {};
                zrt::fill_field(hold.ex_time, pos_data["uTime"].GetString());
                zrt::fill_field(hold.local_time, zrt::get_epoch13());
                zrt::fill_field(hold.market, GetMarket());
                zrt::fill_field(hold.account_id, m_account_id);
                zrt::fill_field(hold.instrument, pos_data["instId"].GetString());
                zrt::fill_field(hold.inst_type, DictInstTypeFromOkx(pos_data["instType"].GetString()));
                zrt::fill_field(hold.margin_mode, DictTradeModeFromOkx(pos_data["mgnMode"].GetString()));
                zrt::fill_field(hold.pos_side, DictPosSideFromOkx(pos_data["posSide"].GetString()));
                zrt::fill_field(hold.available, pos_data["pos"].GetString());
                zrt::fill_field(hold.avg_px, pos_data["avgPx"].GetString());
                // 标记数据来源为交易所
                zrt::fill_field(hold.pos_source, PosSource::Exchange);

                m_strategy_engine.PostMsg(kPositionPush, std::make_shared<TBuffer>(hold));
                SPDLOG_INFO("push hold to engine");
            }
        }
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("parse json data failed, err={} data={}", e.what(), data.Dump());
    }
}

void OkxTrade::OnPositions(const sonic_json::Node& data) const {
    SPDLOG_INFO("");
    try {
        for (size_t data_i=0; data_i<data.Size(); ++data_i) {
            const auto& pos_data = data[data_i];
            Position hold {};

            // === 识别字段：用于定位持仓 ===
            zrt::fill_field(hold.ex_time, pos_data["uTime"].GetString());
            hold.ex_time *= zrt::kMega;
            zrt::fill_field(hold.local_time, MyUTC().Epoch19());
            zrt::fill_field(hold.datetime, MyUTC(hold.ex_time, 19).GetYmdHMS());
            zrt::fill_field(hold.market, GetMarket());
            zrt::fill_field(hold.account_id, m_account_id);
            zrt::fill_field(hold.instrument, pos_data["instId"].GetString());
            zrt::fill_field(hold.inst_type, DictInstTypeFromOkx(pos_data["instType"].GetString()));
            zrt::fill_field(hold.margin_mode, DictTradeModeFromOkx(pos_data["mgnMode"].GetString()));
            zrt::fill_field(hold.pos_side, DictPosSideFromOkx(pos_data["posSide"].GetString()));

            // === 收益字段：positions 频道的核心数据 ===
            zrt::fill_field(hold.upl, pos_data["upl"].GetString());
            zrt::fill_field(hold.upl_ratio, pos_data["uplRatio"].GetString());
            if (pos_data.HasMember("notionalUsd")) {
                zrt::fill_field(hold.notional_usd, pos_data["notionalUsd"].GetString());
            }

            // === 同步字段：保持持仓数量和均价同步 ===
            zrt::fill_field(hold.available, pos_data["pos"].GetString());
            zrt::fill_field(hold.avg_px, pos_data["avgPx"].GetString());

            // 标记数据来源为交易所
            zrt::fill_field(hold.pos_source, PosSource::Exchange);

            m_strategy_engine.PostMsg(kPositionPush, std::make_shared<TBuffer>(hold));
            SPDLOG_INFO("push position profit to engine");
        }
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("parse positions data failed, err={} data={}", e.what(), data.Dump());
    }
}

std::string OkxTrade::GetSign(const std::string& secret, const std::string& timestamp) {
    std::string raw_data = fmt::format("{}GET/users/self/verify", timestamp);
    CryptoPP::HMAC<CryptoPP::SHA256> hmac(reinterpret_cast<const CryptoPP::byte*>(secret.data()), secret.size());
    std::string digest {};
    CryptoPP::StringSource ss1(raw_data, true, new CryptoPP::HashFilter(hmac, new CryptoPP::StringSink(digest)));
    std::string encoded {};
    CryptoPP::StringSource ss2(digest, true, new CryptoPP::Base64Encoder(new CryptoPP::StringSink(encoded), false));
    return encoded;
}

void OkxTrade::on_close_impl() {
    SPDLOG_INFO("OKX Trade WebSocket connection closed");

    // 发送 Slack 断开连接消息
    SendNotifyMsg("OKX Trade WebSocket 连接断开",
                  fmt::format("OKX Trade WebSocket - Account: {} - URI: {}", m_account_id, RefUri()));
}

void OkxTrade::on_reconnected_impl() {
    SPDLOG_INFO("OKX Trade WebSocket reconnected");

    // 注意: 不需要在此处查询委托，因为重连时会触发Login，OnLogin()中已经会查询

    // 发送 Slack 重连成功消息
    SendNotifyMsg("OKX Trade WebSocket 重连成功",
                  fmt::format("OKX Trade WebSocket - Account: {} - URI: {}", m_account_id, RefUri()));
}

void OkxTrade::QueryAllOpenEntrusts() {
    SPDLOG_INFO("Query all entrusts for account={}", m_account_id);

    if (!m_okex_client) {
        SPDLOG_ERROR("OkexClient not initialized");
        return;
    }

    try {
        // <ex_entno,Entrust>
        std::unordered_map<int64_t,Order> all_entrusts_map {};

        OpenEntrustsQryReq open_ent_req {};
        zrt::fill_field(open_ent_req.market, GetMarket());
        zrt::fill_field(open_ent_req.account_id, m_account_id);

        if (auto open_entrusts_buf = std::make_shared<TBuffer>();
            m_okex_client->QryOpenEntrusts(open_entrusts_buf, open_ent_req)) {
            SPDLOG_INFO("Query open entrusts success, count={}", open_entrusts_buf->GetSize() / sizeof(Order));
            open_entrusts_buf->ForEach<Order>([&all_entrusts_map](const Order& entrust) {
                all_entrusts_map[entrust.ex_entno] = entrust;
            });
        } else {
            SPDLOG_WARN("Query open entrusts failed for account={}", m_account_id);
        }

        HisEntrustsQryReq his_ent_req {};
        zrt::fill_field(his_ent_req.market, GetMarket());
        zrt::fill_field(his_ent_req.account_id, m_account_id);
        const int64_t now = MyUTC().Epoch19();
        const int64_t days_ago = now - m_gtrade_cfg.entrust_maintain_days * 24 * 3600 * zrt::kGiga;
        his_ent_req.start_time = days_ago;
        // req.end_time = now;

        auto his_entrusts_buf = std::make_shared<TBuffer>();
        if (m_okex_client->QryHisEntrusts(his_entrusts_buf, his_ent_req)) {
            his_entrusts_buf->ForEach<Order>([&all_entrusts_map](const Order& entrust) {
                all_entrusts_map[entrust.ex_entno] = entrust;
            });
            SPDLOG_INFO("Query historical entrusts success, count={}", (his_entrusts_buf->GetSize() / sizeof(Order)));
        }
        else {
            SPDLOG_WARN("Query historical entrusts failed for account={}", m_account_id);
        }

        SPDLOG_INFO("Total entrusts queried: {}", all_entrusts_map.size());
        auto buf_ptr = std::make_shared<TBuffer>();
        for (const auto& [_, entrust] : all_entrusts_map) {
            SPDLOG_DEBUG("Entrust: {}", zrt::to_str(entrust));
            if (m_received_from_ws.count(entrust.entno)) {
                SPDLOG_INFO("entrust({}) already received from websocket", entrust.entno);
                continue;
            }
            buf_ptr->Append(entrust);
        }
        m_strategy_engine.PostMsg(kOrderRecovery, buf_ptr);

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception when querying entrusts: {}", e.what());
    }
}

void OkxTrade::SendNotifyMsg(const std::string& subject, const std::string& content) {
    try {
        // 构造 Slack 消息请求
        NotifyMessageReq req {};
        zrt::fill_field(req.channel, "websocket");  // 使用 websocket 频道
        zrt::fill_field(req.subject, subject);
        zrt::fill_field(req.content, content);
        req.is_async = false;
        m_strategy_engine.PostMsg(MsgId::kNotifyMessage, std::make_shared<TBuffer>(req));
        SPDLOG_DEBUG("Slack notification sent: subject={} content={}", subject, content);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to send Slack notification: {}", e.what());
    }
}