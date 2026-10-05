//
// K-Line Event Source for Backtesting
//

#pragma once

#include <vector>
#include <tuple>
#include <string>
#include "my_utc.h"
#include "mysql_client.h"
#include "service_map.h"
#include "zrtools/zrt_time.h"
#include "kline_helper.h"
#include "event_source_base.h"
#include "db_structures.h"

class KLineEventSource : public EventSourceBase {
public:
    using KeyType = std::tuple<std::string, std::string, int, char>;

    KLineEventSource(const GTradeConfig& gtrade_cfg, MyHandler& strategy_engine, const KeyType& key);
    ~KLineEventSource() override = default;

    bool Initialize() override;
    bool AdvanceToTime(int64_t target_ns) override;
    int64_t GetNextEventNs() const override;
    bool HasMoreData() const override;
protected:
    void PreloadData() override;
private:
    // 从数据库或CSV加载K线数据
    bool LoadKLineData(int64_t start_ns, int64_t end_ns);

    KeyType m_key {};
    std::string m_market {};
    std::string m_instrument {};
    int m_coefficient {};
    char m_scale {};

    // 缓存的K线数据，按时间排序
    // <ex_time,KLine>
    std::map<int64_t, KLine> m_cached_data;

    int64_t m_last_loaded_ns{}; // 最后加载数据的时间
    int64_t m_last_open_ns{}; // 最后推送K线开启事件的时间
    int64_t m_last_close_ns{}; // 最后推送K线关闭事件的时间
    KLine m_last_close_kline{}; // 最新一根已关闭但未发送的kline

    ServiceMap& m_pool = ServiceMap::GetInstance();
    MyHandler* m_qry_srv {};
    std::unique_ptr<MysqlClient> m_client = std::make_unique<MysqlClient>(
            m_gtrade_cfg.db_config);

    const int64_t m_start_ns = MyUTC(m_gtrade_cfg.start_date, BACKTEST_TIME_FORMAT).Epoch19();
    const int64_t m_end_ns = MyUTC(m_gtrade_cfg.end_date, BACKTEST_TIME_FORMAT).Epoch19();
    int64_t m_cur_ns = m_start_ns;
    // 预加载1000条K线
    const int64_t m_preload_ns = GetScaleNs(m_scale) * m_coefficient * 1000;
};
