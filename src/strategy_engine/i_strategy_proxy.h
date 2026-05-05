//
// 策略代理接口
//
// IStrategyProxy 是 StrategyEngine 与策略实例之间的抽象层。
// 无论策略运行在进程内（InProcessProxy）还是子进程（OutProcessProxy），
// StrategyEngine 都通过统一接口进行通信，调用方无感知。
//
// 生命周期由 StrategyEngine 管理，通过 unique_ptr 持有。
//

#pragma once

#include <yaml-cpp/yaml.h>
#include <string>
#include "type_define.h"
#include "i_strategy_engine.h"

/**
 * 策略代理抽象接口
 *
 * 封装单个策略实例的通信与生命周期。
 * StrategyEngine 持有 unique_ptr<IStrategyProxy>，通过此接口与策略交互。
 */
class IStrategyProxy {
public:
    virtual ~IStrategyProxy() = default;

    // ── 生命周期 ─────────────────────────────────────────────────────────────

    /**
     * 初始化策略（加载配置，注册消息处理器）。
     * 对应策略的 OnInit()，在加载时调用一次。
     *
     * @param strat_yml  策略 YAML 配置节点
     * @return true 初始化成功
     */
    virtual bool Init(const YAML::Node& strat_yml) = 0;

    /**
     * 启动策略（进入运行状态）。
     * 对应策略的 OnStart()。
     *
     * @return true 启动成功
     */
    virtual bool Start() = 0;

    /**
     * 停止策略（进入停止状态）。
     * 对应策略的 OnStop()。
     */
    virtual void Stop() = 0;

    /**
     * 暂停策略（保持状态但暂停处理行情）。
     * 对应策略的 OnPause()。
     *
     * @return true 暂停成功
     */
    virtual bool Pause() = 0;

    /**
     * 恢复策略（从暂停状态恢复）。
     * 对应策略的 OnResume()。
     *
     * @return true 恢复成功
     */
    virtual bool Resume() = 0;

    // ── 消息投递 ──────────────────────────────────────────────────────────────

    /**
     * 向策略投递数据消息（行情 tick、K线、委托回报、查询结果、定时器事件等）。
     *
     * 对于进程内策略：直接调用 PostMsg 投递到策略线程队列。
     * 对于子进程策略：序列化后写入 Channel A 共享内存 ring buffer。
     *
     * @param msg_type  消息类型（MsgId 枚举值）
     * @param buffer    消息 payload（TBuffer 中的原始字节）
     */
    virtual void PostData(const int msg_type, const BufPtr& buffer) = 0;

    // ── 查询 ─────────────────────────────────────────────────────────────────

    /**
     * 获取策略 ID。
     */
    virtual const std::string& GetStratId() const = 0;

    /**
     * 获取策略元信息（状态、参数、指标等）的只读引用。
     * 注意：子进程代理的 StrategyInfo 由心跳或消息同步更新，可能略有延迟。
     */
    virtual const StrategyInfo& GetStrategyInfo() const = 0;

    /**
     * 检查策略是否存活。
     *
     * 进程内策略始终返回 true（StrategyEngine 自身崩溃则系统已挂）。
     * 子进程策略通过心跳超时或 waitpid 检测存活状态。
     */
    virtual bool IsAlive() const = 0;

    /**
     * 标记策略正在被删除（防止保存操作）。
     * 对应 StrategyBase::MarkAsDeleting()。
     */
    virtual void MarkAsDeleting() = 0;
};
