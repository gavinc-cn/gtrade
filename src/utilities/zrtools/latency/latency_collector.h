#pragma once

// =============================================================================
// latency_collector.h —— 延时样本存储与统计（多指标直方图）
// =============================================================================
// 设计要点（工业级低开销延时测量）：
//   - 每个指标维护一组"对数分桶直方图"，热路径 Record() 只做一次原子自增，
//     无锁、无内存分配，观测者效应极小。
//   - 百分位（p50/p90/p99/p999）通过遍历桶累积计数反解，长尾一目了然。
//   - mean/std 额外用原子累加 count/sum/sum_sq 实时维护，精度不依赖分桶。
//   - Snapshot 时复制计数后离线计算，不阻塞热路径。
//
// 线程安全：Record() 与 SnapshotAll() 均线程安全（全原子操作）。
// =============================================================================

#ifdef __linux__

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zrt {

// ---------------------------------------------------------------------------
// 直方图分桶上界（单位：100ns = 0.1µs）。桶 i 覆盖区间 (edges[i-1], edges[i]]，
// 超过最后一个边界的样本落入溢出桶。覆盖范围 0.2µs ~ 1000ms。
// 亚微秒级分桶（0.2/0.3/.../1.0µs）确保低延时场景 p50/p99 有区分度。
// 数组保持有序，供 std::lower_bound 二分定位桶号。
// ---------------------------------------------------------------------------
struct LatencyBucketEdges {
    // 分桶上界（100ns 单位：2=0.2µs, 5=0.5µs, 10=1µs, 100=10µs, ...）
    static constexpr int64_t kEdges[] = {
        // 亚微秒: 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0 µs
        2, 3, 4, 5, 6, 7, 8, 9, 10,
        // 1~10µs: 1.2, 1.5, 2, 2.5, 3, 4, 5, 7.5, 10
        12, 15, 20, 25, 30, 40, 50, 75, 100,
        // 10µs~1ms
        150, 200, 300, 500, 750, 1000,
        1500, 2000, 3000, 5000, 7500,
        // 1ms~1000ms
        10000, 15000, 20000, 30000, 50000, 100000, 200000, 500000, 1000000
    };
    static constexpr size_t kCount = sizeof(kEdges) / sizeof(kEdges[0]);
};

// ---------------------------------------------------------------------------
// 单指标的统计快照（Snapshot 时产出，单位统一微秒 us）
// ---------------------------------------------------------------------------
struct LatencySnapshot {
    std::string metric;       // 指标名
    uint64_t    count {0};    // 样本数
    double      mean_us {0.0};
    double      min_us {0.0};
    double      max_us {0.0};
    double      std_us {0.0}; // 样本标准差
    double      p50_us {0.0};
    double      p90_us {0.0};
    double      p99_us {0.0};
    double      p999_us{0.0};
    std::vector<uint64_t> bucket_counts {};  // 各桶样本数（与 kEdges 对应 + 溢出桶）
};

// ---------------------------------------------------------------------------
// LatencyHistogram —— 单指标的原子直方图存储
//   - Record(delta_ns)：定位桶 + 原子自增，O(log N) 二分，N≈37，约 5~6 次比较
//   - 热路径零锁、零分配
// ---------------------------------------------------------------------------
class LatencyHistogram {
public:
    LatencyHistogram() = default;

    // 记录一个延时样本（参数单位：纳秒 ns）
    void Record(const int64_t delta_ns) noexcept {
        // 纳秒 → 100ns 单位（向下取整；负差容错 clamp 到 0）
        int64_t hns = delta_ns / 100;
        if (hns < 0) {
            hns = 0;  // 容错：理论上单调时钟不会出现负差，防御性 clamp
        }
        const size_t idx = BucketIndexOf(hns);

        // 原子更新桶计数
        m_bucket_counts[idx].fetch_add(1, std::memory_order_relaxed);

        // 实时维护 count / sum / sum_sq / min / max
        m_count.fetch_add(1, std::memory_order_relaxed);
        // sum/sum_sq 累加用 relaxed（统计允许极小并发误差）
        m_sum_ns.fetch_add(static_cast<uint64_t>(delta_ns > 0 ? delta_ns : 0), std::memory_order_relaxed);
        m_sum_sq_ns.fetch_add(static_cast<uint64_t>(delta_ns > 0 ? delta_ns : 0) *
                              static_cast<uint64_t>(delta_ns > 0 ? delta_ns : 0),
                              std::memory_order_relaxed);
        AtomicMin(m_min_ns, delta_ns);
        AtomicMax(m_max_ns, delta_ns);
    }

    // 生成统计快照（复制原子计数后离线计算）
    LatencySnapshot Snapshot(const std::string& metric_name) const;

    // 清零全部计数（周期窗口统计时，flush 后调用以开始新窗口）
    void Reset() noexcept;

    // 由直方图桶计数反解指定分位的延时（桶内线性插值），返回 100ns 单位值。
    // 公开静态：供外部累积分布复用（如 recorder 的整运行汇总）。
    static double Percentile(const std::vector<uint64_t>& bucket_counts, uint64_t count, double q) noexcept;

private:
    // 由微秒值定位桶号：返回首个 >= us 的边界下标；超过最大边界返回溢出桶
    static size_t BucketIndexOf(const int64_t val) noexcept;

    // 原子 min/max 更新（CAS 循环）
    static void AtomicMin(std::atomic<int64_t>& slot, const int64_t v) noexcept;
    static void AtomicMax(std::atomic<int64_t>& slot, const int64_t v) noexcept;

    // ---- 原子存储 ----
    std::array<std::atomic<uint64_t>, LatencyBucketEdges::kCount + 1> m_bucket_counts {};  // +1 溢出桶
    std::atomic<uint64_t> m_count     {0};   // 样本总数
    std::atomic<uint64_t> m_sum_ns    {0};   // 延时总和（ns）
    std::atomic<uint64_t> m_sum_sq_ns {0};   // 延时平方和（ns²），用于 std
    std::atomic<int64_t>  m_min_ns    {INT64_MAX};
    std::atomic<int64_t>  m_max_ns    {0};
};

// ---------------------------------------------------------------------------
// LatencyCollector —— 全局多指标收集器（单例）
//   - RegisterMetric(name) → 返回稳定的 metric_id（供热路径快速 Record）
//   - Record(metric_id, delta_ns) → 转发到对应直方图
//   - SnapshotAll() → 拷贝所有指标快照（供周期 flush）
//   - Reset() → 清零全部计数（按窗口统计用）
// ---------------------------------------------------------------------------
class LatencyCollector {
public:
    static LatencyCollector& Instance();

    // 注册指标，返回 id。同名重复注册返回已注册的 id（幂等）。
    int RegisterMetric(const std::string_view name);

    // 热路径：按 id 记录样本。id 无效时静默丢弃（不影响交易）。
    void Record(const int metric_id, const int64_t delta_ns) noexcept {
        if (metric_id < 0 || metric_id >= static_cast<int>(m_metrics.size())) {
            return;
        }
        m_metrics[metric_id].histogram.Record(delta_ns);
    }

    // 便捷：按名称记录（注册期/低频路径用，内部查表）
    void RecordByName(const std::string_view name, const int64_t delta_ns);

    // 拷贝全部指标快照
    std::vector<LatencySnapshot> SnapshotAll() const;

    // 清零全部（周期窗口统计时，flush 后调用以开始新窗口）
    void Reset();

    // 指标总数（诊断）
    size_t MetricCount() const { return m_metrics.size(); }

private:
    LatencyCollector() = default;
    struct MetricEntry {
        std::string      name;
        LatencyHistogram histogram;
    };
    // 用 deque：LatencyHistogram 含 std::atomic 不可拷贝/移动，
    // deque 增长时分配新块、不搬迁既有元素，配合 emplace_back 原地构造可避开移动语义。
    std::deque<MetricEntry>  m_metrics;
    std::unordered_map<std::string, int> m_name_to_id;
};

// ---------------------------------------------------------------------------
// LatencyCorrelator —— 订单往返延时关联器（L9 ack往返 / L10 成交往返）
//   订单发出时按 entno 记录发送时刻；ack/成交回报到达时反查并作差，
//   得到 "order_ack_rtt" / "fill_rtt"。
//   线程安全（内部互斥）：发送点在交易网关线程，回报点在引擎线程。
// ---------------------------------------------------------------------------
class LatencyCorrelator {
public:
    static LatencyCorrelator& Instance();

    // 订单发出网卡时调用：记录 entno → send_ns
    void RecordSend(const int64_t entno, const int64_t send_ns);

    // 订单 ack（kPlaceOrderConfirm）到达：算 order_ack_rtt 并记录，移除映射
    void OnAck(const int64_t entno);

    // 成交回报（kTradePush）到达：算 fill_rtt 并记录。
    // 不立即移除映射（一个订单可能分多次成交；由 Cleanup 清理）。
    void OnFill(const int64_t entno);

    // 清理早于 cutoff_ns 的映射，防止内存无限增长（cutoff 为 monotonic ns）
    void CleanupOlderThan(const int64_t cutoff_ns);

private:
    LatencyCorrelator() = default;
    std::unordered_map<int64_t, int64_t> m_send_time;  // entno → send_ns
    mutable std::mutex m_mtx;
};

} // namespace zrt

#endif // __linux__
