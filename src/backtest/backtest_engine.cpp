//
// Created by gtrade on 2026/4/16.
//

#include "backtest_engine.h"
#include "csv_quote.h"
#include "timer_manager_dummy.h"
#include "string_keys.h"

/**
 * 向 ServiceMap 注册回测专用服务:
 *   - k_CsvQuote      → CsvQuote      (替代实盘的 OkxWs，从 CSV 文件读取历史行情)
 *   - k_TimerManager  → TimerManagerDummy (替代实盘的 TimerManager，接受虚拟时间推进)
 */
void BacktestEngine::RegisterServices(ServiceMap& service_map, const GTradeConfig& gtrade_cfg) {
    service_map.emplace(k_CsvQuote,
        std::make_unique<CsvQuote>(service_map, gtrade_cfg));
    service_map.emplace(k_TimerManager,
        std::make_unique<TimerManagerDummy>(service_map, gtrade_cfg));
}
