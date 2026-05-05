/**
 * 状态查看器
 *
 * 功能：
 * 1. 查看Redis中的共享状态
 * 2. 查看Leader锁状态
 * 3. 查看消息队列状态
 * 4. 监控主备切换
 *
 * 使用方式：
 *   ./state_viewer [redis_host] [redis_port]
 */

#include "ha_types.h"
#include "redis_client.h"
#include <iostream>
#include <string>
#include <iomanip>
#include <thread>
#include <chrono>

using namespace ha;

void PrintLeaderInfo(RedisClient& redis, const HAConfig& config) {
    auto value = redis.Get(config.leader_lock_key);

    std::cout << "\n=== Leader Status ===" << std::endl;
    if (value) {
        // 格式: node_id:term:timestamp
        std::istringstream ss(*value);
        std::string node_id;
        int64_t term, timestamp;

        std::getline(ss, node_id, ':');
        ss >> term;
        ss.ignore(1);
        ss >> timestamp;

        int64_t age_ms = NowMs() - timestamp;

        std::cout << "Current Leader: " << node_id << std::endl;
        std::cout << "Term:           " << term << std::endl;
        std::cout << "Lock Age:       " << age_ms << " ms" << std::endl;
        std::cout << "Lock Value:     " << *value << std::endl;
    } else {
        std::cout << "No leader (lock not held)" << std::endl;
    }
    std::cout << "=====================\n" << std::endl;
}

void PrintQueueStatus(RedisClient& redis, const HAConfig& config) {
    int64_t queue_len = redis.LLen(config.message_queue_key);

    std::string pending_key = config.message_queue_key + ":pending";
    auto pending = redis.HGetAll(pending_key);

    std::cout << "\n=== Message Queue ===" << std::endl;
    std::cout << "Queue Length:     " << queue_len << std::endl;
    std::cout << "Pending Messages: " << pending.size() << std::endl;

    if (!pending.empty()) {
        std::cout << "\nPending Details:" << std::endl;
        for (const auto& [id, data] : pending) {
            Message msg = Message::Deserialize(data);
            int64_t age_ms = NowMs() - msg.timestamp_ms;
            std::cout << "  msg_id=" << msg.msg_id
                      << ", type=" << static_cast<int>(msg.type)
                      << ", age=" << age_ms << "ms" << std::endl;
        }
    }

    // 显示队列中的消息（前5条）
    if (queue_len > 0) {
        std::cout << "\nQueue Preview (first 5):" << std::endl;
        auto items = redis.LRange(config.message_queue_key, 0, 4);
        for (const auto& item : items) {
            Message msg = Message::Deserialize(item);
            std::cout << "  msg_id=" << msg.msg_id
                      << ", type=" << static_cast<int>(msg.type) << std::endl;
        }
    }

    std::cout << "=====================\n" << std::endl;
}

void PrintTermInfo(RedisClient& redis, const HAConfig& config) {
    auto term_str = redis.Get(config.state_prefix + "term");
    int64_t term = term_str ? std::stoll(*term_str) : 0;

    auto counter_str = redis.Get(config.state_prefix + "msg_counter");
    int64_t msg_counter = counter_str ? std::stoll(*counter_str) : 0;

    std::cout << "\n=== Global State ===" << std::endl;
    std::cout << "Current Term:    " << term << std::endl;
    std::cout << "Message Counter: " << msg_counter << std::endl;
    std::cout << "====================\n" << std::endl;
}

void MonitorLoop(RedisClient& redis, const HAConfig& config) {
    std::string last_leader;
    int64_t last_term = 0;

    std::cout << "\nMonitoring... (Ctrl+C to stop)\n" << std::endl;

    while (true) {
        auto value = redis.Get(config.leader_lock_key);

        std::string current_leader;
        int64_t current_term = 0;

        if (value) {
            std::istringstream ss(*value);
            std::getline(ss, current_leader, ':');
            ss >> current_term;
        }

        // 检测变化
        if (current_leader != last_leader || current_term != last_term) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);

            std::cout << "[" << std::put_time(std::localtime(&time), "%H:%M:%S") << "] ";

            if (current_leader.empty()) {
                std::cout << "*** LEADER LOST *** (was: " << last_leader << ")" << std::endl;
            } else if (last_leader.empty()) {
                std::cout << "*** NEW LEADER: " << current_leader
                          << " (term=" << current_term << ") ***" << std::endl;
            } else if (current_leader != last_leader) {
                std::cout << "*** LEADER CHANGE: " << last_leader
                          << " -> " << current_leader
                          << " (term=" << current_term << ") ***" << std::endl;
            } else {
                std::cout << "Term changed: " << last_term << " -> " << current_term << std::endl;
            }

            last_leader = current_leader;
            last_term = current_term;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void PrintHelp() {
    std::cout << "\nCommands:\n"
              << "  leader   - Show leader info\n"
              << "  queue    - Show queue status\n"
              << "  term     - Show term/counter info\n"
              << "  all      - Show all info\n"
              << "  monitor  - Monitor leader changes\n"
              << "  clear    - Clear all HA data\n"
              << "  quit     - Exit\n"
              << "  help     - Show this help\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    std::string redis_host = (argc > 1) ? argv[1] : "127.0.0.1";
    int redis_port = (argc > 2) ? std::stoi(argv[2]) : 6379;

    std::cout << "========================================" << std::endl;
    std::cout << "     HA State Viewer                   " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Redis: " << redis_host << ":" << redis_port << std::endl;
    std::cout << "========================================" << std::endl;

    // 配置
    HAConfig config;
    config.redis_host = redis_host;
    config.redis_port = redis_port;

    // 连接Redis
    RedisClient redis(redis_host, redis_port);
    if (!redis.Connect()) {
        std::cerr << "Failed to connect to Redis" << std::endl;
        return 1;
    }
    std::cout << "Connected to Redis" << std::endl;

    PrintHelp();

    std::string line;
    while (true) {
        std::cout << "[viewer] > ";
        std::cout.flush();

        if (!std::getline(std::cin, line)) {
            break;
        }

        if (line.empty()) {
            continue;
        }

        if (line == "leader" || line == "l") {
            PrintLeaderInfo(redis, config);
        }
        else if (line == "queue" || line == "q") {
            PrintQueueStatus(redis, config);
        }
        else if (line == "term" || line == "t") {
            PrintTermInfo(redis, config);
        }
        else if (line == "all" || line == "a") {
            PrintLeaderInfo(redis, config);
            PrintQueueStatus(redis, config);
            PrintTermInfo(redis, config);
        }
        else if (line == "monitor" || line == "m") {
            MonitorLoop(redis, config);
        }
        else if (line == "clear") {
            std::cout << "Clearing all HA data..." << std::endl;

            // 清除Leader锁
            redis.Del(config.leader_lock_key);

            // 清除消息队列
            redis.Del(config.message_queue_key);
            redis.Del(config.message_queue_key + ":pending");

            // 清除term和counter
            redis.Del(config.state_prefix + "term");
            redis.Del(config.state_prefix + "msg_counter");

            // 清除processed set
            redis.Del(config.processed_set_key);

            std::cout << "Done" << std::endl;
        }
        else if (line == "quit" || line == "exit") {
            break;
        }
        else if (line == "help" || line == "h") {
            PrintHelp();
        }
        else {
            std::cout << "Unknown command: " << line << std::endl;
            PrintHelp();
        }
    }

    std::cout << "Bye!" << std::endl;
    return 0;
}
