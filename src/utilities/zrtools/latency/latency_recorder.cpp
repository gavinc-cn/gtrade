// =============================================================================
// latency_recorder.cpp —— 延时测量门面实现
// =============================================================================
#ifdef __linux__
#ifdef GTRADE_ENABLE_LATENCY_TEST

#include "zrtools/latency/latency_recorder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>

#include "spdlog/spdlog.h"

namespace zrt {

// ---------------------------------------------------------------------------
// 内置指标名集合（与 [TT] 打点改造处、P1/P2 新增点保持一致）
//   在此集中注册，确保即使某指标本运行零样本，也会出现在输出中（count=0）。
// ---------------------------------------------------------------------------
static const char* kBuiltinMetricNames[] = {
    "quote_parse",        // L2  行情解析（okx_ws quote_out，span from monotonic）
    "engine_recv",        //     行情到达引擎（engine_quote_in）
    "engine_dispatch",    // L3  引擎分发到策略（engine_quote_out）
    "strat_recv",         // L4  策略接收行情（strat_in，所有策略共用）
    "strat_total",        //     行情→策略完成下单（strat_out，所有策略共用）
    "engine_ent_recv",    //     引擎收到下单请求（engine_ent_in）
    "engine_ent_done",    //     引擎转交交易网关（engine_ent_out）
    "trade_recv",         //     交易网关收到订单（trade_in）
    "before_send",        //     序列化完成待发送（before_send）
    "tick2order",         // L8  端到端内部延时 ★
    "strat_decision",     // L5  策略决策耗时（P1-4：strat_out - strat_in）
    "engine_order_proc",  // L6  引擎下单处理（P1-4：engine_ent_out - engine_ent_in）
    "trade_gw_proc",      // L7  交易网关序列化（P1-4：before_send - trade_in）
    "quote_net_inbound",  // L1  行情网络下行（P1）
    "order_ack_rtt",      // L9  订单 ack 往返（P1）
    "fill_rtt"            // L10 成交往返（P1）
};

LatencyRecorder& LatencyRecorder::Instance() {
    static LatencyRecorder inst {};
    return inst;
}

void LatencyRecorder::RegisterBuiltinMetrics() {
    auto& collector = LatencyCollector::Instance();
    for (const char* name : kBuiltinMetricNames) {
        collector.RegisterMetric(name);
        // 同步初始化累积分布条目（桶计数全 0）
        CumulativeMetric cm {};
        cm.name = name;
        cm.bucket_counts.assign(LatencyBucketEdges::kCount + 1, 0);
        m_cumulative.emplace(name, std::move(cm));
    }
}

void LatencyRecorder::Init(const LatencyRecorderConfig& cfg) {
    if (m_inited.load()) {
        return;  // 幂等
    }
    m_cfg = cfg;

    // 1) 标定时钟（决定 rdtsc / clock_gettime）
    LatencyClock::Init(cfg.force_clock);
    SPDLOG_INFO("[LatencyRecorder] {}", LatencyClock::DiagnoseInfo());

    // 2) 创建输出目录
    std::error_code ec {};
    std::filesystem::create_directories(m_cfg.output_dir, ec);
    if (ec) {
        SPDLOG_ERROR("[LatencyRecorder] create_directories failed: {} dir={}", ec.message(), m_cfg.output_dir);
    }

    // 3) 打开 CSV（文件名带启动日期，避免覆盖）
    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string csv_path = m_cfg.output_dir + "/" + m_cfg.run_name + "_"
                                 + FormatIso8601Utc(now_ns).substr(0, 10) + ".csv";
    m_csv.open(csv_path, std::ios::out | std::ios::app);
    if (!m_csv.is_open()) {
        SPDLOG_ERROR("[LatencyRecorder] open csv failed: {}", csv_path);
    } else {
        WriteCsvHeader();
        SPDLOG_INFO("[LatencyRecorder] csv -> {}", csv_path);
    }

    // 4) 注册指标
    RegisterBuiltinMetrics();

    m_inited.store(true);
}

void LatencyRecorder::WriteCsvHeader() {
    if (!m_csv.is_open()) {
        return;
    }
    m_csv << "timestamp,metric,count,mean_us,min_us,p50_us,p90_us,p99_us,p999_us,max_us,std_us\n";
    m_csv.flush();
}

void LatencyRecorder::WriteCsvRows(const std::vector<LatencySnapshot>& snaps, const int64_t ts_ns) {
    if (!m_csv.is_open()) {
        return;
    }
    const std::string ts_str = FormatIso8601Utc(ts_ns);
    for (const auto& s : snaps) {
        if (s.count == 0) {
            continue;  // 本窗口无样本，跳过（避免噪声行）
        }
        m_csv << ts_str << ','
              << s.metric << ','
              << s.count << ','
              << s.mean_us << ','
              << s.min_us << ','
              << s.p50_us << ','
              << s.p90_us << ','
              << s.p99_us << ','
              << s.p999_us << ','
              << s.max_us << ','
              << s.std_us << '\n';
    }
    m_csv.flush();
}

void LatencyRecorder::AccumulateCumulative(const std::vector<LatencySnapshot>& snaps) {
    for (const auto& s : snaps) {
        auto it = m_cumulative.find(s.metric);
        if (it == m_cumulative.end()) {
            // 运行期动态新增的指标也兼容
            CumulativeMetric cm {};
            cm.name = s.metric;
            cm.bucket_counts.assign(LatencyBucketEdges::kCount + 1, 0);
            it = m_cumulative.emplace(s.metric, std::move(cm)).first;
        }
        auto& cm = it->second;
        // 桶计数累加
        const size_t n = std::min(cm.bucket_counts.size(), s.bucket_counts.size());
        for (size_t i = 0; i < n; ++i) {
            cm.bucket_counts[i] += s.bucket_counts[i];
        }
        // count / sum / sum_sq（由 mean/std/count 反解，精度足够）
        cm.count += s.count;
        const double mean_ns = s.mean_us * 1000.0;
        const double std_ns  = s.std_us * 1000.0;
        cm.sum_ns    += static_cast<uint64_t>(mean_ns * static_cast<double>(s.count));
        cm.sum_sq_ns += static_cast<uint64_t>(static_cast<double>(s.count) * (std_ns * std_ns + mean_ns * mean_ns));
        // min/max
        if (s.count > 0) {
            const int64_t smin = static_cast<int64_t>(s.min_us * 1000.0);
            const int64_t smax = static_cast<int64_t>(s.max_us * 1000.0);
            cm.min_ns = std::min(cm.min_ns, smin);
            cm.max_ns = std::max(cm.max_ns, smax);
        }
    }
}

void LatencyRecorder::FlushOnce() {
    if (!m_inited.load()) {
        return;
    }
    // wall-clock 时间戳仅作窗口标签（非测量时钟）
    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
    auto snaps = LatencyCollector::Instance().SnapshotAll();
    WriteCsvRows(snaps, now_ns);
    AccumulateCumulative(snaps);
    LatencyCollector::Instance().Reset();  // 开始下一窗口
}

void LatencyRecorder::FlushLoop() {
    const auto interval = std::chrono::milliseconds(m_cfg.flush_interval_ms);
    // 分片睡眠（50ms 粒度）以便 Shutdown 时及时退出
    const auto slice = std::chrono::milliseconds(50);
    while (m_running.load()) {
        auto remaining = interval;
        while (remaining > std::chrono::milliseconds(0) && m_running.load()) {
            const auto step = (remaining < slice) ? remaining : slice;
            std::this_thread::sleep_for(step);
            remaining -= step;
        }
        if (m_running.load()) {
            FlushOnce();
        }
    }
}

void LatencyRecorder::Start() {
    if (!m_inited.load() || m_running.load()) {
        return;
    }
    m_running.store(true);
    m_flush_thread = std::thread(&LatencyRecorder::FlushLoop, this);
    SPDLOG_INFO("[LatencyRecorder] flush thread started, interval_ms={}", m_cfg.flush_interval_ms);
}

void LatencyRecorder::Shutdown() {
    if (!m_inited.load()) {
        return;
    }
    if (m_running.load()) {
        m_running.store(false);
        if (m_flush_thread.joinable()) {
            m_flush_thread.join();
        }
    }
    // 最后 flush 一次，纳入尚未输出的尾部样本
    FlushOnce();
    WriteSummaryJson();
    if (m_csv.is_open()) {
        m_csv.close();
    }
    SPDLOG_INFO("[LatencyRecorder] shutdown, summary written");
}

void LatencyRecorder::WriteSummaryJson() {
    const std::string path = m_cfg.output_dir + "/" + m_cfg.run_name + "_summary.json";
    std::ofstream jf {path};
    if (!jf.is_open()) {
        SPDLOG_ERROR("[LatencyRecorder] open summary json failed: {}", path);
        return;
    }
    jf << "{\n";
    jf << "  \"run\": \"" << m_cfg.run_name << "\",\n";
    jf << "  \"clock\": \"" << LatencyClock::DiagnoseInfo() << "\",\n";
    // 桶边界（us），供绘图
    jf << "  \"bucket_edges_us\": [";
    for (size_t i = 0; i < LatencyBucketEdges::kCount; ++i) {
        if (i) jf << ',';
        jf << LatencyBucketEdges::kEdges[i];
    }
    jf << "],\n";
    jf << "  \"metrics\": [\n";
    bool first = true;
    for (auto& [name, cm] : m_cumulative) {
        if (!first) jf << ",\n";
        first = false;
        const double mean_ns = (cm.count > 0) ? (static_cast<double>(cm.sum_ns) / static_cast<double>(cm.count)) : 0.0;
        const double var_ns  = (cm.count > 0) ? (static_cast<double>(cm.sum_sq_ns) / static_cast<double>(cm.count) - mean_ns * mean_ns) : 0.0;
        const double p50  = (cm.count > 0) ? LatencyHistogram::Percentile(cm.bucket_counts, cm.count, 0.50) / 10.0  : 0.0;
        const double p90  = (cm.count > 0) ? LatencyHistogram::Percentile(cm.bucket_counts, cm.count, 0.90) / 10.0  : 0.0;
        const double p99  = (cm.count > 0) ? LatencyHistogram::Percentile(cm.bucket_counts, cm.count, 0.99) / 10.0  : 0.0;
        const double p999 = (cm.count > 0) ? LatencyHistogram::Percentile(cm.bucket_counts, cm.count, 0.999) / 10.0 : 0.0;

        jf << "    {\"metric\":\"" << name << "\","
           << "\"count\":" << cm.count << ","
           << "\"mean_us\":" << (mean_ns / 1000.0) << ","
           << "\"min_us\":" << (cm.count > 0 ? static_cast<double>(cm.min_ns) / 1000.0 : 0.0) << ","
           << "\"p50_us\":" << p50 << ","
           << "\"p90_us\":" << p90 << ","
           << "\"p99_us\":" << p99 << ","
           << "\"p999_us\":" << p999 << ","
           << "\"max_us\":" << (cm.count > 0 ? static_cast<double>(cm.max_ns) / 1000.0 : 0.0) << ","
           << "\"std_us\":" << (var_ns > 0.0 ? std::sqrt(var_ns) / 1000.0 : 0.0) << ","
           << "\"bucket_counts\":[";
        for (size_t i = 0; i < cm.bucket_counts.size(); ++i) {
            if (i) jf << ',';
            jf << cm.bucket_counts[i];
        }
        jf << "]}";
    }
    jf << "\n  ]\n}\n";
    jf.flush();
    SPDLOG_INFO("[LatencyRecorder] summary json -> {}", path);
}

std::string LatencyRecorder::FormatIso8601Utc(const int64_t epoch_ns) {
    const int64_t epoch_s = (epoch_ns > 0 ? epoch_ns : 0) / 1'000'000'000LL;
    std::time_t t = static_cast<std::time_t>(epoch_s);
    std::tm tm_utc {};
    gmtime_r(&t, &tm_utc);
    char buf[32] {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    return std::string(buf);
}

} // namespace zrt

#endif // GTRADE_ENABLE_LATENCY_TEST
#endif // __linux__
