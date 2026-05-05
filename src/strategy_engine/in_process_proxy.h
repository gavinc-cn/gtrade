//
// 进程内策略代理
//
// InProcessProxy 是 IStrategyProxy 的进程内实现，
// 直接持有 shared_ptr<StrategyBase>，原样转发所有调用。
// 与原有 m_strategy_map 机制行为完全一致，迁移到 Proxy 架构后行为不变。
//

#pragma once

#include <memory>
#include "i_strategy_proxy.h"
#include "strategy_base.h"

/**
 * 进程内策略代理
 *
 * 将 StrategyBase 包装为 IStrategyProxy，所有调用直接转发给被包装的策略实例。
 * 策略运行在主进程共享线程池中，与原有方式完全相同。
 *
 * 使用场景：
 *   - 对延迟极敏感、不需要崩溃隔离的策略（isolation: inprocess）
 *   - 默认模式，保持对已有策略的向后兼容
 */
class InProcessProxy final : public IStrategyProxy {
public:
    /**
     * 构造进程内代理。
     *
     * @param strategy  由 StrategyEngine 创建（dlopen/工厂函数）的策略实例
     * @param strat_id  策略 ID（用于日志和查询，与 strategy 内部 ID 一致）
     */
    explicit InProcessProxy(std::shared_ptr<StrategyBase> strategy,
                            const std::string& strat_id)
        : m_strategy(std::move(strategy))
        , m_strat_id(strat_id)
        , m_strat_info_cache(this->m_strategy->CopyStratInfo())  // 初始快照，在引擎线程构造
    {
    }

    ~InProcessProxy() override = default;

    // ── IStrategyProxy 实现 ─────────────────────────────────────────────────

    /**
     * 初始化策略：调用 StrategyBase::OnInit()。
     */
    bool Init(const YAML::Node& strat_yml) override {
        return m_strategy->OnInit(strat_yml);
    }

    /**
     * 启动策略：通过 StrategyBase::Start() 投递同步启动消息。
     */
    bool Start() override {
        return m_strategy->Start();
    }

    /**
     * 停止策略：通过 StrategyBase::Stop() 投递同步停止消息。
     */
    void Stop() override {
        m_strategy->Stop();
    }

    /**
     * 暂停策略（预留，StrategyBase 尚未实现暂停逻辑，直接返回 true）。
     */
    bool Pause() override {
        // TODO: StrategyBase 暂停接口待实现
        return true;
    }

    /**
     * 恢复策略（预留，返回 true）。
     */
    bool Resume() override {
        return true;
    }

    /**
     * 向策略投递消息：直接调用 PostMsg 投递到策略所在的线程队列。
     * 与原有 SendToStrategy() 调用 iter->second->PostMsg() 的行为完全一致。
     */
    void PostData(const int msg_type, const BufPtr& buffer) override {
        m_strategy->PostMsg(msg_type, buffer);
    }

    const std::string& GetStratId() const override {
        return m_strat_id;
    }

    /**
     * 返回策略元信息的只读引用。
     * 读取引擎线程本地缓存，无跨线程竞争。
     * 缓存由 StrategyEngine::OnDbSetStrategyInfo 在引擎线程上同步更新。
     */
    const StrategyInfo& GetStrategyInfo() const override {
        return m_strat_info_cache;
    }

    /**
     * 更新本地缓存（由 StrategyEngine::OnDbSetStrategyInfo 在引擎线程调用）。
     */
    void UpdateStratInfoCache(const StrategyInfo& info) {
        m_strat_info_cache = info;
    }

    /**
     * 进程内策略始终存活（引擎线程自身若崩溃，整个进程已结束）。
     */
    bool IsAlive() const override {
        return true;
    }

    /**
     * 标记策略正在被删除，防止保存操作。
     */
    void MarkAsDeleting() override {
        m_strategy->MarkAsDeleting();
    }

    /**
     * 获取底层 StrategyBase 裸指针（供 StrategyEngine 直接操作，如 Init/Start 前的配置）。
     * 勿在 Proxy 接口抽象建立后的正常路径调用，仅用于引擎初始化阶段。
     */
    StrategyBase* GetRawStrategy() const {
        return m_strategy.get();
    }

private:
    std::shared_ptr<StrategyBase> m_strategy;
    std::string m_strat_id;
    StrategyInfo m_strat_info_cache {};  // 引擎线程本地副本，由 UpdateStratInfoCache 更新
};
