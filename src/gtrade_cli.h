//
// Created by dell on 2025/11/14.
//

#ifndef GTRADE_CLI_H
#define GTRADE_CLI_H

#include <string>

/**
 * GTrade HTTP客户端类
 * 用于与运行中的gtrade服务端进行通信，执行策略管理操作
 */
class GTradeClient {
public:
    /**
     * 构造函数
     * @param host 服务器地址，默认为localhost
     * @param port 服务器端口，默认从配置文件加载
     */
    explicit GTradeClient(const std::string& host = "localhost", int port = 0);

    /**
     * 从配置文件加载HTTP端口
     * @return 成功返回端口号，失败返回8080（默认端口）
     */
    static int LoadHttpPortFromConfig();

    /**
     * 添加新策略
     * @param cfg_path 策略配置文件路径
     * @return 0表示成功，1表示失败
     */
    int AddStrategy(const std::string& cfg_path);

    /**
     * 删除已有策略
     * @param strat_id 策略ID
     * @return 0表示成功，1表示失败
     */
    int DeleteStrategy(const std::string& strat_id);

    /**
     * 重启已有策略
     * @param strat_id 策略ID
     * @return 0表示成功，1表示失败
     */
    int RestartStrategy(const std::string& strat_id);

    /**
     * 列出所有策略
     * @return 0表示成功，1表示失败
     */
    int ListStrategies();

    /**
     * 手动触发保存快照
     * @return 0表示成功，1表示失败
     */
    int SaveSnapshot();

    /**
     * 获取WAL统计信息
     * @return 0表示成功，1表示失败
     */
    int GetWalStats();

private:
    std::string m_host;  // 服务器地址
    int m_port;          // 服务器端口
};

#endif // GTRADE_CLI_H
