#include "leader_election.h"
#include <iostream>
#include <sstream>

namespace ha {

LeaderElection::LeaderElection(const HAConfig& config, RedisClient* redis)
    : config_(config), redis_(redis) {}

LeaderElection::~LeaderElection() {
    Stop();
}

void LeaderElection::Start() {
    if (running_.exchange(true)) {
        return;  // 已经在运行
    }

    // 获取当前term
    auto term_str = redis_->Get(config_.state_prefix + "term");
    if (term_str) {
        current_term_.store(std::stoll(*term_str));
    }

    election_thread_ = std::thread(&LeaderElection::ElectionLoop, this);

    std::cout << "[Election] Started for node: " << config_.node_id << std::endl;
}

void LeaderElection::Stop() {
    if (!running_.exchange(false)) {
        return;  // 已经停止
    }

    // 如果是Leader，释放锁
    if (is_leader_.load()) {
        StepDown();
    }

    if (election_thread_.joinable()) {
        election_thread_.join();
    }

    std::cout << "[Election] Stopped for node: " << config_.node_id << std::endl;
}

NodeRole LeaderElection::GetRole() const {
    if (is_leader_.load()) {
        return NodeRole::kLeader;
    }
    return NodeRole::kFollower;
}

std::string LeaderElection::GetCurrentLeader() const {
    auto value = redis_->Get(config_.leader_lock_key);
    if (value) {
        // 格式: node_id:term:timestamp
        auto pos = value->find(':');
        if (pos != std::string::npos) {
            return value->substr(0, pos);
        }
    }
    return "";
}

void LeaderElection::SetLeadershipCallback(LeadershipCallback callback) {
    leadership_callback_ = std::move(callback);
}

void LeaderElection::StepDown() {
    if (!is_leader_.load()) {
        return;
    }

    std::stringstream ss;
    ss << config_.node_id << ":" << current_term_.load() << ":" << NowMs();
    std::string lock_value = ss.str();

    redis_->ReleaseLock(config_.leader_lock_key, lock_value);

    is_leader_.store(false);
    std::cout << "[Election] Node " << config_.node_id << " stepped down" << std::endl;

    if (leadership_callback_) {
        leadership_callback_(false, current_term_.load());
    }
}

std::optional<LeaderElection::LeaderInfo> LeaderElection::GetLeaderInfo() const {
    auto value = redis_->Get(config_.leader_lock_key);
    if (!value) {
        return std::nullopt;
    }

    // 格式: node_id:term:timestamp
    std::istringstream ss(*value);
    std::string node_id;
    int64_t term, timestamp;

    std::getline(ss, node_id, ':');
    ss >> term;
    ss.ignore(1);
    ss >> timestamp;

    return LeaderInfo{node_id, term, timestamp};
}

void LeaderElection::ElectionLoop() {
    while (running_.load()) {
        if (is_leader_.load()) {
            // 作为Leader，定期续租
            if (!RenewLeadership()) {
                // 续租失败，可能网络分区或Redis故障
                std::cout << "[Election] Leader " << config_.node_id
                          << " failed to renew lease, stepping down" << std::endl;
                OnLoseLeadership();
            }
        } else {
            // 作为Follower，尝试竞选
            if (TryBecomeLeader()) {
                OnBecomeLeader();
            }
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.heartbeat_interval_ms));
    }
}

bool LeaderElection::TryBecomeLeader() {
    // 递增term
    int64_t new_term = IncrementTerm();

    // 构造锁值: node_id:term:timestamp
    std::stringstream ss;
    ss << config_.node_id << ":" << new_term << ":" << NowMs();
    std::string lock_value = ss.str();

    // 尝试获取锁
    bool acquired = redis_->TryLock(
        config_.leader_lock_key,
        lock_value,
        config_.lease_ttl_ms
    );

    if (acquired) {
        current_term_.store(new_term);
        std::cout << "[Election] Node " << config_.node_id
                  << " acquired leadership at term " << new_term << std::endl;
    }

    return acquired;
}

bool LeaderElection::RenewLeadership() {
    std::stringstream ss;
    ss << config_.node_id << ":" << current_term_.load() << ":" << NowMs();
    std::string lock_value = ss.str();

    // 先读取当前锁值，确认还是自己持有
    auto current_value = redis_->Get(config_.leader_lock_key);
    if (!current_value) {
        return false;
    }

    // 检查node_id和term是否匹配
    std::istringstream iss(*current_value);
    std::string node_id;
    int64_t term;
    std::getline(iss, node_id, ':');
    iss >> term;

    if (node_id != config_.node_id || term != current_term_.load()) {
        // 锁已被其他节点获取
        return false;
    }

    // 续租（更新过期时间）
    return redis_->RenewLock(
        config_.leader_lock_key,
        *current_value,  // 使用原值续租
        config_.lease_ttl_ms
    );
}

void LeaderElection::OnBecomeLeader() {
    is_leader_.store(true);

    std::cout << "[Election] Node " << config_.node_id
              << " became LEADER at term " << current_term_.load() << std::endl;

    if (leadership_callback_) {
        leadership_callback_(true, current_term_.load());
    }
}

void LeaderElection::OnLoseLeadership() {
    is_leader_.store(false);

    std::cout << "[Election] Node " << config_.node_id
              << " lost leadership at term " << current_term_.load() << std::endl;

    if (leadership_callback_) {
        leadership_callback_(false, current_term_.load());
    }
}

int64_t LeaderElection::IncrementTerm() {
    std::string term_key = config_.state_prefix + "term";
    return redis_->Incr(term_key);
}

}  // namespace ha
