// =============================================================================
// latency_collector.cpp —— 多指标延时直方图实现
// =============================================================================
#ifdef __linux__

#include "zrtools/latency/latency_collector.h"
#include "zrtools/latency/latency_clock.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <utility>

namespace zrt {

// ---------------------------------------------------------------------------
// 由 100ns 单位值定位桶号：在有序上界数组中二分，返回首个 >= val 的下标；
// 超过最大边界则返回溢出桶下标（kCount）。
// ---------------------------------------------------------------------------
size_t LatencyHistogram::BucketIndexOf(const int64_t val) noexcept {
    const auto* begin = LatencyBucketEdges::kEdges;
    const auto* end   = begin + LatencyBucketEdges::kCount;
    // lower_bound 找到首个 >= val 的上界；返回其下标即桶号
    const auto* it = std::lower_bound(begin, end, val);
    return static_cast<size_t>(it - begin);
}

// ---------------------------------------------------------------------------
// 原子 min/max 更新（CAS 循环，relaxed 序：统计容忍极小并发误差）
// ---------------------------------------------------------------------------
void LatencyHistogram::AtomicMin(std::atomic<int64_t>& slot, const int64_t v) noexcept {
    int64_t cur = slot.load(std::memory_order_relaxed);
    while (v < cur && !slot.compare_exchange_weak(cur, v, std::memory_order_relaxed, std::memory_order_relaxed)) {
        // cur 已被 CAS 更新为最新值，继续比较
    }
}

void LatencyHistogram::AtomicMax(std::atomic<int64_t>& slot, const int64_t v) noexcept {
    int64_t cur = slot.load(std::memory_order_relaxed);
    while (v > cur && !slot.compare_exchange_weak(cur, v, std::memory_order_relaxed, std::memory_order_relaxed)) {
        // 同上
    }
}

// ---------------------------------------------------------------------------
// 生成本指标快照：拷贝原子计数后离线计算百分位 / mean / std
// ---------------------------------------------------------------------------
LatencySnapshot LatencyHistogram::Snapshot(const std::string& metric_name) const {
    LatencySnapshot snap {};
    snap.metric = metric_name;

    // 一次性拷贝各桶计数
    snap.bucket_counts.reserve(LatencyBucketEdges::kCount + 1);
    for (size_t i = 0; i <= LatencyBucketEdges::kCount; ++i) {
        snap.bucket_counts.push_back(m_bucket_counts[i].load(std::memory_order_relaxed));
    }

    const uint64_t count = m_count.load(std::memory_order_relaxed);
    snap.count = count;
    if (count == 0) {
        return snap;  // 无样本，其余字段保持 0
    }

    // mean / min / max（ns → us）
    const uint64_t sum_ns  = m_sum_ns.load(std::memory_order_relaxed);
    const double   mean_ns = static_cast<double>(sum_ns) / static_cast<double>(count);
    snap.mean_us = mean_ns / 1000.0;
    snap.min_us  = static_cast<double>(m_min_ns.load(std::memory_order_relaxed)) / 1000.0;
    snap.max_us  = static_cast<double>(m_max_ns.load(std::memory_order_relaxed)) / 1000.0;

    // std：var = E[x²] - E[x]²（ns 域），再开方转 us
    const uint64_t sum_sq_ns = m_sum_sq_ns.load(std::memory_order_relaxed);
    const double   e_x2      = static_cast<double>(sum_sq_ns) / static_cast<double>(count);
    const double   var_ns    = e_x2 - mean_ns * mean_ns;
    snap.std_us = (var_ns > 0.0) ? (std::sqrt(var_ns) / 1000.0) : 0.0;

    // 百分位：遍历桶累积计数，达到目标秩时在桶内线性插值
    // Percentile 返回 100ns 单位值（与 kEdges 一致），÷10 转 µs
    snap.p50_us  = Percentile(snap.bucket_counts, count, 0.50) / 10.0;
    snap.p90_us  = Percentile(snap.bucket_counts, count, 0.90) / 10.0;
    snap.p99_us  = Percentile(snap.bucket_counts, count, 0.99) / 10.0;
    snap.p999_us = Percentile(snap.bucket_counts, count, 0.999) / 10.0;

    return snap;
}

// ---------------------------------------------------------------------------
// 百分位反解：bucket_counts 为各桶计数，count 为总数，q 为分位
//   返回该分位对应的延时的"桶内线性插值"估计值（单位：100ns，调用方 ÷10 转 µs）
// ---------------------------------------------------------------------------
double LatencyHistogram::Percentile(const std::vector<uint64_t>& bucket_counts,
                                    const uint64_t count, const double q) noexcept {
    const double target_rank = q * static_cast<double>(count);
    double accum = 0.0;
    for (size_t i = 0; i < bucket_counts.size(); ++i) {
        const double prev = accum;
        accum += static_cast<double>(bucket_counts[i]);
        if (accum < target_rank) {
            continue;
        }
        // 目标秩落在第 i 个桶内
        // 桶上下界（us）：首桶下界 0；其余桶下界为上一上界；末桶(溢出)上界用最大边界代表
        const double lower = (i == 0) ? 0.0 : static_cast<double>(LatencyBucketEdges::kEdges[i - 1]);
        const double upper = (i < LatencyBucketEdges::kCount)
                                 ? static_cast<double>(LatencyBucketEdges::kEdges[i])
                                 : static_cast<double>(LatencyBucketEdges::kEdges[LatencyBucketEdges::kCount - 1]);
        const double bucket_cnt = static_cast<double>(bucket_counts[i]);
        if (bucket_cnt <= 0.0) {
            return lower;  // 空桶不应到达，防御性
        }
        // 桶内线性插值：目标秩在桶内的相对位置
        const double frac = (target_rank - prev) / bucket_cnt;
        return lower + (upper - lower) * frac;
    }
    // 理论上不会走到（target_rank <= count）
    return static_cast<double>(LatencyBucketEdges::kEdges[LatencyBucketEdges::kCount - 1]);
}

// ---------------------------------------------------------------------------
// 清零本指标全部计数（周期窗口统计用；与热路径 Record 有轻微竞态可接受）
// ---------------------------------------------------------------------------
void LatencyHistogram::Reset() noexcept {
    for (size_t i = 0; i <= LatencyBucketEdges::kCount; ++i) {
        m_bucket_counts[i].store(0, std::memory_order_relaxed);
    }
    m_count.store(0, std::memory_order_relaxed);
    m_sum_ns.store(0, std::memory_order_relaxed);
    m_sum_sq_ns.store(0, std::memory_order_relaxed);
    m_min_ns.store(INT64_MAX, std::memory_order_relaxed);
    m_max_ns.store(0, std::memory_order_relaxed);
}

// ===========================================================================
// LatencyCollector 单例与注册表
// ===========================================================================
LatencyCollector& LatencyCollector::Instance() {
    // Meyer's 单例：C++11 起线程安全初始化
    static LatencyCollector inst {};
    return inst;
}

int LatencyCollector::RegisterMetric(const std::string_view name) {
    const std::string key {name};
    const auto it = m_name_to_id.find(key);
    if (it != m_name_to_id.end()) {
        return it->second;  // 幂等
    }
    const int id = static_cast<int>(m_metrics.size());
    // 原地默认构造 MetricEntry（histogram 含 atomic 不可移动），再赋名；
    // deque 不搬迁元素，避免对 LatencyHistogram 的移动构造。
    m_metrics.emplace_back();
    m_metrics.back().name = key;
    m_name_to_id.emplace(std::move(key), id);
    return id;
}

void LatencyCollector::RecordByName(const std::string_view name, const int64_t delta_ns) {
    const std::string key {name};
    const auto it = m_name_to_id.find(key);
    if (it == m_name_to_id.end()) {
        return;  // 未注册指标，静默丢弃
    }
    m_metrics[it->second].histogram.Record(delta_ns);
}

std::vector<LatencySnapshot> LatencyCollector::SnapshotAll() const {
    std::vector<LatencySnapshot> out {};
    out.reserve(m_metrics.size());
    for (const auto& m : m_metrics) {
        out.push_back(m.histogram.Snapshot(m.name));
    }
    return out;
}

void LatencyCollector::Reset() {
    // 注意：Reset 与热路径 Record 存在轻微竞态（周期窗口统计可接受，
    // 偶有跨窗口样本归属误差）。仅由 flush 线程调用。
    for (auto& m : m_metrics) {
        m.histogram.Reset();
    }
}

// ===========================================================================
// LatencyCorrelator —— 订单往返延时关联（L9/L10）
// ===========================================================================
LatencyCorrelator& LatencyCorrelator::Instance() {
    static LatencyCorrelator inst {};
    return inst;
}

void LatencyCorrelator::RecordSend(const int64_t entno, const int64_t send_ns) {
    if (entno == 0) {
        return;  // 无效 entno，忽略
    }
    std::lock_guard<std::mutex> lk(m_mtx);
    m_send_time[entno] = send_ns;
}

void LatencyCorrelator::OnAck(const int64_t entno) {
    if (entno == 0) {
        return;
    }
    int64_t send_ns = 0;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        const auto it = m_send_time.find(entno);
        if (it == m_send_time.end()) {
            return;  // 未找到（已清理或非本会话发出）
        }
        send_ns = it->second;
        m_send_time.erase(it);  // ack 后移除（订单生命周期内仅一次 ack）
    }
    // 往返延时 = 当前时刻（同一时钟域 LatencyClock）- 发送时刻
    LatencyCollector::Instance().RecordByName("order_ack_rtt", LatencyClock::Now() - send_ns);
}

void LatencyCorrelator::OnFill(const int64_t entno) {
    if (entno == 0) {
        return;
    }
    int64_t send_ns = 0;
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        const auto it = m_send_time.find(entno);
        if (it != m_send_time.end()) {
            send_ns = it->second;
            found = true;
            // 不 erase：一个订单可能分多次部分成交，每次都算 fill_rtt；
            // 映射由 CleanupOlderThan 兜底清理，防内存增长。
        }
    }
    if (found) {
        LatencyCollector::Instance().RecordByName("fill_rtt", LatencyClock::Now() - send_ns);
    }
}

void LatencyCorrelator::CleanupOlderThan(const int64_t cutoff_ns) {
    std::lock_guard<std::mutex> lk(m_mtx);
    for (auto it = m_send_time.begin(); it != m_send_time.end(); ) {
        if (it->second < cutoff_ns) {
            it = m_send_time.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace zrt

#endif // __linux__
