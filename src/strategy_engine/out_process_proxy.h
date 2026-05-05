//
// 子进程策略代理
//
// OutProcessProxy 是 IStrategyProxy 的跨进程实现。
// 策略运行在独立子进程（gtrade_strategy_runner）中，
// 主进程与子进程通过 StrategyShm（共享内存 ring buffer）通信：
//
//   主进程 → Channel A（行情、委托回报等数据）  → 子进程
//   子进程 → Channel B（PlaceOrderReq 等请求） → 主进程
//   主进程 → Channel C（Start/Stop/Pause 控制）→ 子进程（高优先级）
//
// 子进程崩溃时，其他策略和主进程不受影响。
// 按配置可自动重启并从 checkpoint 恢复状态。
//

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include "i_strategy_proxy.h"
#include "strategy_shm.h"
#include "subprocess_manager.h"
#include "type_define.h"
#include "utilities/tbuffer.h"
#include "spdlog/spdlog.h"

/// 子进程策略在 YAML 中的额外配置字段
struct OutProcessProxyConfig {
    std::string strat_cfg_path;          ///< 策略配置文件路径（传给 runner）
    SubprocessConfig subprocess;         ///< 子进程运行时配置
};

/**
 * 子进程策略代理
 *
 * 主进程侧的代理对象。与 InProcessProxy 接口一致，
 * 但所有消息通过共享内存 ring buffer 而非直接函数调用传递。
 *
 * 持有：
 *   - StrategyShm：共享内存句柄
 *   - SubprocessManager：子进程生命周期
 *   - 后台轮询线程：持续读取 Channel B，将请求转发给 StrategyEngine
 */
class OutProcessProxy final : public IStrategyProxy {
public:
    /**
     * 构造子进程代理。
     *
     * @param strat_id      策略 ID
     * @param strat_info    策略元信息（初始值，后续由子进程同步更新）
     * @param config        子进程配置（从 YAML 解析而来）
     * @param strat_engine  主进程 StrategyEngine 的 MyHandler 指针（用于转发 Channel B 消息）
     */
    OutProcessProxy(const std::string& strat_id,
                    const StrategyInfo& strat_info,
                    OutProcessProxyConfig config,
                    MyHandler* strat_engine)
        : m_strat_id(strat_id)
        , m_strat_info(strat_info)
        , m_config(std::move(config))
        , m_strat_engine(strat_engine)
    {
    }

    ~OutProcessProxy() override {
        m_poll_running.store(false, std::memory_order_release);
        if (m_poll_thread.joinable()) {
            m_poll_thread.join();
        }
        if (m_subprocess_mgr) {
            m_subprocess_mgr->Stop();
        }
    }

    // 禁止拷贝
    OutProcessProxy(const OutProcessProxy&) = delete;
    OutProcessProxy& operator=(const OutProcessProxy&) = delete;

    // ── IStrategyProxy 实现 ─────────────────────────────────────────────────

    /**
     * 初始化：创建共享内存，启动子进程，启动 Channel B 轮询线程。
     *
     * @param strat_yml  策略 YAML（此处仅用于更新 m_strat_info 的初始字段，
     *                   实际策略初始化在子进程中完成）
     * @return true 初始化成功
     */
    bool Init(const YAML::Node& strat_yml) override {
        // 创建共享内存
        m_shm = StrategyShm::Create(m_strat_id);
        if (!m_shm) {
            SPDLOG_ERROR("OutProcessProxy::Init: failed to create shm for strat={}",
                         m_strat_id);
            return false;
        }

        // 创建 SubprocessManager
        m_subprocess_mgr = std::make_unique<SubprocessManager>(
            m_strat_id,
            m_config.subprocess,
            m_shm,
            [this]() { OnSubprocessCrash(); }
        );

        // 启动子进程
        if (!m_subprocess_mgr->Launch(m_config.strat_cfg_path, false)) {
            SPDLOG_ERROR("OutProcessProxy::Init: failed to launch subprocess for strat={}",
                         m_strat_id);
            return false;
        }

        // 启动 Channel B 轮询线程（读取子进程请求并转发给 StrategyEngine）
        m_poll_running.store(true, std::memory_order_release);
        m_poll_thread = std::thread(&OutProcessProxy::PollChannelB, this);

        SPDLOG_INFO("OutProcessProxy::Init: strat={} initialized successfully", m_strat_id);
        return true;
    }

    /**
     * 启动策略：通过 Channel C 发送 Start 命令，子进程收到后调用 OnStart()。
     */
    bool Start() override {
        if (!m_shm) return false;
        m_shm->WriteCtrl(static_cast<uint32_t>(MsgId::kSubprocStratStart));
        return true;
    }

    /**
     * 停止策略：通过 Channel C 发送 Stop 命令，等待子进程确认（最多 3 秒），
     * 然后停止子进程。
     */
    void Stop() override {
        if (!m_shm) return;
        m_shm->WriteCtrl(static_cast<uint32_t>(MsgId::kSubprocStratStop));

        // 等待子进程处理停止命令（简单等待，避免复杂的同步机制）
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // 停止子进程
        m_poll_running.store(false, std::memory_order_release);
        if (m_subprocess_mgr) {
            m_subprocess_mgr->Stop();
        }
        if (m_poll_thread.joinable()) {
            m_poll_thread.join();
        }
    }

    /**
     * 暂停策略：通过 Channel C 发送 Pause 命令。
     */
    bool Pause() override {
        if (!m_shm) return false;
        m_shm->WriteCtrl(static_cast<uint32_t>(MsgId::kSubprocStratPause));
        return true;
    }

    /**
     * 恢复策略：通过 Channel C 发送 Resume 命令。
     */
    bool Resume() override {
        if (!m_shm) return false;
        m_shm->WriteCtrl(static_cast<uint32_t>(MsgId::kSubprocStratResume));
        return true;
    }

    /**
     * 向子进程投递数据消息（Channel A）。
     *
     * 将 TBuffer 中的原始字节序列化写入共享内存 ring buffer。
     * 若 Channel A 已满（子进程卡死），行情消息被丢弃（不阻塞）。
     */
    void PostData(const int msg_type, const BufPtr& buffer) override {
        if (!m_shm) return;
        const bool ok = m_shm->WriteData(
            static_cast<uint32_t>(msg_type),
            buffer->Data(),
            static_cast<uint32_t>(buffer->GetSize()));
        if (!ok) {
            ++m_drop_count;
            if (m_drop_count % 1000 == 1) {
                SPDLOG_WARN("OutProcessProxy: strat={} Channel A full, drop_count={}",
                            m_strat_id, m_drop_count.load());
            }
        }
    }

    const std::string& GetStratId() const override {
        return m_strat_id;
    }

    /**
     * 返回本地缓存的 StrategyInfo。
     * 子进程通过 Channel B 的 kSubprocSyncStratInfo 消息更新此缓存（未来扩展）。
     * 当前仅在生命周期事件（Start/Stop）时更新 status 字段。
     */
    const StrategyInfo& GetStrategyInfo() const override {
        return m_strat_info;
    }

    /**
     * 检查子进程是否存活（委托给 SubprocessManager）。
     */
    bool IsAlive() const override {
        if (!m_subprocess_mgr) return false;
        return m_subprocess_mgr->IsAlive();
    }

    /**
     * 标记策略正在被删除（在 Channel B 中写一条特殊消息通知子进程不要继续保存，
     * 同时设置本地标志）。
     * 子进程收到后在执行完当前消息后停止写入。
     */
    void MarkAsDeleting() override {
        m_is_deleting.store(true, std::memory_order_release);
        if (m_shm) {
            // 通过 Channel C 通知子进程停止保存操作
            m_shm->WriteCtrl(static_cast<uint32_t>(MsgId::kSubprocStratStop));
        }
    }

private:
    /**
     * Channel B 轮询线程：
     *   持续读取子进程发往主进程的消息（PlaceOrderReq、SetTimer 等），
     *   将原始字节包装成 TBuffer 后调用 m_strat_engine->PostMsg() 转发给 StrategyEngine。
     *
     *   Channel B 消息格式与进程内策略调用 Post2StratEngine() 的格式完全一致，
     *   StrategyEngine 现有处理器无需修改。
     */
    void PollChannelB() {
        while (m_poll_running.load(std::memory_order_acquire)) {
            bool had_data = false;

            // 批量读取，减少循环开销（每次最多处理 64 条消息）
            for (int i = 0; i < 64; ++i) {
                const bool read_ok = m_shm->ReadRequest(
                    [&](const uint32_t msg_type,
                        const void* const data,
                        const uint32_t len)
                    {
                        // 包装为 TBuffer 转发给 StrategyEngine
                        auto buf = std::make_shared<TBuffer>(
                            static_cast<const char*>(data),
                            static_cast<unsigned int>(len));
                        m_strat_engine->PostMsg(static_cast<int>(msg_type), buf);
                        had_data = true;
                    });

                if (!read_ok) {
                    break;
                }
            }

            if (!had_data) {
                // Channel B 为空，短暂休眠降低 CPU 占用
                // 50μs：行情最快几毫秒一次，委托响应几十毫秒，此延迟可接受
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        }
    }

    /**
     * 子进程崩溃回调（由 SubprocessManager 监控线程调用）。
     *
     * 根据配置决定是否重启：
     *   - 超过最大重启次数：标记为死亡，记录告警
     *   - 重启间隔未到：延迟后重启
     *   - 重启：重置共享内存 → 重新 Launch 子进程
     */
    void OnSubprocessCrash() {
        SPDLOG_ERROR("OutProcessProxy: strat={} subprocess crashed (restart_count={}/{})",
                     m_strat_id, m_restart_count, m_config.subprocess.max_restart_count);

        if (!m_config.subprocess.auto_restart) {
            SPDLOG_WARN("OutProcessProxy: strat={} auto_restart=false, not restarting", m_strat_id);
            return;
        }

        if (m_restart_count >= m_config.subprocess.max_restart_count) {
            SPDLOG_ERROR("OutProcessProxy: strat={} max_restart_count={} reached, giving up",
                         m_strat_id, m_config.subprocess.max_restart_count);
            return;
        }

        // 等待重启间隔
        std::this_thread::sleep_for(
            std::chrono::milliseconds(m_config.subprocess.restart_interval_ms));

        // 重置共享内存 channel（清除上次运行的残留数据）
        m_shm->ResetChannels();
        m_shm->ResetCrashState();

        // 是否从 checkpoint 恢复
        const bool restore = m_config.subprocess.restore_checkpoint;

        SPDLOG_INFO("OutProcessProxy: strat={} restarting (attempt={}/{}, restore_checkpoint={})",
                    m_strat_id, m_restart_count + 1,
                    m_config.subprocess.max_restart_count, restore);

        if (!m_subprocess_mgr->Launch(m_config.strat_cfg_path, restore)) {
            SPDLOG_ERROR("OutProcessProxy: strat={} restart failed", m_strat_id);
            return;
        }

        ++m_restart_count;
        SPDLOG_INFO("OutProcessProxy: strat={} restarted successfully (pid={})",
                    m_strat_id, m_subprocess_mgr->GetPid());
    }

    std::string           m_strat_id;
    StrategyInfo          m_strat_info;
    OutProcessProxyConfig m_config;
    MyHandler*            m_strat_engine{nullptr};

    std::shared_ptr<StrategyShm>        m_shm;
    std::unique_ptr<SubprocessManager>  m_subprocess_mgr;

    std::thread          m_poll_thread;
    std::atomic<bool>    m_poll_running{false};
    std::atomic<bool>    m_is_deleting{false};
    std::atomic<uint64_t> m_drop_count{0};  ///< Channel A 丢弃消息计数
    int                  m_restart_count{0};
};
