//
// Depth Event Source for Backtesting
//

#pragma once

#include <vector>
#include <tuple>
#include <string>
#include "my_utc.h"
#include "mysql_client.h"
#include "service_map.h"
#include "zrtools/zrt_time.h"
#include "event_source_base.h"
#include "db_structures.h"

class DepthEventSource: public EventSourceBase {
public:
    // <market, instrument>
    using KeyType = std::tuple<std::string, std::string>;

    DepthEventSource(const GTradeConfig& gtrade_cfg, MyHandler& strategy_engine, const KeyType& key);
    ~DepthEventSource() override = default;

    bool Initialize() override;
    bool AdvanceToTime(int64_t target_ns) override;
    int64_t GetNextEventNs() const override;
    bool HasMoreData() const override;
protected:
    void PreloadData() override;
private:
    // 从数据库或CSV加载Depth数据
    bool LoadDepthData(int64_t start_ns, int64_t end_ns);

    KeyType m_key {};
    std::string m_market {};
    std::string m_instrument {};

    // 缓存的Depth数据，按时间排序
    // <timestamp,Depth>
    std::map<int64_t, Depth> m_cached_data;

    int64_t m_last_loaded_ns{}; // 最后加载数据的时间

    ServiceMap& m_pool = ServiceMap::GetInstance();
    MyHandler* m_qry_srv {};
    std::unique_ptr<MysqlClient> m_client = std::make_unique<MysqlClient>(m_gtrade_cfg.db_config);

    const int64_t m_start_ns = MyUTC(m_gtrade_cfg.start_date, BACKTEST_TIME_FORMAT).Epoch19();
    const int64_t m_end_ns = MyUTC(m_gtrade_cfg.end_date, BACKTEST_TIME_FORMAT).Epoch19();
    int64_t m_cur_ns = m_start_ns;
    // 预加载1000秒的Depth数据
    const int64_t m_preload_ns = 1000 * 1000 * zrt::kMega; // 1000秒的数据
};