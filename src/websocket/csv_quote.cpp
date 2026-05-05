//
// Created by dell on 2025/2/19.
//


#include <filesystem>
#include "csv_quote.h"

#include <global.h>

#include "tbuffer.h"
#include "i_strategy_engine_dump.h"
#include "my_utc.h"
#include "type_define_dump.h"
#include "zrtools/zrt_time-inl.h"
#include "strategy_engine.h"
#include "time_machine.h"
#include "msg_id_dump.h"

namespace stdfs = std::filesystem;

bool CsvQuote::Init() {
    SPDLOG_INFO("{}", __PRETTY_FUNCTION__ );

    m_timer_manager = m_pool.at(k_TimerManager).get();

    InstallDefaultHandler([](int msg_id, const BufPtr buffer) {
        SPDLOG_ERROR("msg_id={} buf_sz={}", GetEmName_MsgId(msg_id), buffer->GetSize());
    });
    ZRT_ADD_HANDLER(kStratSubscribeQuote, CsvQuote::OnSubscribeQuote);
    ZRT_ADD_HANDLER(kCsvQuoteTimerEvent, CsvQuote::OnTimerEvent);

    return true;
}

bool CsvQuote::Start() {
    SPDLOG_INFO("");
    // MyService::Start();

    // 现在有strategy_engine驱动
    // m_timer = std::make_unique<boost::asio::steady_timer>(*RefIoService(),std::chrono::milliseconds(1));
    // m_timer->async_wait(std::bind(&CsvQuote::AsyncWaitHandler, this, std::placeholders::_1));
    return true;
}

void CsvQuote::OnSubscribeQuote(int msg_id, const BufPtr buffer) {
    QuoteSub quote_sub = *reinterpret_cast<const QuoteSub*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(quote_sub));
    const stdfs::path channel_dir = stdfs::path(m_gtrade_cfg.csv_quote_base_dir) / quote_sub.channel;
    SPDLOG_INFO("csv data dir: {}", channel_dir.string());
    for (const auto& date_dir: stdfs::directory_iterator(channel_dir)) {
        const std::string date_str = date_dir.path().filename().string();
        const int64_t date = MyUTC(date_str, "%Y%m%d").GetYmd();
        const int64_t start_date = MyUTC(m_gtrade_cfg.start_date, BACKTEST_TIME_FORMAT).GetYmd();
        const int64_t end_date = MyUTC(m_gtrade_cfg.end_date, BACKTEST_TIME_FORMAT).GetYmd();
        if (date < start_date || date > end_date) continue;

        std::set<stdfs::path> file_path_set {};
        for (const auto& data_file: stdfs::directory_iterator(date_dir)) {
            SPDLOG_INFO("data_file={}", data_file.path().stem().string());
            if (data_file.path().stem().string() == fmt::format("{}.{}.{}", quote_sub.inst_id, quote_sub.market, date_str)) {
                file_path_set.emplace(data_file);
            }
        }
        SPDLOG_INFO("csv files to load: {}", zrt::to_str(file_path_set));
        for (const auto& d: file_path_set) {
            if (zrt::equal(quote_sub.channel, k_depth1)) {
                const auto key = std::make_tuple(quote_sub.market, quote_sub.inst_id);
                Depth1CsvInfo depth1_csv_info = m_depth1_reader_bmic.TryCopy<TagPrimeKey>(key);
                zrt::fill_field(depth1_csv_info.market, quote_sub.market);
                zrt::fill_field(depth1_csv_info.instrument, quote_sub.inst_id);
                // 同市场, 同标的的文件, 按照文件名的顺序排到一个list中
                depth1_csv_info.reader_lst.emplace_back(std::make_shared<Depth1CsvReader>(d.string()));
                m_depth1_reader_bmic.Update<TagPrimeKey>(key, depth1_csv_info);
            }
        }
    }
}

bool CsvQuote::SendDepth1ToEngine(Depth1CsvInfo& csv_info, const int64_t quote_ms_now) const {
    SPDLOG_DEBUG("file={} backtest={}",
                MyUTC(csv_info.data.ex_time, 19).ToFormat(),
                MyUTC(quote_ms_now, 19).ToFormat());
    // 时间超过了, 返回跳过文件, 等待下一次事件
    if (csv_info.data.ex_time > quote_ms_now) return false;
    // 说明还有文件没读到这个时间戳, 先读其他文件
    // if (csv_info.data.ex_time > GetMinFileTimestamp()) return false;
    // 时间没到, 不用真的发送, 但仍然要清理, 以便读下一个数据
    if (csv_info.data.ex_time >= m_start_date_ms) {
        m_strategy_engine->PostMsg(kDepth1, std::make_shared<TBuffer>(csv_info.data));
    }
    csv_info.has_data = false;
    csv_info.Reset();
    return true;
}

// int64_t CsvQuote::GetMinFileTimestamp() const {
//     int64_t min_ts = INT64_MAX;
//     // for (const auto& [market, market_v]: m_depth1_reader_map) {
//     //     for (const auto& [inst, inst_v]: market_v) {
//     //         min_ts = std::min(inst_v.timestamp, min_ts);
//     //     }
//     // }
//     return min_ts;
// }

bool CsvQuote::ProceedDepth1(int64_t quote_ms_now) {
    // 是否向前推进, 没有推进就是所有文件都到达最新回测时间了, 就会等待下一个定时事件, 反之就会接着处理把文件都读到最新回测时间
    bool is_proceed = false;
    if (m_depth1_reader_bmic.IsEmpty()) {
        SPDLOG_INFO("m_depth1_reader_bmic is empty");
        return is_proceed;
    }
    Depth1CsvInfo csv_info = *m_depth1_reader_bmic.Begin<Tag_timestamp>();
    do {
        if (csv_info.reader_lst.empty()) {
            SPDLOG_INFO("no more file to read for {} {}", csv_info.market, csv_info.instrument);
            break;
        }
        const std::shared_ptr<Depth1CsvReader>& reader = csv_info.reader_lst.front();
        Depth& data = csv_info.data;
        if (!csv_info.has_read_header) {
            reader->read_header(io::ignore_extra_column, "ex_time","local_time","ex_time_iso","market","symbol","bid1_px","bid1_vol","ask1_px","ask1_vol");
            csv_info.has_read_header = true;
        }
        if (csv_info.has_data) {
            if (!SendDepth1ToEngine(csv_info, quote_ms_now)) {
                // 文件中的交易所时间超过回测到的时间, 去读下一个文件
                break;
            }
            is_proceed = true;
        }
        std::string ex_time_iso {};
        char* market = &(data.market[0]);
        char* symbol = &(data.symbol[0]);
        if (reader->read_row(data.ex_time,data.local_time,ex_time_iso,market,symbol,data.bid_price[0],data.bid_amount[0],data.ask_price[0],data.ask_amount[0])) {
            zrt::fill_field(data.datetime, ex_time_iso);
            zrt::fill_field(data.market, market);
            zrt::fill_field(data.symbol, symbol);
            zrt::fill_field(data.ask_cnt, 1);
            zrt::fill_field(data.bid_cnt, 1);
            zrt::fill_field(data.seq_id, ++csv_info.seq_id);
            zrt::fill_field(data.monotonic, zrt::get_monotonic19());
            SPDLOG_TRACE("depth={}", zrt::to_str(data));
            csv_info.has_data = true;
            csv_info.timestamp = data.ex_time;
            is_proceed = true;
            // 刚读出来, 还没有按照时间戳排序, 所以应该算作有进展, 但是要等下一轮再处理
            break;

            // if (!SendDepth1ToEngine(csv_info, quote_ms_now)) {
            //     // 文件中的交易所时间超过回测到的时间, 去读下一个文件
            //     break;
            // }
            // is_proceed = true;
        }
        else {
            SPDLOG_INFO("file {} done, read next", csv_info.reader_lst.front()->get_truncated_file_name());
            csv_info.reader_lst.pop_front();
            csv_info.has_read_header = false;
        }
    } while (false);
    m_depth1_reader_bmic.Update<TagPrimeKey>(std::make_tuple(csv_info.market, csv_info.instrument), csv_info);
    return is_proceed;
}

bool CsvQuote::ProceedKLine(int64_t quote_ms_now) {
    // 是否向前推进, 没有推进就是所有文件都到达最新回测时间了, 就会等待下一个定时事件, 反之就会接着处理把文件都读到最新回测时间
    bool is_proceed = false;
    if (m_depth1_reader_bmic.IsEmpty()) {
        SPDLOG_INFO("m_depth1_reader_bmic is empty");
        return is_proceed;
    }
    Depth1CsvInfo csv_info = *m_depth1_reader_bmic.Begin<Tag_timestamp>();
    do {
        if (csv_info.reader_lst.empty()) {
            SPDLOG_INFO("no more file to read for {} {}", csv_info.market, csv_info.instrument);
            break;
        }
        const std::shared_ptr<Depth1CsvReader>& reader = csv_info.reader_lst.front();
        Depth& data = csv_info.data;
        if (!csv_info.has_read_header) {
            reader->read_header(io::ignore_extra_column, "ex_time","local_time","ex_time_iso","market","symbol","bid1_px","bid1_vol","ask1_px","ask1_vol");
            csv_info.has_read_header = true;
        }
        if (csv_info.has_data) {
            if (!SendDepth1ToEngine(csv_info, quote_ms_now)) {
                // 文件中的交易所时间超过回测到的时间, 去读下一个文件
                break;
            }
            is_proceed = true;
        }
        std::string ex_time_iso {};
        char* market = &(data.market[0]);
        char* symbol = &(data.symbol[0]);
        if (reader->read_row(data.ex_time,data.local_time,ex_time_iso,market,symbol,data.bid_price[0],data.bid_amount[0],data.ask_price[0],data.ask_amount[0])) {
            zrt::fill_field(data.datetime, ex_time_iso);
            zrt::fill_field(data.market, market);
            zrt::fill_field(data.symbol, symbol);
            zrt::fill_field(data.ask_cnt, 1);
            zrt::fill_field(data.bid_cnt, 1);
            zrt::fill_field(data.seq_id, ++csv_info.seq_id);
            zrt::fill_field(data.monotonic, zrt::get_monotonic19());
            SPDLOG_TRACE("depth={}", zrt::to_str(data));
            csv_info.has_data = true;
            csv_info.timestamp = data.ex_time;
            is_proceed = true;
            // 刚读出来, 还没有按照时间戳排序, 所以应该算作有进展, 但是要等下一轮再处理
            break;

            // if (!SendDepth1ToEngine(csv_info, quote_ms_now)) {
            //     // 文件中的交易所时间超过回测到的时间, 去读下一个文件
            //     break;
            // }
            // is_proceed = true;
        }
        else {
            SPDLOG_INFO("file {} done, read next", csv_info.reader_lst.front()->get_truncated_file_name());
            csv_info.reader_lst.pop_front();
            csv_info.has_read_header = false;
        }
    } while (false);
    m_depth1_reader_bmic.Update<TagPrimeKey>(std::make_tuple(csv_info.market, csv_info.instrument), csv_info);
    return is_proceed;
}


void CsvQuote::AsyncWaitHandler(const boost::system::error_code &ec) {
    if (ec) {
        SPDLOG_ERROR("{}", ec.message());
        return;
    }
    const auto buf = std::make_shared<TBuffer>();
    buf->Append(MyUTC().Epoch19());
    PostMsg(kCsvQuoteTimerEvent, buf);
}

void CsvQuote::OnTimerEvent(int msg_id, const BufPtr buffer) {
    const int64_t real_ms_now = *reinterpret_cast<const int64_t*>(buffer->Data());
    SPDLOG_TRACE("real_time={}", MyUTC(real_ms_now, 19).ToFormat());
    const int64_t real_ms_elapsed = real_ms_now - m_start_epoch_ms;
    const int64_t quote_ms_elapsed = real_ms_elapsed * std::max(m_gtrade_cfg.backtest_rate, 1.0);
    const int64_t quote_ms_now = MyUTC(m_gtrade_cfg.start_date, BACKTEST_TIME_FORMAT).Epoch19() + quote_ms_elapsed;
    const int64_t adj_quote_ms = std::min(quote_ms_now, m_end_date_ms);
    TimeMachine::GetInstance().SetEpoch(adj_quote_ms * zrt::kMega);
    SPDLOG_INFO("bt_clock={}", MyUTC(adj_quote_ms, 19).ToFormat());

    const auto buf = std::make_shared<TBuffer>();
    buf->Append(MyUTC().Epoch19());
    m_timer_manager->PostMsg(kBacktestTimerEvent, buf);

    bool is_proceed = false;
    do {
        is_proceed = ProceedDepth1(adj_quote_ms);
    } while (is_proceed);
    if (quote_ms_now <= m_end_date_ms) {
        m_timer->expires_after(std::chrono::milliseconds(1));
        m_timer->async_wait(std::bind(&CsvQuote::AsyncWaitHandler, this, std::placeholders::_1));
    } else {
        SPDLOG_INFO("backtest end: {} >= {}",
            MyUTC(adj_quote_ms, 19).ToFormat(),
            MyUTC(m_end_date_ms, 19).ToFormat());
        GlobalControl::is_running = false;
    }
}

void CsvQuote::OnTimerUpdate(int msg_id, const BufPtr buffer) {
    const int64_t bt_clock_ns = *reinterpret_cast<const int64_t*>(buffer->Data());
    MyUTC bt_clock(bt_clock_ns, 19);
    SPDLOG_INFO("bt_clock={}", bt_clock.ToFormat());

    const auto buf = std::make_shared<TBuffer>();
    buf->Append(bt_clock.Epoch19());
    m_timer_manager->PostMsg(kBacktestTimerEvent, buf);

    bool is_proceed = false;
    do {
        is_proceed = ProceedDepth1(bt_clock.Epoch19());
    } while (is_proceed);
}

