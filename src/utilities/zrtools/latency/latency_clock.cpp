// =============================================================================
// latency_clock.cpp —— 自适应时钟实现
// =============================================================================
#ifdef __linux__

#include "zrtools/latency/latency_clock.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <time.h>
#include <unistd.h>

namespace zrt {

// ---- 静态成员定义（Init 写一次，Now 多线程只读）----
LatencyClock::Backend LatencyClock::s_backend   = LatencyClock::Backend::kUninitialized;
double                LatencyClock::s_ns_per_cycle = 0.0;
bool                  LatencyClock::s_inited      = false;

// ---------------------------------------------------------------------------
// 读取 TSC 周期计数的底层指令封装
//   - 优先 rdtscp：序列化指令，保证其前的 load/store 完成后再读 TSC，避免乱序
//     导致的时间戳偏移（对延时测量精度很关键）。
//   - rdtscp 不可用时回退 lfence + rdtsc + lfence（近似序列化）。
// 返回 64 位周期计数值。
// ---------------------------------------------------------------------------
static inline uint64_t ReadTsc() noexcept {
#if defined(__x86_64__)
    uint32_t lo {0}, hi {0}, aux {0};
    // rdtscp 序列化读 TSC；aux 占位（处理器号），延时测量不需要
    __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux) :: "memory");
    return (static_cast<uint64_t>(hi) << 32) | lo;
#else
    // 非 x86_64 平台没有 TSC，返回 0（上层 ProbeTscAvailable 会判否并回退）
    return 0;
#endif
}

// ---------------------------------------------------------------------------
// clock_gettime(CLOCK_MONOTONIC) 取纳秒，作为标定基准与回退实现
// ---------------------------------------------------------------------------
static inline int64_t MonoNowNs() noexcept {
    timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}

// ---------------------------------------------------------------------------
// 探测 CPU 是否支持 invariant TSC
//   读 /proc/cpuinfo 的 flags 行，检查三个关键标志：
//     constant_tsc : 频率不随 P-state（降频）变化
//     nonstop_tsc  : 深度休眠（C-state）时仍计数
//     tsc_reliable : 内核标记 TSC 可靠（最强信号，多路也会标记）
//   三者齐全才认为可跨线程安全使用 rdtsc。
// ---------------------------------------------------------------------------
bool LatencyClock::ProbeTscAvailable() {
    std::ifstream ifs {"/proc/cpuinfo"};
    if (!ifs.is_open()) {
        return false;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        // 只看 flags 行，避免误匹配其它字段
        if (line.rfind("flags", 0) == std::string::npos) {
            continue;
        }
        const bool has_const   = line.find("constant_tsc") != std::string::npos;
        const bool has_nonstop = line.find("nonstop_tsc")  != std::string::npos;
        const bool has_reliable= line.find("tsc_reliable") != std::string::npos;
        // 取首个 CPU 的 flags 行即可（同型号 CPU 标志一致）
        return has_const && has_nonstop && has_reliable;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 标定 ns_per_cycle
//   思路：对每段短时间区间，同时用 rdtsc 周期增量与 clock_gettime 纳秒增量
//   度量，比值即为 ns_per_cycle。多次采样后取中位数，消除调度抖动。
//   busy_spin：忙等一段时钟，保证 rdtsc 增量足够大（减小量化误差）。
// ---------------------------------------------------------------------------
double LatencyClock::CalibrateNsPerCycle(const int sample_count, const int64_t busy_spin_ns) {
    std::vector<double> ratios {};
    ratios.reserve(static_cast<size_t>(sample_count));
    // 前 5 轮预热（cache/调度抖动），稳定后再采
    for (int i = 0; i < sample_count + 5; ++i) {
        const int64_t  t0 = MonoNowNs();
        const uint64_t c0 = ReadTsc();

        // 忙等 busy_spin_ns 纳秒：以 clock_gettime 为参照消耗 CPU 周期
        int64_t cur = t0;
        while (cur - t0 < busy_spin_ns) {
            cur = MonoNowNs();
        }

        const int64_t  t1 = MonoNowNs();
        const uint64_t c1 = ReadTsc();

        if (i < 5) {
            continue;  // 预热样本丢弃
        }
        const double dt_ns = static_cast<double>(t1 - t0);
        const double dc    = static_cast<double>(c1 > c0 ? (c1 - c0) : 0);
        if (dc > 1.0 && dt_ns > 0.0) {
            ratios.push_back(dt_ns / dc);
        }
    }
    if (ratios.empty()) {
        return 0.0;
    }
    std::sort(ratios.begin(), ratios.end());
    // 中位数（对偶数取下中位数，足够稳定）
    return ratios[ratios.size() / 2];
}

// ---------------------------------------------------------------------------
// 初始化：决定后端 + 标定
// ---------------------------------------------------------------------------
void LatencyClock::Init(const Backend force_backend) {
    if (s_inited) {
        return;  // 幂等，避免重复标定
    }

    Backend chosen = force_backend;
    if (chosen == Backend::kUninitialized) {
        // 自动探测：TSC 可用且能标定出非零频率，才选 rdtsc
        if (ProbeTscAvailable()) {
            const double rate = CalibrateNsPerCycle();
            if (rate > 0.0) {
                s_ns_per_cycle = rate;
                chosen = Backend::kRdtsc;
            } else {
                chosen = Backend::kClockGettime;
            }
        } else {
            chosen = Backend::kClockGettime;
        }
    } else if (chosen == Backend::kRdtsc) {
        // 强制 rdtsc 也需要标定频率
        const double rate = CalibrateNsPerCycle();
        s_ns_per_cycle = (rate > 0.0) ? rate : 0.0;
        if (rate <= 0.0) {
            // 标定失败则安全回退
            chosen = Backend::kClockGettime;
        }
    }

    s_backend = chosen;
    s_inited  = true;
}

// ---------------------------------------------------------------------------
// Now()：热路径，无锁
//   - rdtsc 后端：读 TSC 周期数 × ns_per_cycle → ns
//   - 否则：直接 clock_gettime
//   - 未初始化：安全回退到 clock_gettime（永远可用）
// ---------------------------------------------------------------------------
int64_t LatencyClock::Now() noexcept {
    if (s_backend == Backend::kRdtsc && s_ns_per_cycle > 0.0) {
        const uint64_t cycles = ReadTsc();
        return static_cast<int64_t>(static_cast<double>(cycles) * s_ns_per_cycle);
    }
    return MonoNowNs();
}

// ---------------------------------------------------------------------------
// 诊断信息：后端类型 + 标定频率（折算 GHz）+ 是否已初始化
// ---------------------------------------------------------------------------
std::string LatencyClock::DiagnoseInfo() {
    const char* be_name = "uninitialized";
    switch (s_backend) {
        case Backend::kRdtsc:         be_name = "rdtsc"; break;
        case Backend::kClockGettime:  be_name = "clock_gettime"; break;
        case Backend::kUninitialized: be_name = "uninitialized(fallback clock_gettime)"; break;
    }
    const double ghz = (s_ns_per_cycle > 0.0) ? (1.0 / s_ns_per_cycle) : 0.0;
    std::string info {"LatencyClock backend="};
    info += be_name;
    info += " ns_per_cycle=" + std::to_string(s_ns_per_cycle);
    info += " (~" + std::to_string(ghz) + " GHz)";
    info += " inited=" + std::to_string(s_inited ? 1 : 0);
    return info;
}

} // namespace zrt

#endif // __linux__
