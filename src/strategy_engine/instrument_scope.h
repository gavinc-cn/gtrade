#pragma once

#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "zrtools/zrt_misc.h"   // zrt::TupleHasher

// 标的范围/订阅所有者的纯逻辑助手（无引擎依赖，便于单测）。
// 订阅 key = (channel, market, inst_id)；范围条目 = (market, inst_id, inst_type)。
// 两者相差一维 inst_type：行情订阅报文只带 instId（现货与合约各订阅各的），但"标的范围"
// 是用户视角的三元组 —— instType 是标的唯一性的一部分（币币杠杆 MARGIN 复用现货的 instId，
// instIdCode 与全部规格字段都相同），二元范围无法区分。多条范围条目可以映射到同一条订阅，
// 引用计数归并见 `HasOtherInstType`。
// 非线程安全：QuoteOwnerMap 为无锁容器，仅限引擎线程访问；
// 其它线程（如 HttpGateway 的 HTTP 线程）须先 Post 消息到引擎线程，再在引擎线程内调用。
using QuoteSubKey = std::tuple<std::string, std::string, std::string>;
using QuoteOwnerMap = std::unordered_map<QuoteSubKey, std::unordered_set<std::string>, zrt::TupleHasher>;
using ScopeKey = std::tuple<std::string, std::string, std::string>;
using ScopeSet = std::set<ScopeKey>;

// 范围变更差集
struct ScopeChange {
    std::vector<ScopeKey> added {};
    std::vector<ScopeKey> removed {};
};

// 计算 old_scope → wanted 的差集
ScopeChange DiffScope(const ScopeSet& old_scope, const ScopeSet& wanted);

// key 上添加 owner；返回 true 表示此前无主（调用方需要下发订阅）
bool AddOwner(QuoteOwnerMap& owners, const QuoteSubKey& key, const std::string& owner);

// key 上移除 owner；返回 true 表示移除后集合变空（调用方需要下发退订），
// 集合变空时本函数删除该条目；key 不存在返回 false
bool RemoveOwner(QuoteOwnerMap& owners, const QuoteSubKey& key, const std::string& owner);

// 移除某 owner 的全部订阅；返回"由有主变无主"的 key 列表（条目同时被删除）
std::vector<QuoteSubKey> RemoveAllOwnedBy(QuoteOwnerMap& owners, const std::string& owner);

// scope 中是否还有同 (market, inst_id) 的**其它**条目（即别的 inst_type）。
// 用于退订前的引用计数归并：行情订阅键不含 inst_type，同 inst_id 的多个范围条目共享同一条
// 订阅（k_scope_owner 在 QuoteOwnerMap 里只算一次），若不归并，移除任一条就会把还在用的
// 另一条一起退订（"退 MARGIN 连坐退掉 SPOT"）。
// 约定：调用方**先**把 key 从 scope 里 erase 掉，再调用本函数。
bool HasOtherInstType(const ScopeSet& scope, const ScopeKey& key);
