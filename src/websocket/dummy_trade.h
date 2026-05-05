//
// Created by dell on 2025/2/15.
//

#pragma once

#include "pch.h"
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include "sonic_helper.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "websocket_base.h"
#include "i_exchange_data_dump.h"
#include "i_strategy_engine_dump.h"
#include "type_define.h"
#include "misc.h"
#include "str_utils.h"
#include "json_helper.h"
#include "zrtools/zrt_time.h"
#include "zrtools/zrt_file.h"
#include "dict.h"
#include "dummy_orderbook.h"

class StrategyEngine;

class DummyTrade final : public MyHandler {
public:
    DummyTrade(const GTradeConfig& gtrade_cfg, const std::string& account_id, StrategyEngine& strat_engine);
    ~DummyTrade() override = default;
    // 初始化
    bool Init() override;
    bool Start() override {return true;}
    DummyOrderbook& RefDummyOrderbook(const std::string& market, const std::string& instrument);
//    void on_open_impl() override;
    // 订阅
    void OnSubscribe(int msg_id, const BufPtr buffer);
    // 下单
    // static void FillStruct(Entrust& dst, const EntrustReq& src);
    void OnSendEntrust(int msg_id, const BufPtr buffer);
    // 撤单
    void OnWithDrawEntrust(int msg_id, const BufPtr buffer);
    void OnDepth1(int msg_id, const BufPtr buffer);
    void PushDone(const double match_px, const double trade_vol, const Order& entrust);
    void Entrust2Csv(const Order& x);
    void Done2Csv(const Trade& x);
private:
    MyHandler& m_strategy_engine;
    GTradeConfig m_gtrade_cfg {};
    std::string m_account_id {};
    static const std::unordered_map<char,std::string> m_price_type_map;
    static const std::unordered_map<std::string, char> m_status_map;
    static const std::unordered_map<char,std::string> m_bs_side_map;
    static const std::unordered_map<char,std::string> m_pos_side_map;
    static const std::unordered_map<char,std::string> m_trade_mode_map;

    // <market,<instrument,Depth>>
    std::unordered_map<std::string,std::unordered_map<std::string,Depth>> m_csv_depth_map {};
    // <market,<instrument,DummyOrderbook>>
    std::unordered_map<std::string,std::unordered_map<std::string,DummyOrderbook>> m_dummy_ob_map {};
    // std::string m_bt_ent_fpath = fmt::format("{}/{}_{}_{}.{}.csv",
    //         m_gtrade_cfg.backtest_out_dir, m_gtrade_cfg.start_date, m_gtrade_cfg.end_date, "entrust", zrt::DateTimeUTC().ToFormat());
    // std::string m_bt_done_fpath = fmt::format("{}/{}_{}_{}.{}.csv",
    //     m_gtrade_cfg.backtest_out_dir, m_gtrade_cfg.start_date, m_gtrade_cfg.end_date, "done", zrt::DateTimeUTC().ToFormat());
    // auto m_bt_ent_writer = std::make_shared<EntrustCsvWriter>(fmt::format("{}/{}_{}_{}.{}.csv",
    //     m_gtrade_cfg.backtest_out_dir,
    //     m_gtrade_cfg.start_date,
    //     m_gtrade_cfg.end_date,
    //     "entrust", zrt::DateTimeUTC().ToFormat()));
    // auto m_bt_done_writer = std::make_shared<DoneCsvWriter>(fmt::format("{}/{}_{}_{}.{}.csv",
    //     m_gtrade_cfg.backtest_out_dir,
    //     m_gtrade_cfg.start_date,
    //     m_gtrade_cfg.end_date,
    //     "done", zrt::DateTimeUTC().ToFormat()));
    zrt::FileWriter m_bt_ent_writer = zrt::FileWriter(
        fmt::format("{}/{}_{}_{}_{}.csv",
            m_gtrade_cfg.backtest_out_dir,
            m_gtrade_cfg.start_date,
            m_gtrade_cfg.end_date,
            // 这里是回测输出的文件名, 需要真实时间
            "entrust", zrt::DateTimeUTC().ToFormat(BACKTEST_TIME_FORMAT)),
            true);
    zrt::FileWriter m_bt_done_writer = zrt::FileWriter(
        fmt::format("{}/{}_{}_{}_{}.csv",
            m_gtrade_cfg.backtest_out_dir,
            m_gtrade_cfg.start_date,
            m_gtrade_cfg.end_date,
            "done", zrt::DateTimeUTC().ToFormat(BACKTEST_TIME_FORMAT)),
            true);
};

