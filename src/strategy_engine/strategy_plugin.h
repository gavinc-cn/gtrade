//
// 策略动态插件接口定义
//
// 每个编译为独立 .so 的策略插件必须导出以下两个 C 函数：
//   - gtrade_create_strategy : 工厂函数，创建策略实例
//   - gtrade_get_build_mode  : 返回构建模式（0=实盘，1=回测），引擎加载时校验
//
// 使用示例（在策略 .cpp 末尾追加）：
//
//   #include "strategy_engine/strategy_plugin.h"
//
//   extern "C" {
//
//   StrategyBase* gtrade_create_strategy(
//       const GTradeConfig& cfg,
//       MyHandler*          engine,
//       const std::string&  strat_id)
//   {
//       return new MyStrategy(cfg, engine, strat_id);
//   }
//
//   int gtrade_get_build_mode()
//   {
//       return GlobalConst::IsRealTrading ? 0 : 1;
//   }
//
//   } // extern "C"
//

#pragma once

#include "strategy_base.h"

extern "C" {

/**
 * 工厂函数：创建策略实例。
 *
 * 参数类型使用 MyHandler*（而非 StrategyEngine*），与 StrategyBase 构造函数保持一致。
 * 这样 StrategyEngine 和 BacktestStrategyEngine 都可以直接传 this（两者均为 MyHandler 子类）。
 *
 * 返回值为堆分配的 StrategyBase*，所有权转移给调用方（引擎以 shared_ptr 包装并管理生命周期）。
 */
StrategyBase* gtrade_create_strategy(
    const GTradeConfig& gtrade_cfg,
    MyHandler*          strat_engine,
    const std::string&  strat_id);

/**
 * 构建模式标识。
 * 返回 0 表示实盘（与 gtrade 配套），返回 1 表示回测（与 gtrade_bt 配套）。
 * 引擎加载时会校验此值，不匹配则拒绝加载，防止实盘/回测 .so 误装载。
 */
int gtrade_get_build_mode();

} // extern "C"

/// 工厂函数指针类型，用于 dlsym 结果的类型转换
using GTradeStrategyFactory =
    StrategyBase* (*)(const GTradeConfig&, MyHandler*, const std::string&);
