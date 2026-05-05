/**
 * 上游模拟器
 *
 * 功能：
 * 1. 模拟上游系统发送订单请求
 * 2. 直接写入Redis消息队列
 * 3. 可以持续发送或批量发送
 *
 * 使用方式：
 *   ./upstream_sim [redis_host] [redis_port]
 *
 * 命令：
 *   send [count]  - 发送订单（默认1个）
 *   batch <count> - 批量发送订单
 *   queue         - 查看队列状态
 *   quit          - 退出
 */

#include "ha_types.h"
#include "redis_client.h"
#include "message_broker.h"
#include <iostream>
#include <string>
#include <random>
#include <iomanip>

using namespace ha;

// 随机数生成器
static std::random_device rd;
static std::mt19937 gen(rd());

std::string GenerateOrderId() {
    std::uniform_int_distribution<> dis(10000, 99999);
    return "ORD-" + std::to_string(NowMs()) + "-" + std::to_string(dis(gen));
}

std::string RandomSymbol() {
    static const char* symbols[] = {
        "BTC-USDT", "ETH-USDT", "SOL-USDT", "DOGE-USDT", "XRP-USDT"
    };
    std::uniform_int_distribution<> dis(0, 4);
    return symbols[dis(gen)];
}

double RandomPrice(const std::string& symbol) {
    std::uniform_real_distribution<> dis(0.95, 1.05);
    double base = 1000.0;

    if (symbol == "BTC-USDT") base = 50000.0;
    else if (symbol == "ETH-USDT") base = 3000.0;
    else if (symbol == "SOL-USDT") base = 100.0;
    else if (symbol == "DOGE-USDT") base = 0.1;
    else if (symbol == "XRP-USDT") base = 0.5;

    return base * dis(gen);
}

double RandomQuantity() {
    std::uniform_real_distribution<> dis(0.1, 10.0);
    return dis(gen);
}

Order GenerateOrder() {
    Order order;
    order.order_id = GenerateOrderId();
    order.symbol = RandomSymbol();
    order.price = RandomPrice(order.symbol);
    order.quantity = RandomQuantity();
    order.status = OrderStatus::kPending;
    order.create_time_ms = NowMs();
    order.update_time_ms = order.create_time_ms;
    order.processed_by_term = 0;
    return order;
}

void PrintOrder(const Order& order) {
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "  " << order.order_id
              << " | " << std::setw(10) << order.symbol
              << " | " << std::setw(12) << order.price
              << " | " << std::setw(8) << order.quantity
              << std::endl;
}

void PrintHelp() {
    std::cout << "\nCommands:\n"
              << "  send [count] - Send order(s) (default: 1)\n"
              << "  batch <n>    - Send n orders with delay\n"
              << "  queue        - Show queue status\n"
              << "  clear        - Clear message queue\n"
              << "  quit         - Exit\n"
              << "  help         - Show this help\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    std::string redis_host = (argc > 1) ? argv[1] : "127.0.0.1";
    int redis_port = (argc > 2) ? std::stoi(argv[2]) : 6379;

    std::cout << "========================================" << std::endl;
    std::cout << "     Upstream Simulator                " << std::endl;
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

    // 创建消息代理
    MessageBroker broker(config, &redis);

    PrintHelp();

    std::string line;
    while (true) {
        std::cout << "[upstream] > ";
        std::cout.flush();

        if (!std::getline(std::cin, line)) {
            break;
        }

        if (line.empty()) {
            continue;
        }

        // 解析命令
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "send" || cmd == "s") {
            int count = 1;
            iss >> count;
            if (count <= 0) count = 1;

            std::cout << "\nSending " << count << " order(s)..." << std::endl;

            for (int i = 0; i < count; ++i) {
                Order order = GenerateOrder();
                int64_t msg_id = broker.PushOrderRequest(order);

                if (msg_id > 0) {
                    std::cout << "Sent msg_id=" << msg_id << ":" << std::endl;
                    PrintOrder(order);
                } else {
                    std::cerr << "Failed to send order" << std::endl;
                }
            }

            std::cout << "Queue length: " << broker.GetQueueLength() << std::endl;
        }
        else if (cmd == "batch" || cmd == "b") {
            int count = 10;
            iss >> count;
            if (count <= 0) count = 10;

            std::cout << "\nBatch sending " << count << " orders..." << std::endl;

            for (int i = 0; i < count; ++i) {
                Order order = GenerateOrder();
                int64_t msg_id = broker.PushOrderRequest(order);

                std::cout << "  [" << (i + 1) << "/" << count << "] "
                          << "msg_id=" << msg_id << " "
                          << order.order_id << std::endl;

                // 短暂延迟
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            std::cout << "Done. Queue length: " << broker.GetQueueLength() << std::endl;
        }
        else if (cmd == "queue" || cmd == "q") {
            auto stats = broker.GetStats();
            std::cout << "\n=== Queue Status ===" << std::endl;
            std::cout << "Queue length:     " << stats.queue_length << std::endl;
            std::cout << "Pending messages: " << stats.pending_count << std::endl;
            std::cout << "====================\n" << std::endl;
        }
        else if (cmd == "clear") {
            // 清空队列
            while (redis.LPop(config.message_queue_key)) {
                // 继续弹出
            }
            std::cout << "Queue cleared" << std::endl;
        }
        else if (cmd == "quit" || cmd == "exit") {
            break;
        }
        else if (cmd == "help" || cmd == "h") {
            PrintHelp();
        }
        else {
            std::cout << "Unknown command: " << cmd << std::endl;
            PrintHelp();
        }
    }

    std::cout << "Bye!" << std::endl;
    return 0;
}
