#pragma once

#include "pch.h"
#include "type_define.h"          // BufPtr / GTradeConfig / MyHandler
#include "i_strategy_engine.h"    // OrderReq / WithdrawReq
#include "db_structures.h"        // Order / Trade / Position / Balance
#include "msg_id.h"               // kPlaceOrder / kCancelOrder / kPlaceOrderConfirm ...
#include "string_keys.h"          // k_ctp（CTP market 标识，m_market 固定值）
#include "ThostFtdcTraderApi.h"   // CThostFtdcTraderApi
#include "ctp_spi_trader.h"       // CtpSpiTrader
#include "ctp_converter.h"        // CtpConverter

#include <memory>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <string>

// =====================================================================
// CtpTrader —— CTP 交易网关（每个账户一个，存于 StrategyEngine::m_trade_gw_map）
// ---------------------------------------------------------------------
// 职责（与 OkxTrade 对称）：
//   1. 维护一条 CTP 交易连接（TraderApi），登录 → 结算单确认 → 可交易。
//   2. 接收策略引擎发来的 kPlaceOrder / kCancelOrder，转为 CTP 报单/撤单请求。
//   3. 把 CTP 委托回报/成交回报/持仓查询/资金查询 转换为 GTrade 内部结构，
//      投递 kPlaceOrderConfirm / kTradePush / kPositionPush / kBalancePush。
//
// 线程模型：
//   - MyHandler 线程：处理 kPlaceOrder/kCancelOrder（下单/撤单请求）。
//   - CTP 内部线程：TraderApi->Init() 后由 CTP 库创建，回调 CtpSpiTrader → 本类方法。
//     本类回报方法直接 PostMsg 到策略引擎（PostMsg 线程安全）。
//   - OrderRef↔entno 映射被 MyHandler 线程(写)与 CTP 线程(读)共享，用 m_ref_mtx 保护。
//
// 订单关联键：GTrade entno 为 epoch 级大数，CTP OrderRef 仅 char[13]，故维护
//   本地递增序号 m_order_ref_seq，建立 ref↔entno 双向映射用于回报反查与撤单。
// =====================================================================
class CtpTrader : public MyHandler {
public:
    CtpTrader(const GTradeConfig& gtrade_cfg, const std::string& account_id, MyHandler* strat_engine);
    ~CtpTrader() override;

    bool Init() override;   // 注册 OnPlaceOrder / OnCancelOrder 处理器
    bool Start() override;  // 创建 TraderApi、订阅流、注册前置/SPI、启动连接
    void Stop() override;   // 释放 TraderApi

    // ===== MyHandler 线程：策略引擎发来的下单/撤单请求 =====
    void OnPlaceOrder(int msg_id, const BufPtr buffer);
    void OnCancelOrder(int msg_id, const BufPtr buffer);

    // ===== CTP 线程（由 CtpSpiTrader 转发）=====
    void OnFrontConnected();                 // 连接就绪 → 登录
    void OnRspUserLogin(bool ok, int front_id, int session_id);  // 登录应答 → 结算单确认
    void OnSettlementConfirmed();            // 结算确认完成 → 查询资金/持仓快照
    void OnRtnOrder(const CThostFtdcOrderField* pOrder);         // 委托回报 → kPlaceOrderConfirm
    void OnRtnTrade(const CThostFtdcTradeField* pTrade);         // 成交回报 → kTradePush
    void OnPosition(const CThostFtdcInvestorPositionField* pPos);// 持仓查询 → kPositionPush
    void OnAccount(const CThostFtdcTradingAccountField* pAcc);   // 资金查询 → kBalancePush

private:
    // 取账户 extra 配置项
    [[nodiscard]] std::string GetExtra(const std::string& key, const std::string& def = "") const;
    // 登录 / 结算单确认 / 查询
    void DoLogin();
    void DoSettlementConfirm();
    void DoQryAccount();
    void DoQryPosition();

    MyHandler* m_strategy_engine {};         // 策略引擎（投递回报目标）
    const GTradeConfig m_gtrade_cfg {};
    const std::string m_account_id {};
    const std::string m_market {k_ctp};      // 固定 "ctp"

    std::unique_ptr<CtpSpiTrader> m_spi {};
    CThostFtdcTraderApi* m_trader_api {};

    // 交易登录凭证（从账户 extra 读取）
    std::string m_broker_id {};
    std::string m_user_id {};                // 投资者账号（登录 UserID）
    std::string m_password {};
    std::string m_td_front {};               // 交易前置 tcp://ip:port

    std::atomic<int> m_request_id {0};       // CTP 请求号
    std::atomic<int> m_order_ref_seq {0};    // 本地 OrderRef 递增序号
    std::atomic<bool> m_logged_in {false};

    int m_front_id {0};                      // 登录应答返回的前置编号（撤单用）
    int m_session_id {0};                    // 登录应答返回的会话编号（撤单用）

    // OrderRef ↔ 本地 entno 双向映射（跨线程共享）
    std::mutex m_ref_mtx {};
    std::unordered_map<std::string, int64_t> m_ref_to_entno {};   // OrderRef -> entno（回报反查）
    std::unordered_map<int64_t, std::string> m_entno_to_ref {};   // entno -> OrderRef（撤单定位）
};
