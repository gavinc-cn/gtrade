#pragma once

// =============================================================================
// latency_clock.h —— 自适应低开销单调时钟
// =============================================================================
// 设计目的：
//   核心交易链路的延时测量需要纳秒级、跨线程一致、且观测者效应尽可能小的
//   时钟源。本项目运行环境（WSL2/Docker on i7-12700 单路）经实测：
//     - 内核 current_clocksource = tsc
//     - CPU 具备 constant_tsc / nonstop_tsc / tsc_reliable / tsc_known_freq / rdtscp
//   ⇒ 直接用 rdtsc 跨线程比较是安全可靠的。
//
//   但生产服务器（Ubuntu24）TSC 状态未知，故本模块做成"自适应"：
//     1. Init() 时探测 CPU 标志位（/proc/cpuinfo）。
//     2. 满足条件则用 rdtsc（优先 rdtscp 序列化指令），并标定 ns_per_cycle。
//     3. 不满足则回退 clock_gettime(CLOCK_MONOTONIC)。
//   对外只暴露统一的 LatencyClock::Now()（返回 ns），上层无感。
//
// 线程安全：
//   - Init() 仅在启动单线程阶段调用一次。
//   - Now() 无状态、无锁，可被任意线程高频调用。
//   - s_ns_per_cycle 在 Init() 中（线程派生前）写入一次，其后只读，依赖
//     线程派生的 happens-before 关系保证可见性（标准且 x86 上安全）。
// =============================================================================

#ifdef __linux__

#include <cstdint>
#include <string>

namespace zrt {

class LatencyClock {
public:
    // 时钟后端类型
    enum class Backend : int {
        kUninitialized = 0,  // 未初始化（此时 Now() 回退到 clock_gettime）
        kRdtsc          = 1,  // 使用 TSC 周期计数（低开销）
        kClockGettime   = 2,  // 使用 clock_gettime（兼容回退）
    };

    // 初始化时钟：探测 CPU 标志 + 标定频率，必须在程序启动、工作线程派生前调用一次。
    // force_backend: 调试用，强制指定后端；传 kUninitialized 表示自动探测。
    static void Init(const Backend force_backend = Backend::kUninitialized);

    // 读取当前单调时间戳，单位纳秒（ns）。热路径调用，零锁。
    // 未调用 Init() 时自动回退 clock_gettime，保证永远可用。
    static int64_t Now() noexcept;

    // 返回当前后端类型（诊断用）
    static Backend GetBackend() noexcept { return s_backend; }

    // 返回标定的"每周期纳秒数"（诊断用；非 rdtsc 后端时为 0）
    static double GetNsPerCycle() noexcept { return s_ns_per_cycle; }

    // 返回可读的诊断信息字符串（用于日志/报告）
    static std::string DiagnoseInfo();

private:
    // 探测 CPU 是否支持 invariant/constant TSC（读 /proc/cpuinfo 标志位）
    // 返回 true 表示可安全使用 rdtsc 做跨线程延时测量
    static bool ProbeTscAvailable();

    // 用 rdtsc 增量与 clock_gettime 增量做多次采样，取中位数得到 ns_per_cycle。
    // sample_count: 采样次数；busy_spin_ns: 每次采样间忙等的纳秒数（保证 rdtsc 增量足够大）
    static double CalibrateNsPerCycle(const int sample_count = 31, const int64_t busy_spin_ns = 2 * 1000 * 1000);

    // ---- 运行期只读状态（Init 写一次，Now 读）----
    static Backend s_backend;          // 当前后端
    static double  s_ns_per_cycle;     // 每个 TSC 周期对应的纳秒数（仅 kRdtsc 有效）
    static bool    s_inited;           // 是否已 Init
};

} // namespace zrt

#endif // __linux__
