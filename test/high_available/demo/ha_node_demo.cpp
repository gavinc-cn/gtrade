/**
 * HA节点Demo
 *
 * 功能：
 * 1. 启动一个HA节点（主或备）
 * 2. 自动参与Leader选举
 * 3. 作为Leader时处理消息
 * 4. 支持模拟崩溃和恢复
 *
 * 使用方式：
 *   ./ha_node_demo <node_id> [redis_host] [redis_port]
 *
 * 示例：
 *   # 终端1：启动节点A
 *   ./ha_node_demo node_a
 *
 *   # 终端2：启动节点B
 *   ./ha_node_demo node_b
 *
 *   # 终端3：发送订单
 *   ./upstream_sim
 *
 * 测试场景：
 *   1. 正常运行：两个节点，一个成为Leader
 *   2. 程序崩溃：Ctrl+C终止Leader，观察备节点接管
 *   3. 模拟宕机：输入'crash'模拟崩溃（不持久化）
 *   4. 优雅关闭：输入'quit'优雅退出
 */

#include "ha_node.h"
#include <iostream>
#include <string>
#include <csignal>
#include <atomic>

using namespace ha;

// 全局节点指针，用于信号处理
static std::atomic<HANode*> g_node{nullptr};
static std::atomic<bool> g_running{true};

void SignalHandler(int signum) {
    std::cout << "\n[Signal] Received signal " << signum << std::endl;
    g_running.store(false);

    HANode* node = g_node.load();
    if (node) {
        node->Stop();
    }
}

void PrintHelp() {
    std::cout << "\nCommands:\n"
              << "  status  - Show node status\n"
              << "  orders  - List all orders\n"
              << "  crash   - Simulate crash (no cleanup)\n"
              << "  stepdown- Step down from leadership\n"
              << "  quit    - Graceful shutdown\n"
              << "  help    - Show this help\n"
              << std::endl;
}

void PrintStatus(HANode& node) {
    auto stats = node.GetStats();

    std::cout << "\n=== Node Status ===" << std::endl;
    std::cout << "Node ID:     " << stats.node_id << std::endl;
    std::cout << "Role:        " << RoleToString(stats.role) << std::endl;
    std::cout << "Term:        " << stats.current_term << std::endl;
    std::cout << "Is Healthy:  " << (stats.is_healthy ? "Yes" : "No") << std::endl;
    std::cout << "Uptime:      " << stats.uptime_ms / 1000 << "s" << std::endl;
    std::cout << "\n--- Orders ---" << std::endl;
    std::cout << "Total:       " << stats.total_orders << std::endl;
    std::cout << "Pending:     " << stats.pending_orders << std::endl;
    std::cout << "Completed:   " << stats.completed_orders << std::endl;
    std::cout << "\n--- Messages ---" << std::endl;
    std::cout << "Queue Len:   " << stats.queue_length << std::endl;
    std::cout << "Pending:     " << stats.pending_messages << std::endl;
    std::cout << "Processed:   " << stats.processed_messages << std::endl;
    std::cout << "==================\n" << std::endl;
}

void PrintOrders(HANode& node) {
    auto orders = node.GetAllOrders();

    std::cout << "\n=== Orders (" << orders.size() << ") ===" << std::endl;
    for (const auto& order : orders) {
        std::cout << "  " << order.order_id
                  << " | " << order.symbol
                  << " | " << order.price
                  << " | " << order.quantity
                  << " | " << OrderStatusToString(order.status)
                  << " | term=" << order.processed_by_term
                  << std::endl;
    }
    std::cout << "======================\n" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <node_id> [redis_host] [redis_port]" << std::endl;
        std::cout << "Example: " << argv[0] << " node_a localhost 6379" << std::endl;
        return 1;
    }

    // 解析参数
    std::string node_id = argv[1];
    std::string redis_host = (argc > 2) ? argv[2] : "127.0.0.1";
    int redis_port = (argc > 3) ? std::stoi(argv[3]) : 6379;

    // 配置
    HAConfig config;
    config.node_id = node_id;
    config.redis_host = redis_host;
    config.redis_port = redis_port;
    config.heartbeat_interval_ms = 100;
    config.election_timeout_ms = 500;
    config.lease_ttl_ms = 3000;

    std::cout << "========================================" << std::endl;
    std::cout << "     High Availability Demo Node       " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Node ID:     " << node_id << std::endl;
    std::cout << "Redis:       " << redis_host << ":" << redis_port << std::endl;
    std::cout << "========================================" << std::endl;

    // 设置信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // 创建节点
    HANode node(config);
    g_node.store(&node);

    // 设置回调
    node.SetRoleChangeCallback([](NodeRole old_role, NodeRole new_role) {
        std::cout << "\n*** ROLE CHANGED: " << RoleToString(old_role)
                  << " -> " << RoleToString(new_role) << " ***\n" << std::endl;
    });

    node.SetOrderCallback([](const Order& order) {
        std::cout << "[Callback] Order processed: " << order.order_id
                  << " -> " << OrderStatusToString(order.status) << std::endl;
    });

    // 启动节点
    if (!node.Start()) {
        std::cerr << "Failed to start node" << std::endl;
        return 1;
    }

    PrintHelp();

    // 主循环：处理用户输入
    std::string input;
    while (g_running.load()) {
        std::cout << "[" << node_id << " " << RoleToString(node.GetRole()) << "] > ";
        std::cout.flush();

        if (!std::getline(std::cin, input)) {
            break;
        }

        if (input.empty()) {
            continue;
        }

        if (input == "status" || input == "s") {
            PrintStatus(node);
        }
        else if (input == "orders" || input == "o") {
            PrintOrders(node);
        }
        else if (input == "crash") {
            std::cout << "\n!!! SIMULATING CRASH !!!" << std::endl;
            std::cout << "Exiting without cleanup..." << std::endl;
            // 直接退出，不调用Stop()
            g_node.store(nullptr);
            std::exit(1);
        }
        else if (input == "stepdown") {
            if (node.IsLeader()) {
                std::cout << "Stepping down from leadership..." << std::endl;
                node.GetLeaderElection()->StepDown();
            } else {
                std::cout << "Not a leader" << std::endl;
            }
        }
        else if (input == "quit" || input == "q") {
            std::cout << "Graceful shutdown..." << std::endl;
            break;
        }
        else if (input == "help" || input == "h") {
            PrintHelp();
        }
        else {
            std::cout << "Unknown command: " << input << std::endl;
            PrintHelp();
        }
    }

    // 优雅关闭
    g_node.store(nullptr);
    node.Stop();

    std::cout << "Bye!" << std::endl;
    return 0;
}
