//
// 策略进程隔离 —— 跨进程共享内存通信层
//
// 设计目标：
//   - 主进程与子进程策略之间通过共享内存传递消息，实现进程级隔离
//   - 变长消息，零拷贝，SPSC wait-free
//   - 一块 2MB 大页内存（可选）包含三个单向 ring buffer channel
//
// 内存布局（2MB 对齐，一个 memfd fd）：
//
//   [StrategyShmGlobalHeader]  —— 全局控制头（512 bytes，含心跳、崩溃信息）
//   [Channel A header + data]  —— 主→子，数据（tick、kline、委托回报）
//   [Channel B header + data]  —— 子→主，请求（PlaceOrderReq 等）
//   [Channel C header + data]  —— 主→子，控制（Start/Stop/Pause，高优先级）
//
// 消息帧格式（在 data 区内）：
//   [uint32_t msg_type][uint32_t payload_len][payload bytes, 8字节对齐]
//
// 特殊 msg_type：kWrapSentinel(0xFFFFFFFF) —— 回绕标记，消费者跳至 buffer 起点
//
// 使用方法（主进程创建，子进程 attach）：
//   // 主进程
//   auto shm = StrategyShm::Create("my_strat");
//   shm->WriteData(kDepth1, buf->Data(), buf->GetSize());  // Channel A
//
//   // 子进程
//   auto shm = StrategyShm::Attach("my_strat");
//   shm->ReadData([&](uint32_t msg_type, const void* data, uint32_t len) { ... });
//

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "spdlog/spdlog.h"

// ─────────────────────────────────────────────────────────────────────────────
// 常量定义
// ─────────────────────────────────────────────────────────────────────────────

/// 共享内存总大小（2MB = 一个大页）
static constexpr uint32_t kStrategyShmTotalSize = 2u * 1024u * 1024u;

/// 消息帧头大小（msg_type 4B + payload_len 4B）
static constexpr uint32_t kShmMsgHeaderSize = 8u;

/// 回绕哨兵 msg_type（消费者遇到此值则跳回 buffer 起点）
static constexpr uint32_t kShmWrapSentinel = 0xFFFFFFFFu;

/// Channel A 数据区大小：主→子，行情 + 委托回报（1.5MB）
static constexpr uint32_t kShmChannelADataSize = 1536u * 1024u;

/// Channel B 数据区大小：子→主，委托请求等（256KB）
static constexpr uint32_t kShmChannelBDataSize = 256u * 1024u;

/// Channel C 数据区大小：主→子，控制命令（64KB）
static constexpr uint32_t kShmChannelCDataSize = 64u * 1024u;

// ─────────────────────────────────────────────────────────────────────────────
// Ring buffer channel 控制头
// 每个 channel 的 header 独占两个 cache line，避免 false sharing：
//   第一 cache line：write_pos（生产者独占）
//   第二 cache line：read_pos（消费者独占）
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(64) ShmChannelHeader {
    // ── 生产者 cache line ──────────────────────────────────────────────────
    std::atomic<uint64_t> write_pos{0};  ///< 已写入的字节数（单调递增）
    uint8_t _pad_w[56];                  ///< 填满 64 bytes，防止 false sharing

    // ── 消费者 cache line ──────────────────────────────────────────────────
    std::atomic<uint64_t> read_pos{0};   ///< 已读取的字节数（单调递增）
    uint8_t _pad_r[48];                  ///< 填满 64 bytes
    uint32_t capacity{0};                ///< 数据区大小（bytes，power-of-2 更优）
    uint32_t _reserved{0};
};

static_assert(sizeof(ShmChannelHeader) == 128,
    "ShmChannelHeader must be exactly 128 bytes (2 cache lines)");

// ─────────────────────────────────────────────────────────────────────────────
// 全局共享内存头（512 bytes）
// 含三个 channel 的偏移、子进程心跳时间戳、崩溃信息
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint32_t kShmGlobalHeaderSize = 512u;
static constexpr uint32_t kShmMagic = 0x47545348u;  // "GTSH" = GTradeStrategyShm
static constexpr uint32_t kShmVersion = 1u;

struct StrategyShmGlobalHeader {
    uint32_t magic{kShmMagic};
    uint32_t version{kShmVersion};
    uint32_t channel_a_offset{0};   ///< Channel A ShmChannelHeader 相对于 shm 起点的偏移
    uint32_t channel_b_offset{0};
    uint32_t channel_c_offset{0};
    uint32_t _reserved0{0};

    /// 子进程心跳（microseconds since epoch），子进程每 500ms 更新一次
    std::atomic<int64_t> heartbeat_us{0};

    /// 子进程崩溃信号（子进程捕获 SIGSEGV/SIGABRT 时写入信号编号，主进程轮询）
    std::atomic<uint32_t> crash_signal{0};

    /// 崩溃描述字符串（子进程信号处理函数写入，主进程读取日志）
    char crash_info[256]{};

    /// 填充到 512 bytes：
    ///   6×uint32_t=24 + atomic<int64_t>=8 + atomic<uint32_t>=4 + char[256]=256 = 292
    ///   512 - 292 = 220 bytes padding
    uint8_t _pad[220]{};
};

static_assert(sizeof(StrategyShmGlobalHeader) == kShmGlobalHeaderSize,
    "StrategyShmGlobalHeader must be exactly 512 bytes");

// ─────────────────────────────────────────────────────────────────────────────
// SPSC ring buffer 低级读写操作（自由函数，操作共享内存指针）
// ─────────────────────────────────────────────────────────────────────────────

namespace strat_shm_detail {

/// 将 len 向上对齐到 8 字节
inline constexpr uint32_t align8(const uint32_t len) {
    return (len + 7u) & ~7u;
}

/**
 * 向 ring buffer channel 写入一条消息（生产者调用）。
 *
 * 若消息写到 buffer 末尾时空间不足，先写回绕哨兵，再从 buffer 起点写实际消息，
 * 两次写入在一个 write_pos.store(release) 中完成，消费者不会观察到中间状态。
 *
 * @param hdr     channel 控制头指针（共享内存中）
 * @param data    channel 数据区指针（header 之后紧接）
 * @param msg_type 消息类型（MsgId 枚举值）
 * @param payload  消息 payload 数据指针
 * @param len      payload 长度（bytes）
 * @return true  写入成功
 * @return false buffer 已满，消息被丢弃
 */
inline bool WriteMsg(ShmChannelHeader* const hdr,
                     uint8_t* const data,
                     const uint32_t msg_type,
                     const void* const payload,
                     const uint32_t len)
{
    const uint32_t aligned_len = align8(len);
    const uint32_t msg_size = kShmMsgHeaderSize + aligned_len;  // 8字节对齐的帧总大小

    // 以 relaxed 读 write_pos（仅生产者写，无竞争），以 acquire 读 read_pos（消费者写）
    const uint64_t wp = hdr->write_pos.load(std::memory_order_relaxed);
    const uint64_t rp = hdr->read_pos.load(std::memory_order_acquire);

    const uint32_t cap = hdr->capacity;
    const uint32_t write_off = static_cast<uint32_t>(wp % cap);
    const uint32_t remaining = cap - write_off;  // buffer 末尾剩余字节数

    if (remaining < msg_size) {
        // 需要回绕：先在 remaining 处写哨兵，再在 buffer 起点写消息
        // 总共需要的空闲空间 = remaining（哨兵占位） + msg_size（实际消息）
        const uint64_t free_space = cap - (wp - rp);
        if (free_space < remaining + msg_size) {
            return false;  // buffer 满，丢弃
        }

        // 写回绕哨兵（仅占据 buffer 末尾剩余区域）
        // 注意：remaining >= kShmMsgHeaderSize 保证（capacity 为 8 的倍数，write_off 也为 8 的倍数）
        uint32_t sentinel = kShmWrapSentinel;
        uint32_t sentinel_len = 0u;
        std::memcpy(data + write_off,     &sentinel,     4);
        std::memcpy(data + write_off + 4, &sentinel_len, 4);

        // 在 buffer 起点写实际消息
        std::memcpy(data,     &msg_type, 4);
        std::memcpy(data + 4, &len,      4);
        if (len > 0) {
            std::memcpy(data + 8, payload, len);
        }

        // 一次性更新 write_pos（release 语义确保上方所有写操作对消费者可见）
        hdr->write_pos.store(wp + remaining + msg_size, std::memory_order_release);
        return true;
    }

    // 普通路径：在当前 write_off 处写消息
    const uint64_t free_space = cap - (wp - rp);
    if (free_space < msg_size) {
        return false;  // buffer 满，丢弃
    }

    uint8_t* const dst = data + write_off;
    std::memcpy(dst,     &msg_type, 4);
    std::memcpy(dst + 4, &len,      4);
    if (len > 0) {
        std::memcpy(dst + 8, payload, len);
    }

    hdr->write_pos.store(wp + msg_size, std::memory_order_release);
    return true;
}

/**
 * 从 ring buffer channel 读取一条消息（消费者调用）。
 *
 * 遇到回绕哨兵时自动跳到 buffer 起点，对调用方透明。
 *
 * @param hdr      channel 控制头指针
 * @param data     channel 数据区指针
 * @param callback 消息回调：void(uint32_t msg_type, const void* payload, uint32_t len)
 * @return true  读取到一条消息并调用了 callback
 * @return false buffer 为空
 */
template<typename Callback>
inline bool ReadMsg(ShmChannelHeader* const hdr,
                    const uint8_t* const data,
                    Callback&& callback)
{
    // acquire 读 write_pos（生产者写），relaxed 读 read_pos（仅消费者修改）
    const uint64_t rp = hdr->read_pos.load(std::memory_order_relaxed);
    const uint64_t wp = hdr->write_pos.load(std::memory_order_acquire);

    if (rp >= wp) {
        return false;  // buffer 空
    }

    const uint32_t cap = hdr->capacity;
    const uint32_t read_off = static_cast<uint32_t>(rp % cap);

    uint32_t msg_type = 0u;
    std::memcpy(&msg_type, data + read_off, 4);

    if (msg_type == kShmWrapSentinel) {
        // 回绕：跳过 buffer 末尾剩余区域，read_pos 推进到下一轮起点
        const uint32_t remaining = cap - read_off;
        const uint64_t new_rp = rp + remaining;
        hdr->read_pos.store(new_rp, std::memory_order_release);

        // 递归读取实际消息（最多一层，生产者不会连续回绕两次）
        return ReadMsg(hdr, data, std::forward<Callback>(callback));
    }

    // 读取 payload_len
    uint32_t payload_len = 0u;
    std::memcpy(&payload_len, data + read_off + 4, 4);

    // 调用回调
    callback(msg_type, data + read_off + 8, payload_len);

    // 推进 read_pos（对齐到 8 字节）
    const uint32_t aligned_len = align8(payload_len);
    hdr->read_pos.store(rp + kShmMsgHeaderSize + aligned_len, std::memory_order_release);
    return true;
}

/**
 * 向 ring buffer channel 写入控制命令（不携带 payload，仅 msg_type）。
 * 用于 Channel C 的 Start/Stop/Pause/Resume 等命令。
 *
 * @param hdr      channel 控制头指针
 * @param data     channel 数据区指针
 * @param msg_type 命令类型
 * @return true 写入成功，false buffer 满（实际不可能发生：控制命令极低频）
 */
inline bool WriteCmd(ShmChannelHeader* const hdr,
                     uint8_t* const data,
                     const uint32_t msg_type)
{
    // 控制命令不带 payload：len=0 时 WriteMsg 不会读 payload，但传 nullptr 会让
    // GCC 13 在 -O3 内联后由 _FORTIFY_SOURCE 的 __builtin___memcpy_chk 报硬错误
    // （error: argument 2 null where non-null expected），故用 data（非空且合法）占位，
    // 语义与传 nullptr、len=0 完全一致。
    return WriteMsg(hdr, data, msg_type, data, 0u);
}

}  // namespace strat_shm_detail

// ─────────────────────────────────────────────────────────────────────────────
// StrategyShm：策略共享内存的 RAII 句柄
//
// 主进程调用 Create() 创建并初始化；子进程调用 Attach() 附着到已有共享内存。
// 销毁时，主进程负责删除（unlink），子进程仅 munmap。
// ─────────────────────────────────────────────────────────────────────────────
class StrategyShm {
public:
    ~StrategyShm() {
        if (m_base) {
            ::munmap(m_base, kStrategyShmTotalSize);
            m_base = nullptr;
        }
        if (m_is_owner && !m_shm_name.empty()) {
            // 主进程删除命名共享内存
            const std::string path = "/dev/shm/" + m_shm_name;
            ::shm_unlink(m_shm_name.c_str());
        }
    }

    // 禁止拷贝
    StrategyShm(const StrategyShm&) = delete;
    StrategyShm& operator=(const StrategyShm&) = delete;

    /**
     * 主进程调用：创建并初始化策略共享内存。
     *
     * @param strat_id  策略 ID，用于生成唯一共享内存名称
     * @param use_huge  是否使用大页（需要系统支持 MAP_HUGETLB），默认 false
     * @return 成功返回非空 unique_ptr，失败返回 nullptr
     */
    static std::unique_ptr<StrategyShm> Create(const std::string& strat_id,
                                                const bool use_huge = false)
    {
        const std::string shm_name = MakeShmName(strat_id);

        // 创建 POSIX 共享内存对象
        const int fd = ::shm_open(shm_name.c_str(),
                                  O_CREAT | O_RDWR | O_TRUNC,
                                  0600);
        if (fd < 0) {
            SPDLOG_ERROR("StrategyShm::Create shm_open failed: strat={} err={}",
                         strat_id, strerror(errno));
            return nullptr;
        }

        if (::ftruncate(fd, kStrategyShmTotalSize) != 0) {
            SPDLOG_ERROR("StrategyShm::Create ftruncate failed: strat={} err={}",
                         strat_id, strerror(errno));
            ::close(fd);
            ::shm_unlink(shm_name.c_str());
            return nullptr;
        }

        int map_flags = MAP_SHARED;
        if (use_huge) {
            map_flags |= MAP_HUGETLB;
        }

        // 先尝试带 MAP_HUGETLB 的 mmap；失败时退回普通页
        void* base = ::mmap(nullptr, kStrategyShmTotalSize,
                            PROT_READ | PROT_WRITE,
                            map_flags, fd, 0);

        if (base == MAP_FAILED && use_huge) {
            // 大页映射失败，退回普通页（两次 mmap 都需要 fd 有效，因此延迟 close）
            SPDLOG_WARN("StrategyShm::Create MAP_HUGETLB failed, fallback to normal pages");
            base = ::mmap(nullptr, kStrategyShmTotalSize,
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, 0);
        }

        ::close(fd);  // 两次 mmap 均完成后再关闭 fd

        if (base == MAP_FAILED) {
            SPDLOG_ERROR("StrategyShm::Create mmap failed: strat={} err={}",
                         strat_id, strerror(errno));
            ::shm_unlink(shm_name.c_str());
            return nullptr;
        }

        auto shm = std::unique_ptr<StrategyShm>(new StrategyShm{});
        shm->m_base = static_cast<uint8_t*>(base);
        shm->m_shm_name = shm_name;
        shm->m_is_owner = true;
        shm->InitLayout();

        SPDLOG_INFO("StrategyShm::Create success: strat={} shm={}", strat_id, shm_name);
        return shm;
    }

    /**
     * 子进程调用：附着到已有策略共享内存。
     *
     * @param strat_id  策略 ID
     * @return 成功返回非空 unique_ptr，失败返回 nullptr
     */
    static std::unique_ptr<StrategyShm> Attach(const std::string& strat_id) {
        const std::string shm_name = MakeShmName(strat_id);

        const int fd = ::shm_open(shm_name.c_str(), O_RDWR, 0);
        if (fd < 0) {
            SPDLOG_ERROR("StrategyShm::Attach shm_open failed: strat={} err={}",
                         strat_id, strerror(errno));
            return nullptr;
        }

        void* base = ::mmap(nullptr, kStrategyShmTotalSize,
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, 0);
        ::close(fd);

        if (base == MAP_FAILED) {
            SPDLOG_ERROR("StrategyShm::Attach mmap failed: strat={} err={}",
                         strat_id, strerror(errno));
            return nullptr;
        }

        auto* gh = reinterpret_cast<StrategyShmGlobalHeader*>(base);
        if (gh->magic != kShmMagic || gh->version != kShmVersion) {
            SPDLOG_ERROR("StrategyShm::Attach magic mismatch: strat={}", strat_id);
            ::munmap(base, kStrategyShmTotalSize);
            return nullptr;
        }

        auto shm = std::unique_ptr<StrategyShm>(new StrategyShm{});
        shm->m_base = static_cast<uint8_t*>(base);
        shm->m_shm_name = shm_name;
        shm->m_is_owner = false;  // 子进程不负责删除

        // 从 header 中读取各 channel 偏移
        shm->m_ch_a = reinterpret_cast<ShmChannelHeader*>(
            shm->m_base + gh->channel_a_offset);
        shm->m_ch_b = reinterpret_cast<ShmChannelHeader*>(
            shm->m_base + gh->channel_b_offset);
        shm->m_ch_c = reinterpret_cast<ShmChannelHeader*>(
            shm->m_base + gh->channel_c_offset);

        SPDLOG_INFO("StrategyShm::Attach success: strat={}", strat_id);
        return shm;
    }

    // ── 主进程向子进程写入（Channel A：数据，Channel C：控制） ────────────

    /**
     * 主进程写入：向 Channel A 发送数据消息（行情、委托回报等）。
     * 若队列满，行情消息直接丢弃；委托回报消息应由调用方决定策略。
     */
    bool WriteData(const uint32_t msg_type,
                   const void* const payload,
                   const uint32_t len) const
    {
        return strat_shm_detail::WriteMsg(
            m_ch_a, ChannelData(m_ch_a), msg_type, payload, len);
    }

    /**
     * 主进程写入：向 Channel C 发送控制命令（Start/Stop/Pause/Resume）。
     * 阻塞直到成功（控制命令极低频，理论上不会满）。
     */
    void WriteCtrl(const uint32_t msg_type) const {
        while (!strat_shm_detail::WriteCmd(
                m_ch_c, ChannelData(m_ch_c), msg_type)) {
            // 控制命令必须送达，自旋等待（Channel C 仅 64KB 但极低频）
            asm volatile("pause" ::: "memory");
        }
    }

    // ── 子进程向主进程写入（Channel B：委托请求等） ────────────────────────

    /**
     * 子进程写入：向 Channel B 发送请求消息（PlaceOrderReq、SetTimer 等）。
     */
    bool WriteRequest(const uint32_t msg_type,
                      const void* const payload,
                      const uint32_t len) const
    {
        return strat_shm_detail::WriteMsg(
            m_ch_b, ChannelData(m_ch_b), msg_type, payload, len);
    }

    // ── 读取接口 ──────────────────────────────────────────────────────────────

    /**
     * 子进程读取：从 Channel A 读取一条数据消息（行情、委托回报）。
     * 返回 true 表示读到了一条消息并调用了 callback。
     */
    template<typename Callback>
    bool ReadData(Callback&& callback) const {
        return strat_shm_detail::ReadMsg(
            m_ch_a, ChannelData(m_ch_a), std::forward<Callback>(callback));
    }

    /**
     * 子进程读取：从 Channel C 读取一条控制命令（高优先级，应先于 ReadData 调用）。
     */
    template<typename Callback>
    bool ReadCtrl(Callback&& callback) const {
        return strat_shm_detail::ReadMsg(
            m_ch_c, ChannelData(m_ch_c), std::forward<Callback>(callback));
    }

    /**
     * 主进程读取：从 Channel B 读取一条请求消息（PlaceOrderReq 等）。
     */
    template<typename Callback>
    bool ReadRequest(Callback&& callback) const {
        return strat_shm_detail::ReadMsg(
            m_ch_b, ChannelData(m_ch_b), std::forward<Callback>(callback));
    }

    // ── 心跳 / 崩溃信息 ──────────────────────────────────────────────────────

    /// 子进程更新心跳时间戳（microseconds since epoch）
    void UpdateHeartbeat(const int64_t us) const {
        GlobalHeader()->heartbeat_us.store(us, std::memory_order_release);
    }

    /// 主进程读取子进程心跳时间戳
    int64_t GetHeartbeat() const {
        return GlobalHeader()->heartbeat_us.load(std::memory_order_acquire);
    }

    /// 子进程写入崩溃信号（信号处理函数中调用，async-signal-safe）
    void SetCrashSignal(const int sig, const char* info) const {
        if (info && info[0]) {
            // strncpy 是 async-signal-safe 的
            ::strncpy(GlobalHeader()->crash_info, info,
                      sizeof(GlobalHeader()->crash_info) - 1);
        }
        GlobalHeader()->crash_signal.store(
            static_cast<uint32_t>(sig), std::memory_order_release);
    }

    /// 主进程读取子进程崩溃信号（0 表示未崩溃）
    uint32_t GetCrashSignal() const {
        return GlobalHeader()->crash_signal.load(std::memory_order_acquire);
    }

    const char* GetCrashInfo() const {
        return GlobalHeader()->crash_info;
    }

    /// 重置崩溃状态（主进程重启子进程后调用）
    void ResetCrashState() const {
        GlobalHeader()->crash_signal.store(0u, std::memory_order_release);
        GlobalHeader()->crash_info[0] = '\0';
        GlobalHeader()->heartbeat_us.store(0LL, std::memory_order_release);
    }

    /// 重置所有 channel（主进程在重启子进程前调用，清空旧数据）
    void ResetChannels() const {
        auto reset_channel = [](ShmChannelHeader* hdr) {
            hdr->write_pos.store(0u, std::memory_order_release);
            hdr->read_pos.store(0u, std::memory_order_release);
        };
        reset_channel(m_ch_a);
        reset_channel(m_ch_b);
        reset_channel(m_ch_c);
    }

    /// 获取共享内存名称（子进程通过此名称 attach）
    const std::string& GetShmName() const { return m_shm_name; }

private:
    StrategyShm() = default;

    /// 生成共享内存名称（POSIX shm 名称格式，不含 /dev/shm 前缀）
    static std::string MakeShmName(const std::string& strat_id) {
        return "/gtrade_strat_" + strat_id;
    }

    /// 获取全局 header 指针
    StrategyShmGlobalHeader* GlobalHeader() const {
        return reinterpret_cast<StrategyShmGlobalHeader*>(m_base);
    }

    /// 获取某 channel 的数据区指针（header 之后紧接 data 区）
    static uint8_t* ChannelData(ShmChannelHeader* const hdr) {
        return reinterpret_cast<uint8_t*>(hdr) + sizeof(ShmChannelHeader);
    }

    /// 初始化共享内存布局（Create 时调用）
    void InitLayout() {
        // 清零整块内存
        std::memset(m_base, 0, kStrategyShmTotalSize);

        // 计算各 channel 偏移（紧密排列，header + data）
        static constexpr uint32_t kChAOff =
            kShmGlobalHeaderSize;
        static constexpr uint32_t kChBOff =
            kChAOff + sizeof(ShmChannelHeader) + kShmChannelADataSize;
        static constexpr uint32_t kChCOff =
            kChBOff + sizeof(ShmChannelHeader) + kShmChannelBDataSize;
        static constexpr uint32_t kTotalUsed =
            kChCOff + sizeof(ShmChannelHeader) + kShmChannelCDataSize;

        static_assert(kTotalUsed <= kStrategyShmTotalSize,
            "Shared memory channels overflow 2MB");

        // 初始化全局 header
        auto* gh = GlobalHeader();
        gh->magic = kShmMagic;
        gh->version = kShmVersion;
        gh->channel_a_offset = kChAOff;
        gh->channel_b_offset = kChBOff;
        gh->channel_c_offset = kChCOff;

        // 获取各 channel header 指针
        m_ch_a = reinterpret_cast<ShmChannelHeader*>(m_base + kChAOff);
        m_ch_b = reinterpret_cast<ShmChannelHeader*>(m_base + kChBOff);
        m_ch_c = reinterpret_cast<ShmChannelHeader*>(m_base + kChCOff);

        // 初始化各 channel header（write_pos/read_pos 已由 memset 清零）
        m_ch_a->capacity = kShmChannelADataSize;
        m_ch_b->capacity = kShmChannelBDataSize;
        m_ch_c->capacity = kShmChannelCDataSize;
    }

    uint8_t*          m_base{nullptr};
    std::string       m_shm_name;
    bool              m_is_owner{false};  ///< true = 主进程（负责 shm_unlink）

    ShmChannelHeader* m_ch_a{nullptr};   ///< Channel A header（主→子，数据）
    ShmChannelHeader* m_ch_b{nullptr};   ///< Channel B header（子→主，请求）
    ShmChannelHeader* m_ch_c{nullptr};   ///< Channel C header（主→子，控制）
};
