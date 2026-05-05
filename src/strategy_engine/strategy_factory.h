//
// 策略工厂：维护 strat_template_id → 构造函数 的映射表
// 各策略在自己的 .cpp 文件末尾通过静态初始化块调用 Register() 完成注册，
// 引擎只需调用 Create() 即可，无需 include 每个策略头文件。
//

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include "type_define.h"

// 前向声明，避免循环依赖
class StrategyBase;
struct GTradeConfig;

class StrategyFactory {
public:
    using Creator = std::function<std::shared_ptr<StrategyBase>(
        const GTradeConfig&, MyHandler*, const std::string&)>;

    /**
     * 注册策略构造器
     *
     * @param template_id  策略模板 ID（对应 YAML 中的 strat_template_id）
     * @param creator      工厂函数，负责 make_shared 并返回实例
     * @return             始终返回 true，方便用作静态变量初始化表达式
     */
    static bool Register(const std::string& template_id, Creator creator);

    /**
     * 根据模板 ID 创建策略实例
     *
     * @param template_id  策略模板 ID
     * @param cfg          全局配置
     * @param engine       策略引擎指针（作为父 handler）
     * @param strat_id     策略实例 ID
     * @return             成功返回实例，未找到返回 nullptr
     */
    static std::shared_ptr<StrategyBase> Create(
        const std::string& template_id,
        const GTradeConfig& cfg,
        MyHandler* engine,
        const std::string& strat_id);

    /**
     * 查询某个模板 ID 是否已注册
     */
    static bool Contains(const std::string& template_id);

private:
    // 使用函数内静态变量保证初始化顺序安全（Construct On First Use）
    static std::unordered_map<std::string, Creator>& GetRegistry();
};
