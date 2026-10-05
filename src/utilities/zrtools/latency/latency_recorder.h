#pragma once

// =============================================================================
// latency_recorder.h —— 延时测量门面：初始化/周期输出/汇总
// =============================================================================
// 职责：
//   1. Init()   —— 标定时钟 + 注册全部指标 + 打开 CSV 写表头
//   2. Start()  —— 启动周期 flush 线程，按窗口 Snapshot → 写 CSV 行 → Reset
//   3. Shutdown()—— 停止线程 + 写整运行汇总 JSON（含全运行百分位/直方图）
//
// 同时维护"累积统计"：每窗口 flush 时把样本并入累积分布，供退出汇总。
//
// 生命周期：程序启动（工作线程派生前）调 Init()+Start()；退出时调 Shutdown()。
//   全部功能仅在 -DGTRADE_ENABLE_LATENCY_TEST 下生效；否则为空实现。
// =============================================================================

#ifdef __linux__
#ifdef GTRADE_ENABLE_LATENCY_TEST

#include <atomic>
#include <fstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "zrtools/latency/latency_clock.h"
#include "zrtools/latency/latency_collector.h"

namespace zrt {

// 录制器配置
struct LatencyRecorderConfig {
    std::string           output_dir        = "./latency_out";   // CSV/JSON 输出目录（相对运行目录）
    std::string           run_name          = "gtrade";          // 输出文件名前缀
    int                   flush_interval_ms = 1000;              // 周期 flush 间隔（毫秒）
    LatencyClock::Backend force_clock       = LatencyClock::Backend::kUninitialized; // 时钟后端（默认自动探测）
};

class LatencyRecorder {
public:
    static LatencyRecorder& Instance();

    // 初始化：标定时钟、注册指标、打开 CSV。幂等。
    void Init(const LatencyRecorderConfig& cfg = {});

    // 启动周期 flush 线程。在 Init 之后调用。
    void Start();

    // 停止 flush 线程并输出整运行汇总 JSON。幂等。
    void Shutdown();

    // 手动触发一次 flush（调试用；不影响周期线程）
    void FlushOnce();

private:
    LatencyRecorder() = default;

    // 注册全部内置指标
    void RegisterBuiltinMetrics();

    // flush 线程主循环
    void FlushLoop();

    // 把一窗口快照并入累积分布
    void AccumulateCumulative(const std::vector<LatencySnapshot>& snaps);

    // 写 CSV 表头/一行
    void WriteCsvHeader();
    void WriteCsvRows(const std::vector<LatencySnapshot>& snaps, int64_t ts_ns);

    // 写整运行汇总 JSON
    void WriteSummaryJson();

    // 把纳秒 epoch 转成 ISO8601 UTC 字符串（CSV 时间列用）
    static std::string FormatIso8601Utc(int64_t epoch_ns);

    // ---- 累积分布（每指标）----
    struct CumulativeMetric {
        std::string           name;
        std::vector<uint64_t> bucket_counts;  // 与 kEdges 对齐 + 溢出桶
        uint64_t              count     {0};
        uint64_t              sum_ns    {0};
        uint64_t              sum_sq_ns {0};
        int64_t               min_ns    {INT64_MAX};
        int64_t               max_ns    {0};
    };

    LatencyRecorderConfig                             m_cfg;
    std::ofstream                                     m_csv;
    std::thread                                       m_flush_thread;
    std::atomic<bool>                                 m_running {false};
    std::atomic<bool>                                 m_inited  {false};
    std::unordered_map<std::string, CumulativeMetric> m_cumulative;
};

} // namespace zrt

#else // GTRADE_ENABLE_LATENCY_TEST 未定义：空实现

namespace zrt {
struct LatencyRecorderConfig {};
class LatencyRecorder {
public:
    static LatencyRecorder& Instance() { static LatencyRecorder inst{}; return inst; }
    void Init(const LatencyRecorderConfig& = {}) {}
    void Start() {}
    void Shutdown() {}
    void FlushOnce() {}
};
} // namespace zrt

#endif // GTRADE_ENABLE_LATENCY_TEST
#endif // __linux__
