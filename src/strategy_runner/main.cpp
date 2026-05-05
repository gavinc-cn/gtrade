
//
// gtrade_strategy_runner — 子进程策略运行器
//
// 入口程序，由 OutProcessProxy 通过 fork+exec 启动。
// 职责：
//   1. 解析命令行参数，加载策略 YAML 配置
//   2. Attach 共享内存（由主进程在 fork 前创建）
//   3. dlopen 策略 .so，创建 StrategyBase 实例
//   4. 主循环：轮询 Channel C（控制）→ Channel A（数据）→ 更新心跳
//   5. 捕获 SIGSEGV/SIGABRT 等信号，将崩溃信息写入共享内存后退出
//
// Channel B 的写入由 StratEngineStub 完成：
//   当策略调用 PlaceOrderReq / SetTimer 等接口时，
//   StrategyBase::Post2StratEngine() → m_strat_engine->PostMsg()
//   → StratEngineStub 的处理器 → StrategyShm::WriteRequest()
//

#ifdef __linux__

#include <csignal>
#include <cstring>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <dlfcn.h>
#include <yaml-cpp/yaml.h>
#include "spdlog/spdlog.h"
#include "pch.h"
#include "type_define.h"
#include "strategy_engine/strategy_base.h"
#include "strategy_engine/strategy_shm.h"
#include "strategy_engine/strategy_plugin.h"
#include "strategy_engine/i_strategy_channel.h"
#include "strategy_engine/strategy_zmq_channel.h"
#include "common/string_keys.h"
#include "common/msg_id.h"
#include "zrtools/io_pool_v2/engine_pool.h"
#include "zrtools/io_pool_v2/boost_asio_thread.h"

// ── 全局变量（信号处理器使用）────────────────────────────────────────────────
/// 指向共享内存对象，供 SIGSEGV 等信号处理器写崩溃信息
static StrategyShm* g_shm_ptr = nullptr;

/// 主循环运行标志
static std::atomic<bool> g_running{true};


// ── 信号处理器 ────────────────────────────────────────────────────────────────

/**
 * 崩溃信号处理（SIGSEGV / SIGABRT / SIGFPE / SIGBUS）
 *
 * 将信号编号写入共享内存崩溃区，供主进程 SubprocessManager 读取。
 * 之后恢复默认行为（生成 core dump）。
 */
static void CrashHandler(const int sig)
{
    if (g_shm_ptr) {
        g_shm_ptr->SetCrashSignal(
            static_cast<uint32_t>(sig),
            strsignal(sig) ? strsignal(sig) : "unknown signal");
    }
    // 恢复默认行为，允许内核写 core dump
    signal(sig, SIG_DFL);
    raise(sig);
}

/**
 * 优雅停止信号处理（SIGTERM）
 *
 * 设置 g_running = false，主循环在下一次迭代退出。
 */
static void TermHandler(int /*sig*/)
{
    g_running.store(false, std::memory_order_release);
}


// ── StratEngineStub ───────────────────────────────────────────────────────────

/**
 * 子进程侧的"引擎"存根
 *
 * 充当策略的 m_strat_engine。策略通过 Post2StratEngine() 发出的所有消息
 * （PlaceOrderReq、SetTimer、SubscribeQuote 等）在此处被拦截，
 * 并通过 StrategyShm::WriteRequest() 写入 Channel B，由主进程处理。
 *
 * StratEngineStub 运行在 EnginePool 的一个共享线程上；
 * 策略本身运行在另一个（或同一个）共享线程上，两者之间是异步队列，不会死锁。
 */
class StratEngineStub final : public MyHandler
{
public:
    /**
     * 构造 StratEngineStub。
     *
     * @param shm  共享内存对象，用于写 Channel B
     */
    explicit StratEngineStub(std::shared_ptr<StrategyShm> shm)
        : m_shm(std::move(shm))
    {
        // 使用共享线程（非阻塞）处理策略发来的请求
        SetThread(zrt::EnginePool::GetInstance().GetSharedThread());

        // 默认处理器：将所有消息序列化写入 Channel B
        InstallDefaultHandler([this](const int msg_type, const BufPtr buf) {
            ForwardToChannelB(msg_type, buf);
        });
    }

    bool Init() override { return true; }
    bool Start() override { return true; }

private:
    /**
     * 将消息转发到 Channel B。
     *
     * Channel B 满时丢弃并计数（类似 OutProcessProxy 丢弃 Channel A 的行为）。
     * PlaceOrderReq 等消息频率极低，满载几乎不会发生。
     */
    void ForwardToChannelB(const int msg_type, const BufPtr& buf)
    {
        if (!m_shm) return;

        const void*    data = buf ? buf->Data()    : nullptr;
        const uint32_t len  = buf ? static_cast<uint32_t>(buf->GetSize()) : 0u;

        const bool ok = m_shm->WriteRequest(
            static_cast<uint32_t>(msg_type), data, len);

        if (!ok) {
            ++m_drop_count;
            if (m_drop_count % 100 == 1) {
                SPDLOG_WARN("StratEngineStub: Channel B full, drop_count={} msg_type={}",
                            m_drop_count, msg_type);
            }
        }
    }

    std::shared_ptr<StrategyShm> m_shm;
    uint64_t m_drop_count{0};
};


// ── StratEngineStubZmq ────────────────────────────────────────────────────────

/**
 * 远程模式的"引擎"存根
 *
 * 充当策略的 m_strat_engine。策略通过 Post2StratEngine() 发出的所有消息
 * （PlaceOrderReq、SetTimer、SubscribeQuote 等）在此处被拦截，
 * 并通过 IStrategyChannel::WriteFrame() 发送到 Engine ZMQ ROUTER。
 *
 * 与 StratEngineStub（SHM 模式）对称，区别仅在于底层写入目标：
 *   SHM 模式  → StrategyShm::WriteRequest() 写共享内存 Channel B
 *   远程模式  → IStrategyChannel::WriteFrame() 写 ZMQ DEALER
 */
class StratEngineStubZmq final : public MyHandler
{
public:
    /**
     * 构造 StratEngineStubZmq。
     *
     * @param channel  ZMQ 通道（Runner 侧 DEALER），由 main() 创建并共享
     */
    explicit StratEngineStubZmq(std::shared_ptr<IStrategyChannel> channel)
        : m_channel(std::move(channel))
    {
        SetThread(zrt::EnginePool::GetInstance().GetSharedThread());

        // 默认处理器：将所有消息序列化写入 ZMQ Channel B
        InstallDefaultHandler([this](const int msg_type, const BufPtr buf) {
            ForwardToChannelB(msg_type, buf);
        });
    }

    bool Init()  override { return true; }
    bool Start() override { return true; }

private:
    /**
     * 将消息转发到 ZMQ Channel B。
     *
     * WriteFrame() 返回 false 表示 HWM 满（DEALER 内部队列），此时丢弃并计数。
     * PlaceOrderReq 等消息频率极低，满载几乎不会发生。
     */
    void ForwardToChannelB(const int msg_type, const BufPtr& buf)
    {
        if (!m_channel) return;

        const void*    data = buf ? buf->Data()    : nullptr;
        const uint32_t len  = buf ? static_cast<uint32_t>(buf->GetSize()) : 0u;

        const bool ok = m_channel->WriteFrame(
            static_cast<uint32_t>(msg_type), data, len);

        if (!ok) {
            ++m_drop_count;
            if (m_drop_count % 100 == 1) {
                SPDLOG_WARN("StratEngineStubZmq: write failed, drop_count={} msg_type={}",
                            m_drop_count, msg_type);
            }
        }
    }

    std::shared_ptr<IStrategyChannel> m_channel;
    uint64_t m_drop_count{0};
};


// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    // ── 解析命令行参数 ────────────────────────────────────────────────────────
    std::string strat_cfg_path;
    bool        restore_checkpoint = false;
    std::string mode;          // "" = SHM subprocess 模式（默认），"remote" = ZMQ 远程模式
    std::string engine_addr;   // 仅 remote 模式：Engine ZMQ 地址（如 tcp://192.168.1.10:19000）

    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--strat-config" && i + 1 < argc) {
            strat_cfg_path = argv[++i];
        } else if (arg == "--restore-checkpoint") {
            restore_checkpoint = true;
        } else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (arg == "--engine-addr" && i + 1 < argc) {
            engine_addr = argv[++i];
        }
    }

    if (strat_cfg_path.empty()) {
        SPDLOG_ERROR("Usage: gtrade_strategy_runner --strat-config <path> "
                     "[--restore-checkpoint] [--mode remote --engine-addr <zmq-addr>]");
        return 1;
    }

    const bool is_remote = (mode == "remote");

    if (is_remote && engine_addr.empty()) {
        SPDLOG_ERROR("gtrade_strategy_runner: --mode remote requires --engine-addr");
        return 1;
    }

    SPDLOG_INFO("gtrade_strategy_runner: strat_cfg={} restore_checkpoint={} mode={} engine_addr={}",
                strat_cfg_path, restore_checkpoint, mode.empty() ? "subprocess" : mode, engine_addr);

    // ── 加载策略 YAML 配置 ────────────────────────────────────────────────────
    YAML::Node strat_yml;
    try {
        strat_yml = YAML::LoadFile(strat_cfg_path);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("gtrade_strategy_runner: failed to load config {}: {}",
                     strat_cfg_path, e.what());
        return 1;
    }

    // 策略 ID 从配置文件名提取（去掉 .yml 后缀），与主进程保持一致
    const std::string strat_id =
        std::filesystem::path(strat_cfg_path).stem().string();

    if (!strat_yml[k_so_path]) {
        SPDLOG_ERROR("gtrade_strategy_runner: strat config missing '{}' field "
                     "(only .so strategies support subprocess isolation)",
                     k_so_path);
        return 1;
    }
    const std::string so_path = strat_yml[k_so_path].as<std::string>();

    SPDLOG_INFO("gtrade_strategy_runner: strat_id={} so_path={}", strat_id, so_path);

    // ── 注册信号处理器 ────────────────────────────────────────────────────────
    // CrashHandler 写 g_shm_ptr（SHM 模式）；remote 模式下 g_shm_ptr 为 null，写入被跳过
    signal(SIGSEGV, CrashHandler);
    signal(SIGABRT, CrashHandler);
    signal(SIGFPE,  CrashHandler);
    signal(SIGBUS,  CrashHandler);
    signal(SIGTERM, TermHandler);

    // ── Attach 共享内存（仅 SHM subprocess 模式）──────────────────────────────
    std::shared_ptr<StrategyShm> shm;
    if (!is_remote) {
        shm = StrategyShm::Attach(strat_id);
        if (!shm) {
            SPDLOG_ERROR("gtrade_strategy_runner: failed to attach shm for strat={}",
                         strat_id);
            return 1;
        }
        g_shm_ptr = shm.get();
    }

    // ── 初始化 EnginePool ─────────────────────────────────────────────────────
    // 子进程只需要若干共享线程（策略线程 + stub 线程），无需命名线程
    zrt::EnginePool& pool = zrt::EnginePool::GetInstance();
    pool.AddSharedEngine<BoostAsioThread>(4);
    pool.EngineStart();

    SPDLOG_INFO("gtrade_strategy_runner: EnginePool started");

    // ── 创建 ZMQ channel（仅 remote 模式）────────────────────────────────────
    // 放在 EnginePool 启动后、策略初始化前：
    //   connect() 立即返回，TCP 握手在后台进行；MonitorLoop 线程需要 EnginePool 的线程环境。
    // zmq_context 和 channel 的生命周期贯穿整个 main()，必须在 pool.EngineStop() 之前销毁。
    std::unique_ptr<zmq::context_t> zmq_context;
    std::shared_ptr<StrategyZmqChannel> zmq_channel;
    if (is_remote) {
        zmq_context = std::make_unique<zmq::context_t>(1);
        zmq_channel = std::make_shared<StrategyZmqChannel>(
            *zmq_context, engine_addr, strat_id);
        SPDLOG_INFO("gtrade_strategy_runner: ZMQ DEALER created, identity={}, engine={}",
                    strat_id, engine_addr);
    }

    // ── 创建 Stub（根据模式选择 SHM 或 ZMQ 实现）──────────────────────────────
    std::unique_ptr<MyHandler> stub_handler;
    MyHandler* stub_ptr = nullptr;
    if (is_remote) {
        auto stub_zmq = std::make_unique<StratEngineStubZmq>(zmq_channel);
        stub_zmq->Init();
        stub_ptr = stub_zmq.get();
        stub_handler = std::move(stub_zmq);
    } else {
        auto stub_shm = std::make_unique<StratEngineStub>(shm);
        stub_shm->Init();
        stub_ptr = stub_shm.get();
        stub_handler = std::move(stub_shm);
    }

    // ── dlopen .so 并创建策略实例 ─────────────────────────────────────────────

    // 相对路径以 runner 二进制目录为基准（与主进程 LoadStrategyFromSo 保持一致）
    std::filesystem::path resolved_so = so_path;
    if (resolved_so.is_relative()) {
        std::error_code ec;
        const auto exe_dir =
            std::filesystem::read_symlink("/proc/self/exe", ec).parent_path();
        if (!ec) {
            resolved_so = exe_dir / so_path;
        }
    }

    // RTLD_GLOBAL：使 .so 中未定义的符号（StrategyBase vtable、EnginePool 单例等）
    //              从本进程的全局符号表解析（runner 链接了 strategy_base 等）
    void* dl_handle = dlopen(resolved_so.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    if (!dl_handle) {
        SPDLOG_ERROR("gtrade_strategy_runner: dlopen failed for {}: {}",
                     resolved_so.string(), dlerror());
        pool.EngineStop();
        return 1;
    }

    // 验证构建模式（0=实盘，1=回测）
    dlerror();
    using GetBuildModeFunc = int (*)();
    auto get_mode_fn = reinterpret_cast<GetBuildModeFunc>(
        dlsym(dl_handle, "gtrade_get_build_mode"));
    if (const char* err = dlerror()) {
        SPDLOG_ERROR("gtrade_strategy_runner: dlsym(gtrade_get_build_mode) failed: {}", err);
        dlclose(dl_handle);
        pool.EngineStop();
        return 1;
    }
    // runner 本身始终以实盘模式编译（GTRADE_IN_BACKTEST_MODE=0），与主进程一致
    constexpr int kExpectedMode = 0;
    if (const int actual_mode = get_mode_fn(); actual_mode != kExpectedMode) {
        SPDLOG_ERROR("gtrade_strategy_runner: build mode mismatch in {}: "
                     "expected={} got={}", resolved_so.string(), kExpectedMode, actual_mode);
        dlclose(dl_handle);
        pool.EngineStop();
        return 1;
    }

    // 获取工厂函数
    dlerror();
    auto factory = reinterpret_cast<GTradeStrategyFactory>(
        dlsym(dl_handle, "gtrade_create_strategy"));
    if (const char* err = dlerror()) {
        SPDLOG_ERROR("gtrade_strategy_runner: dlsym(gtrade_create_strategy) failed: {}", err);
        dlclose(dl_handle);
        pool.EngineStop();
        return 1;
    }

    // 调用工厂创建策略实例
    // GTradeConfig 传空值：子进程不需要账户/数据库配置，策略通过 Channel B 转发请求给 Engine
    const GTradeConfig gtrade_cfg{};
    StrategyBase* raw_strategy = factory(gtrade_cfg, stub_ptr, strat_id);
    if (!raw_strategy) {
        SPDLOG_ERROR("gtrade_strategy_runner: factory returned nullptr for strat_id={}",
                     strat_id);
        dlclose(dl_handle);
        pool.EngineStop();
        return 1;
    }

    // 用 unique_ptr 管理生命周期；dlclose 必须在 delete 之后（顺序不可反转）
    auto strategy = std::unique_ptr<StrategyBase>(raw_strategy);

    // ── 初始化策略 ────────────────────────────────────────────────────────────
    // Init()   : 安装 kStratStartSync / kStratStopSync 生命周期处理器（StrategyBase 基类）
    // OnInit() : 策略自身的初始化逻辑（注册消息回调、加载配置等）
    strategy->Init();
    if (!strategy->OnInit(strat_yml)) {
        SPDLOG_ERROR("gtrade_strategy_runner: strategy OnInit failed, strat_id={}", strat_id);
        strategy.reset();
        dlclose(dl_handle);
        pool.EngineStop();
        return 1;
    }

    SPDLOG_INFO("gtrade_strategy_runner: strat={} initialized, entering main loop", strat_id);

    // ── 主循环 ────────────────────────────────────────────────────────────────
    constexpr auto kHeartbeatInterval = std::chrono::milliseconds(500);
    auto last_heartbeat_time = std::chrono::steady_clock::now();

    if (is_remote) {
        // ══════════════════════════════════════════════════════════════════════
        // 远程模式主循环
        //
        // 通道语义：
        //   Engine→Runner：Channel A（行情/委托回报）+ Channel C（控制命令）
        //     均通过同一 ZMQ DEALER ReadFrame() 读取，由 msg_type 区分
        //   Runner→Engine：Channel B（PlaceOrderReq 等，由 StratEngineStubZmq 写入）
        //                  心跳（kSubprocHeartbeat，每 500ms）
        //                  握手（kRemoteHandshake + kRemoteSyncReq，每次连接/重连触发）
        //
        // 握手时机：由 zmq_socket_monitor() ZMQ_EVENT_CONNECTED/RECONNECTED 事件驱动，
        //   PollNeedHandshake() 返回 true 时发送，确保每次 TCP 连接建立后均执行完整对账。
        //   首次连接时 SyncReq payload 为空（无 pending order ids）。
        // ══════════════════════════════════════════════════════════════════════

        // 构造握手 payload：从 YAML 读取 strat_template_id / account_id / instrument
        RemoteHandshakePayload hs_payload{};
        strncpy(hs_payload.strat_id,          strat_id.c_str(),    sizeof(hs_payload.strat_id)          - 1);
        strncpy(hs_payload.strat_template_id,
                strat_yml[k_strat_template_id] ? strat_yml[k_strat_template_id].as<std::string>().c_str() : "",
                sizeof(hs_payload.strat_template_id) - 1);
        strncpy(hs_payload.account_id,
                strat_yml[k_account_id] ? strat_yml[k_account_id].as<std::string>().c_str() : "",
                sizeof(hs_payload.account_id) - 1);
        strncpy(hs_payload.instrument,
                strat_yml[k_instrument] ? strat_yml[k_instrument].as<std::string>().c_str() : "",
                sizeof(hs_payload.instrument) - 1);

        while (g_running.load(std::memory_order_acquire))
        {
            bool had_data = false;

            // ── 1. 检查是否需要（重新）握手 ───────────────────────────────────
            // PollNeedHandshake() 由 MonitorLoop 在 ZMQ_EVENT_CONNECTED/RECONNECTED 时置位，
            // 调用后原子清零，每次重连只触发一次。
            if (zmq_channel->PollNeedHandshake()) {
                // 发握手帧（携带 RemoteHandshakePayload，Engine 用此填充 StrategyInfo）
                zmq_channel->WriteFrame(
                    static_cast<uint32_t>(MsgId::kRemoteHandshake),
                    &hs_payload, sizeof(hs_payload));
                // 发状态同步请求（payload 为空：当前版本不携带 pending_order_ids，
                // Engine 始终返回全量活跃订单，策略 OnReconnected 自行对账差异）
                zmq_channel->WriteFrame(
                    static_cast<uint32_t>(MsgId::kRemoteSyncReq), nullptr, 0);
                SPDLOG_INFO("gtrade_strategy_runner: strat={} handshake + SyncReq sent", strat_id);
            }

            // ── 2. 轮询 Engine→Runner 帧（Channel A + Channel C + SyncResp）───
            // ZMQ 模式下 Channel A 和 Channel C 共用同一 DEALER socket，
            // 以 msg_type 区分控制命令和数据帧。
            for (int i = 0; i < 64; ++i) {
                const bool ok = zmq_channel->ReadFrame(
                    [&](const uint32_t msg_type,
                        const void*    data,
                        const uint32_t len)
                    {
                        switch (static_cast<MsgId>(msg_type)) {

                        // Channel C：控制命令
                        case MsgId::kSubprocStratStart:
                            SPDLOG_INFO("gtrade_strategy_runner [remote]: strat={} Start", strat_id);
                            strategy->Start();
                            break;

                        case MsgId::kSubprocStratStop:
                            SPDLOG_INFO("gtrade_strategy_runner [remote]: strat={} Stop", strat_id);
                            strategy->Stop();
                            g_running.store(false, std::memory_order_release);
                            break;

                        case MsgId::kSubprocStratPause:
                            SPDLOG_INFO("gtrade_strategy_runner [remote]: strat={} Pause (not implemented)",
                                        strat_id);
                            break;

                        case MsgId::kSubprocStratResume:
                            SPDLOG_INFO("gtrade_strategy_runner [remote]: strat={} Resume (not implemented)",
                                        strat_id);
                            break;

                        // SyncResp：对账响应，通知策略当前全量活跃订单
                        case MsgId::kRemoteSyncResp:
                            if (data && len >= sizeof(RemoteSyncResp)) {
                                const auto* resp = static_cast<const RemoteSyncResp*>(data);
                                strategy->OnReconnected(*resp);
                            }
                            break;

                        // Channel A：行情 / 委托回报等数据帧——投递给策略线程
                        default: {
                            auto buf = std::make_shared<TBuffer>(
                                static_cast<const char*>(data),
                                static_cast<unsigned int>(len));
                            strategy->PostMsg(static_cast<int>(msg_type), buf);
                            break;
                        }
                        }
                        had_data = true;
                    });
                if (!ok) break;
            }

            // ── 3. 发送应用层心跳（每 500ms）────────────────────────────────
            // kSubprocHeartbeat：Runner→Engine 方向，无 payload。
            // Engine DispatchIncoming() 收到后以本地 steady_clock 时间更新 last_heartbeat_us，
            // 用于 RemoteProxy::IsAlive() 检测 Runner 进程存活。
            const auto now = std::chrono::steady_clock::now();
            if (now - last_heartbeat_time >= kHeartbeatInterval) {
                zmq_channel->WriteFrame(static_cast<uint32_t>(MsgId::kSubprocHeartbeat));
                last_heartbeat_time = now;
            }

            // ── 4. 空闲时短暂休眠，降低 CPU 占用 ────────────────────────────
            if (!had_data) {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        }

    } else {
        // ══════════════════════════════════════════════════════════════════════
        // SHM subprocess 模式主循环（原有逻辑，不变）
        //
        // 优先级：Channel C（控制命令）> Channel A（数据消息）
        // 每 500ms 通过 shm->UpdateHeartbeat() 更新心跳，让主进程 SubprocessManager
        // 知道子进程还活着。
        // ══════════════════════════════════════════════════════════════════════

        while (g_running.load(std::memory_order_acquire))
        {
            bool had_data = false;

            // ── 1. 轮询 Channel C（控制命令，高优先级）────────────────────────
            for (int i = 0; i < 16; ++i) {
                const bool read_ok = shm->ReadCtrl(
                    [&](const uint32_t msg_type,
                        const void* /*data*/,
                        const uint32_t /*len*/)
                    {
                        switch (static_cast<MsgId>(msg_type)) {

                        case MsgId::kSubprocStratStart:
                            SPDLOG_INFO("gtrade_strategy_runner: strat={} Start", strat_id);
                            strategy->Start();
                            break;

                        case MsgId::kSubprocStratStop:
                            SPDLOG_INFO("gtrade_strategy_runner: strat={} Stop", strat_id);
                            strategy->Stop();
                            g_running.store(false, std::memory_order_release);
                            break;

                        case MsgId::kSubprocStratPause:
                            // TODO: StrategyBase 尚未实现 Pause 接口（同 InProcessProxy）
                            SPDLOG_INFO("gtrade_strategy_runner: strat={} Pause (not implemented)",
                                        strat_id);
                            break;

                        case MsgId::kSubprocStratResume:
                            // TODO: StrategyBase 尚未实现 Resume 接口
                            SPDLOG_INFO("gtrade_strategy_runner: strat={} Resume (not implemented)",
                                        strat_id);
                            break;

                        default:
                            SPDLOG_WARN("gtrade_strategy_runner: unknown ctrl msg_type={}",
                                        msg_type);
                            break;
                        }
                        had_data = true;
                    });

                if (!read_ok) break;
            }

            // ── 2. 轮询 Channel A（数据消息：行情、委托回报等）────────────────
            for (int i = 0; i < 64; ++i) {
                const bool read_ok = shm->ReadData(
                    [&](const uint32_t msg_type,
                        const void* data,
                        const uint32_t len)
                    {
                        // 包装为 TBuffer 后投递给策略线程
                        auto buf = std::make_shared<TBuffer>(
                            static_cast<const char*>(data),
                            static_cast<unsigned int>(len));
                        strategy->PostMsg(static_cast<int>(msg_type), buf);
                        had_data = true;
                    });

                if (!read_ok) break;
            }

            // ── 3. 更新心跳 ───────────────────────────────────────────────────
            const auto now = std::chrono::steady_clock::now();
            if (now - last_heartbeat_time >= kHeartbeatInterval) {
                // 心跳时间戳：microseconds since epoch（与 SubprocessManager 检测逻辑一致）
                const int64_t now_us =
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                shm->UpdateHeartbeat(now_us);
                last_heartbeat_time = now;
            }

            // ── 4. 空闲时短暂休眠，降低 CPU 占用 ──────────────────────────────
            if (!had_data) {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        }
    }

    // ── 优雅退出 ──────────────────────────────────────────────────────────────
    SPDLOG_INFO("gtrade_strategy_runner: strat={} main loop exited, cleaning up", strat_id);

    // 关闭 ZMQ channel（stop MonitorLoop 线程，关闭 DEALER socket）
    if (zmq_channel) {
        zmq_channel->Close();
    }

    // 按顺序析构：先 strategy（执行 StrategyBase 析构和 .so 内代码），再 dlclose
    strategy.reset();
    stub_handler.reset();
    dlclose(dl_handle);

    pool.EngineStop();

    SPDLOG_INFO("gtrade_strategy_runner: strat={} exited cleanly", strat_id);
    return 0;
}

#endif // __linux__
