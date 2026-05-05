//
// MessageServer 使用示例
// 展示如何在其他服务中发送 Slack 消息
//

#include "notify_msg_helper.h"
#include "type_define.h"
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

// ============================================================================
// 示例 1: 在策略引擎中发送交易信号通知
// ============================================================================

class StrategyEngineExample {
public:
    void OnTradingSignal(const std::string& instrument,
                        const std::string& signal_type,
                        double price) {
        std::string subject = fmt::format("交易信号: {} - {}", instrument, signal_type);
        std::string content = fmt::format(
            "标的: {}\n"
            "信号类型: {}\n"
            "价格: {:.4f}\n"
            "时间: {}",
            instrument, signal_type, price, GetCurrentTimeStr()
        );

        // 使用辅助类发送消息
        SlackMessageHelper::SendSlackMessage(*this, "trading", subject, content, true);
    }

    // 模拟 PostMsg 方法 (实际项目中由基类提供)
    void PostMsg(const std::string& service, int msg_id, TBufferPtr buf) {
        SPDLOG_INFO("Posting message to service={} msg_id={}", service, msg_id);
        // 实际实现会通过消息总线发送
    }

private:
    std::string GetCurrentTimeStr() {
        return "2025-10-28 00:00:00";
    }
};

// ============================================================================
// 示例 2: 在策略中发送订单成交通知
// ============================================================================

class StrategyExample {
public:
    void OnOrderFilled(const std::string& instrument,
                      const std::string& side,
                      double price,
                      double quantity) {
        std::string details = fmt::format(
            "标的: {}\n"
            "方向: {}\n"
            "价格: {:.4f}\n"
            "数量: {:.4f}",
            instrument, side, price, quantity
        );

        // 使用便捷方法发送交易通知
        SlackMessageHelper::SendTradeNotification(*this, instrument, "订单成交", details);
    }

    void PostMsg(const std::string& service, int msg_id, TBufferPtr buf) {
        SPDLOG_INFO("Posting message");
    }
};

// ============================================================================
// 示例 3: 在错误处理中发送错误通知
// ============================================================================

class ErrorHandlerExample {
public:
    void OnCriticalError(const std::string& component, const std::exception& e) {
        std::string error_msg = fmt::format(
            "组件: {}\n"
            "错误类型: {}\n"
            "错误信息: {}",
            component, typeid(e).name(), e.what()
        );

        // 同步发送关键错误（确保发送成功）
        SlackMessageHelper::SendError(*this, component, error_msg, false);
    }

    void OnWarning(const std::string& component, const std::string& warning) {
        SlackMessageHelper::SendWarning(*this, component, warning, true);
    }

    void PostMsg(const std::string& service, int msg_id, TBufferPtr buf) {
        SPDLOG_INFO("Posting message");
    }
};

// ============================================================================
// 示例 4: 发送自定义格式的消息
// ============================================================================

class CustomMessageExample {
public:
    void SendBacktestReport(const std::string& strategy_name,
                          double total_return,
                          double sharpe_ratio,
                          double max_drawdown) {
        std::string subject = fmt::format("回测报告: {}", strategy_name);
        std::string content = fmt::format(
            "策略名称: {}\n"
            "总收益率: {:.2f}%\n"
            "夏普比率: {:.2f}\n"
            "最大回撤: {:.2f}%",
            strategy_name,
            total_return * 100,
            sharpe_ratio,
            max_drawdown * 100
        );

        SlackMessageHelper::SendSlackMessage(*this, "backtest", subject, content, true);
    }

    void SendPerformanceAlert(double cpu_usage, double memory_usage) {
        std::string subject = "系统性能警告";
        std::string content = fmt::format(
            "CPU 使用率: {:.1f}%\n"
            "内存使用率: {:.1f}%",
            cpu_usage * 100,
            memory_usage * 100
        );

        SlackMessageHelper::SendSlackMessage(*this, "alerts", subject, content, true);
    }

    void PostMsg(const std::string& service, int msg_id, TBufferPtr buf) {
        SPDLOG_INFO("Posting message");
    }
};

// ============================================================================
// 示例 5: 直接构造 SlackMessageReq 发送消息
// ============================================================================

class DirectMessageExample {
public:
    void SendDirectMessage() {
        // 直接构造请求
        NotifyMessageReq req {};
        zrt::fill_field(req.channel, "alerts");
        zrt::fill_field(req.subject, "测试主题");
        zrt::fill_field(req.content, "测试内容");
        req.is_async = true;

        // 创建缓冲区
        TBufferPtr buf = std::make_shared<TBuffer>();
        buf->Append(req);

        // 发送到 MessageServer
        PostMsg(k_MessageServer, MsgId::kNotifyMessage, buf);
    }

    void PostMsg(const std::string& service, int msg_id, TBufferPtr buf) {
        SPDLOG_INFO("Posting message to {} with msg_id={}", service, msg_id);
    }
};

// ============================================================================
// 主函数 - 运行示例
// ============================================================================

int main() {
    spdlog::set_level(spdlog::level::info);

    std::cout << "=== MessageServer 使用示例 ===" << std::endl;

    // 示例 1
    std::cout << "\n示例 1: 发送交易信号通知" << std::endl;
    StrategyEngineExample strategy_engine;
    strategy_engine.OnTradingSignal("BTC-USDT", "BUY", 50000.0);

    // 示例 2
    std::cout << "\n示例 2: 发送订单成交通知" << std::endl;
    StrategyExample strategy;
    strategy.OnOrderFilled("ETH-USDT", "SELL", 3000.0, 1.5);

    // 示例 3
    std::cout << "\n示例 3: 发送错误和警告通知" << std::endl;
    ErrorHandlerExample error_handler;
    try {
        throw std::runtime_error("测试错误");
    } catch (const std::exception& e) {
        error_handler.OnCriticalError("TestComponent", e);
    }
    error_handler.OnWarning("TestComponent", "测试警告信息");

    // 示例 4
    std::cout << "\n示例 4: 发送自定义消息" << std::endl;
    CustomMessageExample custom;
    custom.SendBacktestReport("StrategyA", 0.25, 1.8, 0.15);
    custom.SendPerformanceAlert(0.85, 0.75);

    // 示例 5
    std::cout << "\n示例 5: 直接发送消息" << std::endl;
    DirectMessageExample direct;
    direct.SendDirectMessage();

    std::cout << "\n=== 示例完成 ===" << std::endl;
    return 0;
}
