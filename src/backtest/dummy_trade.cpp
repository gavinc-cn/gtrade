//
// Created by dell on 2025/2/19.
//

#include <string>
#include <cryptopp/hmac.h>
#include "dummy_trade.h"
#include "dict_mapping.h"
#include "tbuffer.h"
#include "i_strategy_engine_dump.h"
#include "type_define_dump.h"
#include "dummy_orderbook.h"
#include "zrtools/fmt_helper.h"
#include "msg_id_dump.h"

DummyTrade::DummyTrade(const GTradeConfig& gtrade_cfg, const std::string& account_id, MyHandler& strat_engine):
m_strategy_engine(strat_engine),
m_gtrade_cfg(gtrade_cfg),
m_account_id(account_id)
{
    SetThread(zrt::EnginePool::GetInstance().GetNamedThread(k_BackTestThread));
}

bool DummyTrade::Init() {
    SPDLOG_INFO("{}", __PRETTY_FUNCTION__ );

    InstallDefaultHandler([](int msg_id, const BufPtr buffer) {
        SPDLOG_ERROR("msg_id={} buf_sz={}", GetEmName_MsgId(msg_id), buffer->GetSize());
    });
    ZRT_ADD_HANDLER(kStratSubscribeTrade, DummyTrade::OnSubscribe);
    ZRT_ADD_HANDLER(kPlaceOrder, DummyTrade::OnSendEntrust);
    ZRT_ADD_HANDLER(kCancelOrder, DummyTrade::OnWithDrawEntrust);
    ZRT_ADD_HANDLER(kDepth1, DummyTrade::OnDepth1);

    m_bt_ent_writer.EnsureDir();
    m_bt_ent_writer.SetHeader("policy_no,private_no,entno,market,account_id,inst_id,bs_side,pos_side,price,amount,status,filled_px,filled,update_time,update_time_fmt");
    m_bt_ent_writer.Flush();
    SPDLOG_INFO("create {}", m_bt_ent_writer.GetFilePath());
    m_bt_done_writer.EnsureDir();
    m_bt_done_writer.SetHeader("policy_no,private_no,entno,market,account_id,inst_id,bs_side,pos_side,done_px,done_amt,filled_time,filled_time_fmt");
    m_bt_done_writer.Flush();
    SPDLOG_INFO("create {}", m_bt_done_writer.GetFilePath());
    return true;
}

//void DummyTrade::on_open_impl() {
//    // 通知策略引擎
//    WebSocketOpenNotify ws_open_notify {};
//    zrt::fill_field(ws_open_notify.account_id, m_account_id);
//    m_strategy_engine->PostMsg(kWebSocketOpenNotify, std::make_shared<TBuffer>(ws_open_notify));
//}

void DummyTrade::OnSubscribe(int msg_id, const BufPtr buffer) {
    TradeSub trade_sub = *reinterpret_cast<const TradeSub*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(trade_sub));
//    SubscribeOrder("orders", "ANY", trade_sub.inst_id);
    // SubscribeTrade("fills", trade_sub.symbol);
//    SubscribeBalanceAndHold();
}


DummyOrderbook& DummyTrade::RefDummyOrderbook(const std::string& market, const std::string& instrument) {
    const auto iter = m_dummy_ob_map[market].find(instrument);
    if (iter != m_dummy_ob_map[market].end()) {
        return iter->second;
    }
    return m_dummy_ob_map[market].emplace(instrument, DummyOrderbook(*this)).first->second;
}

// void DummyTrade::FillStruct(Entrust& dst, const EntrustReq& src) {
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

void DummyTrade::OnSendEntrust(int msg_id, const BufPtr buffer) {
    const Order& recv_data = buffer->RefData<Order>();
    SPDLOG_INFO("Entrust={}", zrt::to_str(recv_data));

    Order entrust = recv_data;
    // FillStruct(entrust, recv_data);
    zrt::fill_field(entrust.status, OrderStatus::_1);
    zrt::fill_field(entrust.ex_entno, entrust.entno);
    zrt::fill_field(entrust.update_time, MyUTC().Epoch19());
    m_strategy_engine.PostMsg(kPlaceOrderRsp, std::make_shared<TBuffer>(entrust));

    if (zrt::equal(m_gtrade_cfg.fill_mode, BackTestFillMode::Immediate)) {
        zrt::fill_field(entrust.status, OrderStatus::_4);
        zrt::fill_field(entrust.filled, entrust.amount);
        zrt::fill_field(entrust.filled_px, entrust.price);
        zrt::fill_field(entrust.filled_time, entrust.update_time);
        zrt::fill_field(entrust.confirm_time, entrust.update_time);
        m_strategy_engine.PostMsg(kPlaceOrderConfirm, std::make_shared<TBuffer>(entrust));
        Entrust2Csv(entrust);
        PushDone(entrust.filled_px, entrust.filled, entrust);
    }
    else if (zrt::equal(m_gtrade_cfg.fill_mode, BackTestFillMode::Simulate)) {
        DummyOrderbook& ob = RefDummyOrderbook(entrust.market, entrust.inst_id);
        ob.AddEntrust(entrust,
            [this](const Order& x){
            m_strategy_engine.PostMsg(kPlaceOrderConfirm, std::make_shared<TBuffer>(x));
            Entrust2Csv(x);
            // SPDLOG_INFO("dummy entrust confirm: {}", zrt::to_str(x));
        },
            [this](const Order& x){
            m_strategy_engine.PostMsg(kPlaceOrderRsp, std::make_shared<TBuffer>(x));
            // SPDLOG_INFO("dummy entrust rsp: {}", zrt::to_str(x));
        });
    }
    else {
        SPDLOG_ERROR("unexpect backtest fill mode: {}", m_gtrade_cfg.fill_mode);
    }
}

void DummyTrade::OnWithDrawEntrust(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const WithdrawReq*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    WithdrawRsp withdraw_rsp {};
    zrt::fill_field(withdraw_rsp.entno, recv_data.entno);
    zrt::fill_field(withdraw_rsp.ex_time, MyUTC().Epoch19());
    m_strategy_engine.PostMsg(kCancelOrderRsp, std::make_shared<TBuffer>(withdraw_rsp));

    DummyOrderbook& ob = RefDummyOrderbook(recv_data.market, recv_data.instrument);
    ob.DelEntrust(recv_data.entno,
        [this](const Order& x){
        m_strategy_engine.PostMsg(kPlaceOrderConfirm, std::make_shared<TBuffer>(x));
        Entrust2Csv(x);
        // SPDLOG_INFO("dummy withdraw confirm: {}", zrt::to_str(x));
    },
        [this](const WithdrawRsp& x){
        m_strategy_engine.PostMsg(kCancelOrderRsp, std::make_shared<TBuffer>(x));
        // SPDLOG_INFO("dummy withdraw rsp {}", zrt::to_str(x));
    });
}

void DummyTrade::OnDepth1(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const Depth*>(buffer->Data());
    DummyOrderbook& ob = RefDummyOrderbook(recv_data.market, recv_data.symbol);
    ob.AddDepth(recv_data, [this](const Order& x){
        TBufferPtr push_buf = std::make_shared<TBuffer>(x);
        m_strategy_engine.PostMsg(kPlaceOrderConfirm, push_buf);
        Entrust2Csv(x);
    });
}

void DummyTrade::PushDone(const double match_px, const double trade_vol, const Order& entrust) {
    Trade done {};
    zrt::fill_field(done.market, entrust.market);
    zrt::fill_field(done.account_id, entrust.account_id);
    zrt::fill_field(done.instrument, entrust.inst_id);
    zrt::fill_field(done.strat_id, entrust.policy_no);
    zrt::fill_field(done.private_no, entrust.private_no);
    zrt::fill_field(done.ordno, entrust.entno);
    zrt::fill_field(done.td_side, entrust.bs_side);
    zrt::fill_field(done.pos_side, entrust.pos_side);
    zrt::fill_field(done.px_type, entrust.price_type);
    zrt::fill_field(done.td_px, match_px);
    zrt::fill_field(done.td_qty, trade_vol);
    zrt::fill_field(done.filled_time, entrust.filled_time);
    m_strategy_engine.PostMsg(kTradePush, std::make_shared<TBuffer>(done));
    Done2Csv(done);
}


void DummyTrade::Entrust2Csv(const Order& x) {
    m_bt_ent_writer.WriteFmt(""
        "{},{},{},{},{},"
        "{},{},{},{},{},"
        "{},{},{},{},{}",
        x.policy_no,
        x.private_no,
        x.entno,
        x.market,
        x.account_id,
        x.inst_id,
        x.bs_side,
        zrt::safe_format_value(x.pos_side),
        x.price,
        x.amount,
        x.status,
        x.filled_px,
        x.filled,
        x.update_time, MyUTC(x.update_time).ToFormat());
    m_bt_ent_writer.Flush();
}

void DummyTrade::Done2Csv(const Trade& x) {
    m_bt_done_writer.WriteFmt(""
        "{},{},{},{},{},"
        "{},{},{},{},{},"
        "{},{}",
        x.strat_id,
        x.private_no,
        x.ordno,
        x.market,
        x.account_id,
        x.instrument,
        x.td_side,
        zrt::safe_format_value(x.pos_side),
        x.td_px,
        x.td_qty,
        x.filled_time, MyUTC(x.filled_time).ToFormat());
    m_bt_done_writer.Flush();
}