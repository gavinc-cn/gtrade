//
// 策略子进程生命周期管理器
//
// SubprocessManager 负责：
//   1. fork + execv 启动 gtrade_strategy_runner 子进程
//   2. 监控子进程心跳（由 StrategyShm::GetHeartbeat() 读取）
//   3. 通过 waitpid() 检测子进程崩溃
//   4. 按配置决定是否自动重启，以及是否从 checkpoint 恢复
//

#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include "spdlog/spdlog.h"
#include "strategy_shm.h"
#include "my_utc.h"

/// 子进程运行时配置（来自策略 YAML）
struct SubprocessConfig {
    bool auto_restart{false};            ///< 崩溃后是否自动重启
    bool restore_checkpoint{false};      ///< 重启后是否从 checkpoint 恢复状态
    int  max_restart_count{3};           ///< 最大重启次数（防止无限重启）
    int  restart_interval_ms{5000};      ///< 两次重启之间的最小间隔（ms）
    int  heartbeat_timeout_ms{3000};     ///< 心跳超时阈值（ms），超过则视为死亡

    /// 子进程类型：Cpp 使用 gtrade_strategy_runner，Python 使用 python_interpreter -m gtrade_py.runner
    enum class RunnerType { Cpp, Python } runner_type{RunnerType::Cpp};
    std::string python_interpreter{"python3"};  ///< Python 解释器路径（可指向 venv）
    std::string python_class;                   ///< Python 策略类名（仅 RunnerType::Python 有效）
};

/**
 * 子进程生命周期管理器
 *
 * 持有一个 gtrade_strategy_runner 子进程的句柄，负责启动、监控和停止。
 * 由 OutProcessProxy 持有，通过注册的崩溃回调通知 OutProcessProxy 处理重启逻辑。
 */
class SubprocessManager {
public:
    /**
     * @param strat_id   策略 ID
     * @param config     子进程运行时配置
     * @param shm        策略共享内存（用于读取心跳和崩溃信息）
     * @param on_crash   子进程崩溃时的回调（在监控线程中调用）
     */
    SubprocessManager(const std::string& strat_id,
                      const SubprocessConfig& config,
                      std::shared_ptr<StrategyShm> shm,
                      std::function<void()> on_crash)
        : m_strat_id(strat_id)
        , m_config(config)
        , m_shm(std::move(shm))
        , m_on_crash(std::move(on_crash))
    {
    }

    ~SubprocessManager() {
        Stop();
    }

    // 禁止拷贝
    SubprocessManager(const SubprocessManager&) = delete;
    SubprocessManager& operator=(const SubprocessManager&) = delete;

    /**
     * 启动子进程。
     *
     * @param strat_cfg_path 策略 YAML 配置文件路径（传给 runner）
     * @param restore_checkpoint 是否附带 --restore-checkpoint 参数
     * @return true 启动成功（fork + exec 成功）
     */
    bool Launch(const std::string& strat_cfg_path,
                const bool restore_checkpoint = false)
    {
        if (m_child_pid > 0) {
            SPDLOG_WARN("SubprocessManager::Launch: strat={} already running pid={}",
                        m_strat_id, m_child_pid);
            return false;
        }

        // 构造 execv 参数列表
        std::vector<std::string> args_storage;

        if (m_config.runner_type == SubprocessConfig::RunnerType::Python) {
            // ── Python runner ────────────────────────────────────────────────
            // argv: [python_interpreter, "-m", "gtrade_py.runner",
            //        "--strat-id", <id>, "--strat-config", <path>,
            //        "--strat-class", <class>]
            // Python runner 通过 --strat-config 读取 YAML 获取 python_file 等字段，
            // 与 C++ runner 接口保持一致（均通过 --strat-config 传递配置路径）。
            if (m_config.python_class.empty()) {
                SPDLOG_ERROR("SubprocessManager::Launch: python_class is empty for strat={}",
                             m_strat_id);
                return false;
            }
            args_storage.push_back(m_config.python_interpreter);
            args_storage.push_back("-m");
            args_storage.push_back("gtrade_py.runner");
            args_storage.push_back("--strat-id");
            args_storage.push_back(m_strat_id);
            args_storage.push_back("--strat-config");
            args_storage.push_back(strat_cfg_path);
            args_storage.push_back("--strat-class");
            args_storage.push_back(m_config.python_class);
        } else {
            // ── C++ runner（默认）──────────────────────────────────────────────
            // 查找 gtrade_strategy_runner 二进制（与主进程在同一 bin/ 目录）
            std::error_code ec;
            const auto exe_dir =
                std::filesystem::read_symlink("/proc/self/exe", ec).parent_path();
            if (ec) {
                SPDLOG_ERROR("SubprocessManager::Launch: cannot read /proc/self/exe: {}",
                             ec.message());
                return false;
            }

            const std::string runner_path =
                (exe_dir / "gtrade_strategy_runner").string();

            if (!std::filesystem::exists(runner_path)) {
                SPDLOG_ERROR("SubprocessManager::Launch: runner binary not found: {}",
                             runner_path);
                return false;
            }

            // argv: ["gtrade_strategy_runner", "--strat-config", "<path>", (--restore-checkpoint), nullptr]
            args_storage.push_back(runner_path);
            args_storage.push_back("--strat-config");
            args_storage.push_back(strat_cfg_path);
            if (restore_checkpoint) {
                args_storage.push_back("--restore-checkpoint");
            }
        }

        std::vector<char*> argv;
        for (auto& s : args_storage) {
            argv.push_back(s.data());
        }
        argv.push_back(nullptr);

        // 重置崩溃状态（避免上次崩溃信息残留）
        m_shm->ResetCrashState();

        const pid_t pid = ::fork();
        if (pid < 0) {
            SPDLOG_ERROR("SubprocessManager::Launch: fork failed: {}", strerror(errno));
            return false;
        }

        if (pid == 0) {
            // ── 子进程 ────────────────────────────────────────────────────────
            // 在子进程中执行 runner（替换当前进程映像）
            // Python runner：python_interpreter 可能是相对名称（如 "python3"），
            // 使用 execvp 搜索 PATH；C++ runner 传入绝对路径，execvp 行为与 execv 相同。
            ::execvp(argv[0], argv.data());

            // execv 失败（非常罕见，如二进制损坏）
            // 使用 _exit() 而非 exit() 避免 C++ 全局析构在子进程中运行
            ::_exit(127);
        }

        // ── 父进程 ────────────────────────────────────────────────────────────
        m_child_pid = pid;
        m_running.store(true, std::memory_order_release);
        SPDLOG_INFO("SubprocessManager::Launch: strat={} launched pid={}", m_strat_id, pid);

        // 启动监控线程（心跳检测 + waitpid）
        m_monitor_thread = std::thread(&SubprocessManager::MonitorLoop, this);

        return true;
    }

    /**
     * 停止子进程（发 SIGTERM，等待退出，超时后 SIGKILL）。
     * 同时停止监控线程。
     */
    void Stop() {
        m_running.store(false, std::memory_order_release);

        if (m_child_pid > 0) {
            // 先发 SIGTERM 请求优雅退出
            if (::kill(m_child_pid, SIGTERM) == 0) {
                SPDLOG_INFO("SubprocessManager::Stop: SIGTERM sent to pid={}", m_child_pid);
            }

            // 等待最多 3 秒
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (std::chrono::steady_clock::now() < deadline) {
                int status = 0;
                if (::waitpid(m_child_pid, &status, WNOHANG) == m_child_pid) {
                    SPDLOG_INFO("SubprocessManager::Stop: child pid={} exited", m_child_pid);
                    m_child_pid = 0;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (m_child_pid > 0) {
                // 超时，强制 SIGKILL
                SPDLOG_WARN("SubprocessManager::Stop: SIGKILL sent to pid={}", m_child_pid);
                ::kill(m_child_pid, SIGKILL);
                ::waitpid(m_child_pid, nullptr, 0);
                m_child_pid = 0;
            }
        }

        if (m_monitor_thread.joinable()) {
            m_monitor_thread.join();
        }
    }

    /**
     * 检查子进程是否存活（有效 pid 且未被 waitpid 回收）。
     */
    bool IsAlive() const {
        if (m_child_pid <= 0) {
            return false;
        }
        // kill(pid, 0) 探测进程是否存在，不发送实际信号
        return ::kill(m_child_pid, 0) == 0;
    }

    pid_t GetPid() const { return m_child_pid; }
    int   GetRestartCount() const { return m_restart_count; }

private:
    /**
     * 监控线程主循环：
     *   - 每 1 秒检测一次子进程状态（waitpid + 心跳超时）
     *   - 发现子进程死亡时调用 m_on_crash 回调
     */
    void MonitorLoop() {
        while (m_running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            if (!m_running.load(std::memory_order_acquire)) {
                break;
            }

            // 1. waitpid（非阻塞）检测子进程是否已退出
            if (m_child_pid > 0) {
                int wstatus = 0;
                const pid_t ret = ::waitpid(m_child_pid, &wstatus, WNOHANG);
                if (ret == m_child_pid) {
                    // 子进程已退出
                    if (WIFEXITED(wstatus)) {
                        SPDLOG_ERROR("SubprocessManager: strat={} pid={} exited with code={}",
                                     m_strat_id, m_child_pid, WEXITSTATUS(wstatus));
                    } else if (WIFSIGNALED(wstatus)) {
                        SPDLOG_ERROR("SubprocessManager: strat={} pid={} killed by signal={}",
                                     m_strat_id, m_child_pid, WTERMSIG(wstatus));
                    }
                    m_child_pid = 0;
                    OnChildDied();
                    continue;
                }
            }

            // 2. 心跳超时检测
            if (m_child_pid > 0) {
                const int64_t heartbeat_us =
                    m_shm->GetHeartbeat();
                const int64_t now_us = MyUTC().Epoch19() / 1000LL;  // ns→us

                // heartbeat_us == 0 表示子进程还未写入第一次心跳（刚启动），跳过
                if (heartbeat_us > 0) {
                    const int64_t elapsed_ms = (now_us - heartbeat_us) / 1000LL;
                    if (elapsed_ms > m_config.heartbeat_timeout_ms) {
                        SPDLOG_ERROR(
                            "SubprocessManager: strat={} pid={} heartbeat timeout "
                            "(elapsed={}ms > threshold={}ms), killing",
                            m_strat_id, m_child_pid, elapsed_ms,
                            m_config.heartbeat_timeout_ms);

                        // 强制 SIGKILL（心跳超时表示子进程卡死）
                        ::kill(m_child_pid, SIGKILL);
                        ::waitpid(m_child_pid, nullptr, WNOHANG);
                        m_child_pid = 0;
                        OnChildDied();
                    }
                }
            }
        }
    }

    /**
     * 子进程死亡后的处理：
     *   - 记录崩溃信息
     *   - 调用 on_crash 回调（由 OutProcessProxy 决定是否重启）
     */
    void OnChildDied() {
        // 读取崩溃信息
        const uint32_t crash_sig = m_shm->GetCrashSignal();
        if (crash_sig > 0) {
            SPDLOG_ERROR("SubprocessManager: strat={} crash_signal={} info={}",
                         m_strat_id, crash_sig, m_shm->GetCrashInfo());
        }

        if (m_on_crash) {
            m_on_crash();
        }
    }

    std::string        m_strat_id;
    SubprocessConfig   m_config;
    std::shared_ptr<StrategyShm> m_shm;
    std::function<void()> m_on_crash;

    pid_t              m_child_pid{0};
    std::atomic<bool>  m_running{false};
    std::thread        m_monitor_thread;
    int                m_restart_count{0};   ///< 当前累计重启次数（由 OutProcessProxy 维护）
};
