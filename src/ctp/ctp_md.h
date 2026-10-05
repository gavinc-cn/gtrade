#pragma once

#include "pch.h"
#include "service_map.h"
#include "i_strategy_engine.h"   // QuoteSub
#include "i_exchange_data.h"     // Depth
#include "type_define.h"         // BufPtr / GTradeConfig / Account
#include "msg_id.h"              // kDepth1 / kStratSubscribeQuote
#include "ThostFtdcMdApi.h"      // CThostFtdcMdApi
#include "ctp_spi_md.h"          // CtpSpiMd
#include "my_utc.h"

#include <memory>
#include <mutex>
#include <unordered_set>
#include <atomic>
#include <string>

// =====================================================================
// CtpMd —— CTP 行情接入（每个进程一个，对应 ServiceMap 中的 k_CtpQuote）
// ---------------------------------------------------------------------
// 职责：
//   1. 维护一条 CTP 行情连接（MdApi），登录后订阅合约，接收深度行情。
//   2. 把 CTP 深度行情（CThostFtdcDepthMarketDataField）经 CtpConverter
//      转换为 GTrade 内部 Depth（仅买卖 1 档），投递 kDepth1 给策略引擎。
//
// 线程模型（与 OkxWs 一致）：
//   - MyHandler 线程：处理策略引擎发来的 kStratSubscribeQuote（订阅请求）。
//   - CTP 内部线程：MdApi->Init() 后由 CTP 库创建，回调 CtpSpiMd → 本类方法。
//     本类方法（OnFrontConnected/OnRspUserLogin/OnDepth）在 CTP 线程执行，
//     其中 OnDepth 直接 PostMsg(kDepth1) 到策略引擎（PostMsg 线程安全）。
//   - 跨线程共享状态（订阅集合/登录标志）用 m_sub_mtx 保护。
//
// 配置来源：行情连接的 broker_id/user_id/password/md_front 取自行情账户
//          account_map[md_account_id].extra（见 string_keys.h 的 ctp 扩展键）。
//          openctp 7x24 环境 broker_id/user/password 可用任意模拟账号。
// =====================================================================
class CtpMd : public MyHandler {
public:
    // pool: ServiceMap（用于获取策略引擎指针）
    // md_account_id: 提供行情登录凭证的 CTP 账户 id（其 extra 含 md_front 等）
    CtpMd(const ServiceMap& pool, const GTradeConfig& gtrade_cfg, const std::string& md_account_id);
    ~CtpMd() override;

    // ===== 生命周期（由 ServiceMap 统一调用）=====
    bool Init() override;   // 注册 OnSubscribeQuote 处理器
    bool Start() override;  // 创建 MdApi、注册前置/SPI、启动 CTP 连接
    void Stop() override;   // 释放 MdApi

    // ===== MyHandler 线程：策略引擎发来的订阅请求 =====
    void OnSubscribeQuote(int msg_id, const BufPtr buffer);

    // ===== CTP 线程（由 CtpSpiMd 转发）=====
    void OnFrontConnected();                      // 连接就绪 → 登录
    void OnRspUserLogin(bool ok);                 // 登录应答 → 补订阅 pending 合约
    void OnDepth(const CThostFtdcDepthMarketDataField* md);  // 行情 → 转换 + 投递

private:
    // 取账户 extra 中的配置项，缺失返回默认
    [[nodiscard]] std::string GetExtra(const std::string& key, const std::string& def = "") const;
    // 执行登录请求（连接就绪后调用）
    void DoLogin();
    // 订阅一个合约（已登录则即时订阅，否则暂存 pending）
    void SubscribeInstrument(const std::string& inst_id);

    const GTradeConfig m_gtrade_cfg {};
    MyHandler* m_strategy_engine {};        // 策略引擎（投递 kDepth1 目标）
    const std::string m_md_account_id {};  // 行情登录账户

    std::unique_ptr<CtpSpiMd> m_spi {};    // SPI（回调转发层，本类拥有）
    CThostFtdcMdApi* m_md_api {};          // MdApi（CreateFtdcMdApi 返回，Release 释放）

    // 行情登录凭证（从账户 extra 读取）
    std::string m_broker_id {};
    std::string m_user_id {};
    std::string m_password {};
    std::string m_md_front {};             // 行情前置 tcp://ip:port

    std::atomic<int> m_request_id {0};     // CTP 请求号
    std::atomic<bool> m_connected {false};
    std::atomic<bool> m_logged_in {false};
    std::atomic<int64_t> m_depth_seq {0};  // 行情序号（CTP 基础行情无 seqId，本地自增）

    // 跨线程（MyHandler 线程写、CTP 线程读）订阅集合
    std::mutex m_sub_mtx {};
    std::unordered_set<std::string> m_subscribed {};  // 已下发订阅
    std::unordered_set<std::string> m_pending {};     // 登录前暂存
};
