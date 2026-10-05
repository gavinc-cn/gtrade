//
// Created by dell on 2025/2/15.
//

#pragma once

#include "pch.h"
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "websocket_base.h"
#include "i_exchange_data_dump.h"
#include "type_define.h"
#include "misc.h"
#include "str_utils.h"
#include "json_helper.h"
#include "my_utc.h"
#include "zrtools/zrt_time.h"
#include "zrtools/zrt_bmic_hashed.h"
#include "zrtools/zrt_bmic_ordered.h"
#include "fast-cpp-csv-parser/csv.h"
#include "service_map.h"

using Depth1CsvReader = io::CSVReader<9>;
using KLineCsvReader = io::CSVReader<9>;

struct Depth1CsvInfo {
    void Reset() {memset(&data, 0, sizeof(Depth));}

    std::string market {};
    std::string instrument {};
    Depth data {};
    std::list<std::shared_ptr<Depth1CsvReader>> reader_lst;
    bool has_read_header {}; // 是否已经读过文件头
    bool has_data {}; // data是否有效, 无效说明已发送给策略引擎
    int64_t seq_id {};
    int64_t timestamp {};
};

struct KLineCsvInfo {
    void Reset() {memset(&data, 0, sizeof(KLine));}

    std::string market {};
    std::string instrument {};
    std::string coefficient {};
    std::string scale {};
    KLine data {};
    std::list<std::shared_ptr<KLineCsvReader>> reader_lst;
    bool has_read_header {}; // 是否已经读过文件头
    bool has_data {}; // data是否有效, 无效说明已发送给策略引擎
    int64_t seq_id {};
    int64_t timestamp {};
};

class StrategyEngine;

class CsvQuote: public MyHandler {
public:
    CsvQuote(ServiceMap& pool, const GTradeConfig& gtrade_cfg):
    m_pool(pool),
    m_strategy_engine(pool.at(k_StrategyEngine).get()),
    m_gtrade_cfg(gtrade_cfg)
    {
        SetThread(zrt::EnginePool::GetInstance().GetNamedThread(k_BackTestThread));
    }
    ~CsvQuote() override = default;
    bool Init() override;
    bool Start() override;
    void OnSubscribeQuote(int msg_id, const BufPtr buffer);
    bool SendDepth1ToEngine(Depth1CsvInfo& csv_info, const int64_t quote_ms_now) const;
    // int64_t GetMinFileTimestamp() const;
    bool ProceedDepth1(int64_t quote_ms_now);
    bool ProceedKLine(int64_t quote_ms_now);
    void AsyncWaitHandler(const boost::system::error_code &ec);
    void OnTimerEvent(int msg_id, const BufPtr buffer);
    void OnTimerUpdate(int msg_id, const BufPtr buffer);
private:
    ServiceMap& m_pool;
    MyHandler* m_strategy_engine {};
    MyHandler* m_timer_manager {};
    const GTradeConfig m_gtrade_cfg {};
    // <market,<instrument,Depth1CsvInfo>>
    // std::unordered_map<std::string,std::unordered_map<std::string,Depth1CsvInfo>> m_depth1_reader_map {};
    zrt::BMIC<ZRT_BMIC(Depth1CsvInfo,
        ZRT_BMI_HASHED_UNIQUE_2_PARAM_INDEX(TagPrimeKey, Depth1CsvInfo, market, instrument),
        ZRT_BMI_ORDERED_NON_UNIQUE_INDEX(Depth1CsvInfo, timestamp)),
        Depth1CsvInfo> m_depth1_reader_bmic {};
    zrt::BMIC<ZRT_BMIC(KLineCsvInfo,
        ZRT_BMI_HASHED_UNIQUE_4_PARAM_INDEX(TagPrimeKey, KLineCsvInfo, market, instrument, coefficient, scale),
        ZRT_BMI_ORDERED_NON_UNIQUE_INDEX(KLineCsvInfo, timestamp)),
        Depth1CsvInfo> m_kline_reader_bmic {};
    std::unique_ptr<boost::asio::steady_timer> m_timer {};
    // 回测启动真实时间
    int64_t m_start_epoch_ms = MyUTC().Epoch19();
    // 回测设置起始时间
    int64_t m_start_date_ms = MyUTC(m_gtrade_cfg.start_date, BACKTEST_TIME_FORMAT).Epoch19();
    // 回测设置结束时间
    int64_t m_end_date_ms = MyUTC(m_gtrade_cfg.end_date, BACKTEST_TIME_FORMAT).Epoch19();
    // int64_t m_backtest_span_ms = zrt::DateTimeUTC(m_gtrade_cfg.end_date, BACKTEST_TIME_FORMAT).Epoch13() -
                                 // zrt::DateTimeUTC(m_gtrade_cfg.start_date, BACKTEST_TIME_FORMAT).Epoch13();
};
