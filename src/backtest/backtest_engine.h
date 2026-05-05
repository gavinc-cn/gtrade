//
// Created by gtrade on 2026/4/16.
//

#pragma once

#include "type_define.h"
#include "service_map.h"

/**
 * BacktestEngine: 封装回测模式下所有服务的创建与注册。
 *
 * 实盘模式使用 OkxWs + TimerManager，由 gtrade.cpp 直接创建；
 * 回测模式使用 CsvQuote + TimerManagerDummy，由本类统一注册。
 * 策略代码无需感知模式差异，通过 StrategyBase API 与系统交互。
 */
class BacktestEngine {
public:
    /**
     * 注册回测专用服务到 ServiceMap。
     * 必须在 pool.EngineStart() 之前调用。
     * @param service_map 全局服务映射
     * @param gtrade_cfg  全局配置
     */
    static void RegisterServices(ServiceMap& service_map, const GTradeConfig& gtrade_cfg);
};
