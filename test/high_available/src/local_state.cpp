#include "local_state.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace ha {

LocalState::LocalState(const std::string& node_id, const std::string& data_dir)
    : node_id_(node_id), data_dir_(data_dir) {
    EnsureDataDir();
    persist_file_ = data_dir_ + "/state_" + node_id_ + ".dat";
}

LocalState::~LocalState() {
    PersistToFile();
}

void LocalState::EnsureDataDir() {
    struct stat st;
    if (stat(data_dir_.c_str(), &st) != 0) {
        mkdir(data_dir_.c_str(), 0755);
    }
}

bool LocalState::SaveOrder(const Order& order) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        orders_[order.order_id] = order;
    }

    // 立即持久化到文件
    return PersistToFile();
}

std::optional<Order> LocalState::GetOrder(const std::string& order_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = orders_.find(order_id);
    if (it != orders_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::unordered_map<std::string, Order> LocalState::GetAllOrders() {
    std::lock_guard<std::mutex> lock(mutex_);
    return orders_;
}

bool LocalState::DeleteOrder(const std::string& order_id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        orders_.erase(order_id);
    }
    return PersistToFile();
}

bool LocalState::UpdateOrderStatus(const std::string& order_id, OrderStatus status) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = orders_.find(order_id);
        if (it == orders_.end()) {
            return false;
        }
        it->second.status = status;
        it->second.update_time_ms = NowMs();
    }
    return PersistToFile();
}

bool LocalState::PersistToFile() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ofstream ofs(persist_file_, std::ios::binary);
    if (!ofs) {
        std::cerr << "[LocalState] Failed to open file for writing: " << persist_file_ << std::endl;
        return false;
    }

    // 写入元数据
    int64_t term = last_term_.load();
    int64_t msg_id = last_msg_id_.load();
    size_t order_count = orders_.size();

    ofs.write(reinterpret_cast<const char*>(&term), sizeof(term));
    ofs.write(reinterpret_cast<const char*>(&msg_id), sizeof(msg_id));
    ofs.write(reinterpret_cast<const char*>(&order_count), sizeof(order_count));

    // 写入订单数据
    for (const auto& [id, order] : orders_) {
        std::string serialized = order.Serialize();
        size_t len = serialized.size();
        ofs.write(reinterpret_cast<const char*>(&len), sizeof(len));
        ofs.write(serialized.data(), len);
    }

    last_persist_time_ms_.store(NowMs());

    return true;
}

bool LocalState::RecoverFromFile() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ifstream ifs(persist_file_, std::ios::binary);
    if (!ifs) {
        std::cout << "[LocalState] No local state file found: " << persist_file_ << std::endl;
        return false;
    }

    // 读取元数据
    int64_t term, msg_id;
    size_t order_count;

    ifs.read(reinterpret_cast<char*>(&term), sizeof(term));
    ifs.read(reinterpret_cast<char*>(&msg_id), sizeof(msg_id));
    ifs.read(reinterpret_cast<char*>(&order_count), sizeof(order_count));

    if (!ifs) {
        std::cerr << "[LocalState] Failed to read metadata from file" << std::endl;
        return false;
    }

    last_term_.store(term);
    last_msg_id_.store(msg_id);

    // 读取订单数据
    orders_.clear();
    for (size_t i = 0; i < order_count; ++i) {
        size_t len;
        ifs.read(reinterpret_cast<char*>(&len), sizeof(len));

        std::string serialized(len, '\0');
        ifs.read(&serialized[0], len);

        if (!ifs) {
            std::cerr << "[LocalState] Failed to read order " << i << std::endl;
            break;
        }

        Order order = Order::Deserialize(serialized);
        orders_[order.order_id] = order;
    }

    std::cout << "[LocalState] Recovered " << orders_.size() << " orders from local file"
              << ", term=" << term << ", last_msg_id=" << msg_id << std::endl;

    return true;
}

bool LocalState::IsLocalFileAvailable() const {
    struct stat st;
    return stat(persist_file_.c_str(), &st) == 0;
}

std::string LocalState::GetPersistFilePath() const {
    return persist_file_;
}

void LocalState::SetLastTerm(int64_t term) {
    last_term_.store(term);
}

int64_t LocalState::GetLastTerm() const {
    return last_term_.load();
}

void LocalState::SetLastMsgId(int64_t msg_id) {
    last_msg_id_.store(msg_id);
}

int64_t LocalState::GetLastMsgId() const {
    return last_msg_id_.load();
}

LocalState::LocalStats LocalState::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    LocalStats stats;
    stats.order_count = orders_.size();
    stats.last_term = last_term_.load();
    stats.last_msg_id = last_msg_id_.load();
    stats.last_persist_time_ms = last_persist_time_ms_.load();

    return stats;
}

void LocalState::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    orders_.clear();
    last_term_.store(0);
    last_msg_id_.store(0);
    PersistToFile();
}

}  // namespace ha
