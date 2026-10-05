#include "instrument_scope.h"

ScopeChange DiffScope(const ScopeSet& old_scope, const ScopeSet& wanted) {
    ScopeChange change {};
    for (const auto& key : wanted) {
        if (!old_scope.count(key)) { change.added.push_back(key); }
    }
    for (const auto& key : old_scope) {
        if (!wanted.count(key)) { change.removed.push_back(key); }
    }
    return change;
}

bool AddOwner(QuoteOwnerMap& owners, const QuoteSubKey& key, const std::string& owner) {
    auto& owner_set = owners[key];
    const bool was_empty = owner_set.empty();
    owner_set.insert(owner);
    return was_empty;
}

bool RemoveOwner(QuoteOwnerMap& owners, const QuoteSubKey& key, const std::string& owner) {
    const auto iter = owners.find(key);
    if (iter == owners.end()) { return false; }
    iter->second.erase(owner);
    if (!iter->second.empty()) { return false; }
    owners.erase(iter);
    return true;
}

std::vector<QuoteSubKey> RemoveAllOwnedBy(QuoteOwnerMap& owners, const std::string& owner) {
    std::vector<QuoteSubKey> emptied {};
    for (auto iter = owners.begin(); iter != owners.end();) {
        iter->second.erase(owner);
        if (iter->second.empty()) {
            emptied.push_back(iter->first);
            iter = owners.erase(iter);
        } else {
            ++iter;
        }
    }
    return emptied;
}

// 只比前两维：范围集合已按 (market, inst_id, inst_type) 排序，但同 inst_id 的条目只差第三维，
// 顺序上不一定相邻（中间可能插着别的 inst_id），故直接线性扫 —— scope 上限 128，代价可忽略。
bool HasOtherInstType(const ScopeSet& scope, const ScopeKey& key) {
    const std::string& market = std::get<0>(key);
    const std::string& inst_id = std::get<1>(key);
    for (const auto& other : scope) {
        if (std::get<0>(other) == market && std::get<1>(other) == inst_id) { return true; }
    }
    return false;
}
