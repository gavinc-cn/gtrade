#pragma once

// =============================================================================
// latency_tracer.h —— 延时打点器（热路径入口 + 编译期裁剪宏）
// =============================================================================
// 用法（仅在 -DGTRADE_ENABLE_LATENCY_TEST 下生效，否则全部编译为空、零开销）：
//
//   1) 记录一段已知耗时（delta_ns 是表达式）：
//        LATENCY_RECORD("strat_decision", t_end - t_begin);
//
//   2) 从某个起始时刻 t0 到"当前"的耗时：
//        LATENCY_SPAN_FROM("tick2order", quote_monotonic);
//      （内部用 LatencyClock::Now() 取当前时刻，再减 t0）
//
//   3) 取当前时刻（用于打"起始戳"）：
//        int64_t t0 = LATENCY_NOW();
//
//   4) 热路径极致优化（避免每样本 unordered_map 查找）：在文件/函数作用域缓存 id
//        static const int kId = zrt::LatencyCollector::Instance().RegisterMetric("x");
//        LATENCY_RECORD_ID(kId, delta_ns);
//
// 重要：所有"起始戳"与"当前时刻"必须取自同一时钟域（LatencyClock::Now()）。
//   现有 Depth.monotonic / OrderReq.quote_monotonic 原用 get_monotonic19()，
//   启用延时测试时应改为 LATENCY_NOW()（见 P0-6 改造说明）。
// =============================================================================

#ifdef __linux__

#include "zrtools/latency/latency_clock.h"
#include "zrtools/latency/latency_collector.h"

namespace zrt {

// 打点器门面：仅做静态转发，无状态
struct LatencyTracer {
    // 取当前单调时间戳（ns）。统一时钟域入口。
    static int64_t Now() noexcept { return LatencyClock::Now(); }

    // 按指标名记录一段耗时（delta_ns）。低频/集成路径用。
    static void Record(const char* name, const int64_t delta_ns) {
        LatencyCollector::Instance().RecordByName(name, delta_ns);
    }

    // 按 id 记录（热路径，无 map 查找）
    static void RecordId(const int metric_id, const int64_t delta_ns) noexcept {
        LatencyCollector::Instance().Record(metric_id, delta_ns);
    }
};

} // namespace zrt

// ---------------------------------------------------------------------------
// 宏：未启用 GTRADE_ENABLE_LATENCY_TEST 时全部内联为空，零开销
// ---------------------------------------------------------------------------
#ifdef GTRADE_ENABLE_LATENCY_TEST

// 取当前时刻（ns）
#define LATENCY_NOW() (zrt::LatencyClock::Now())

// 记录一段已知耗时（name 为字符串字面量；delta_ns 为表达式，只求值一次）
#define LATENCY_RECORD(name, delta_ns) \
    do { const int64_t _zrt_lat_d = (delta_ns); \
         zrt::LatencyCollector::Instance().RecordByName((name), _zrt_lat_d); } while (0)

// 从起始时刻 t0 到"当前"的耗时
#define LATENCY_SPAN_FROM(name, t0) \
    do { const int64_t _zrt_lat_t0 = (t0); \
         zrt::LatencyCollector::Instance().RecordByName((name), zrt::LatencyClock::Now() - _zrt_lat_t0); } while (0)

// 按已缓存 id 记录（热路径）
#define LATENCY_RECORD_ID(id, delta_ns) \
    do { const int64_t _zrt_lat_d = (delta_ns); \
         zrt::LatencyCollector::Instance().Record((id), _zrt_lat_d); } while (0)

#else // 未启用：全部编译为空

#define LATENCY_NOW()                    (0)
#define LATENCY_RECORD(name, delta_ns)   ((void)0)
#define LATENCY_SPAN_FROM(name, t0)      ((void)0)
#define LATENCY_RECORD_ID(id, delta_ns)  ((void)0)

#endif // GTRADE_ENABLE_LATENCY_TEST

#endif // __linux__
