//
// Created 2026-05-05
//

#include "strategy_factory.h"

// ── 静态注册表 ──────────────────────────────────────────────────────────────
// 使用 Construct On First Use 惯用法：将 map 放在函数内静态变量中，
// 确保在任何翻译单元的静态初始化块调用 Register() 时，map 已被构造。
std::unordered_map<std::string, StrategyFactory::Creator>& StrategyFactory::GetRegistry() {
    static std::unordered_map<std::string, Creator> s_registry;
    return s_registry;
}

bool StrategyFactory::Register(const std::string& template_id, Creator creator) {
    GetRegistry().emplace(template_id, std::move(creator));
    return true;
}

std::shared_ptr<StrategyBase> StrategyFactory::Create(
    const std::string& template_id,
    const GTradeConfig& cfg,
    MyHandler* engine,
    const std::string& strat_id)
{
    const auto& registry = GetRegistry();
    const auto it = registry.find(template_id);
    if (it == registry.end()) {
        return nullptr;
    }
    return it->second(cfg, engine, strat_id);
}

bool StrategyFactory::Contains(const std::string& template_id) {
    return GetRegistry().count(template_id) > 0;
}
