#pragma once

#include "pch.h"
#include "ThostFtdcTraderApi.h"  // CThostFtdcTraderSpi / CThostFtdcTraderApi / 各 *Field

// 前向声明：避免与 ctp_trader.h 互相包含
class CtpTrader;

// =====================================================================
// CtpSpiTrader —— CTP 交易 SPI（回调接口）薄转发层
// ---------------------------------------------------------------------
// CTP API 在其内部线程上回调 SPI 方法；这里只做最小转发，把事件交给
// 持有它的 CtpTrader 处理（转换 + Post 到策略引擎）。
// =====================================================================
// CThostFtdcTraderSpi 析构函数非虚（CTP C 风格 API）。本对象生命周期由 CtpTrader 持有，
// 绝不通过 CThostFtdcTraderSpi* 基类指针 delete（CTP 库自身通过 Release() 释放 API），
// 故 -Wnon-virtual-dtor 属误报，在本类定义处局部抑制。
// 注意：在派生类加 virtual 析构并不能消除该告警——告警针对的是"基类可访问的
// 非虚析构"，只能靠此 pragma 在类定义处关闭。
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
class CtpSpiTrader : public CThostFtdcTraderSpi {
public:
    explicit CtpSpiTrader(CtpTrader* owner) : m_owner(owner) {}
    // 多态回调类保持虚析构，便于安全销毁派生对象；基类析构非虚，故不能用 override。
    virtual ~CtpSpiTrader() = default;

    // ===== 连接管理 =====
    void OnFrontConnected() override;
    void OnFrontDisconnected(int nReason) override;
    void OnHeartBeatWarning(int nTimeLapse) override;

    // ===== 登录 / 结算单确认 =====
    void OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin,
                        CThostFtdcRspInfoField* pRspInfo,
                        int nRequestID, bool bIsLast) override;
    void OnRspSettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField* pSettlementInfoConfirm,
                                    CThostFtdcRspInfoField* pRspInfo,
                                    int nRequestID, bool bIsLast) override;

    // ===== 报单 / 撤单回报 =====
    // 报单录入应答（被拒等同步应答）
    void OnRspOrderInsert(CThostFtdcInputOrderField* pInputOrder,
                          CThostFtdcRspInfoField* pRspInfo,
                          int nRequestID, bool bIsLast) override;
    // 撤单操作应答
    void OnRspOrderAction(CThostFtdcInputOrderActionField* pInputOrderAction,
                          CThostFtdcRspInfoField* pRspInfo,
                          int nRequestID, bool bIsLast) override;
    // 委托回报（订单状态变化推送，核心）
    void OnRtnOrder(CThostFtdcOrderField* pOrder) override;
    // 成交回报（成交通报，核心）
    void OnRtnTrade(CThostFtdcTradeField* pTrade) override;

    // ===== 查询应答 =====
    void OnRspQryInvestorPosition(CThostFtdcInvestorPositionField* pInvestorPosition,
                                  CThostFtdcRspInfoField* pRspInfo,
                                  int nRequestID, bool bIsLast) override;
    void OnRspQryTradingAccount(CThostFtdcTradingAccountField* pTradingAccount,
                                CThostFtdcRspInfoField* pRspInfo,
                                int nRequestID, bool bIsLast) override;
    void OnRspQryInstrument(CThostFtdcInstrumentField* pInstrument,
                            CThostFtdcRspInfoField* pRspInfo,
                            int nRequestID, bool bIsLast) override;

    // ===== 错误 =====
    void OnRspError(CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;

private:
    CtpTrader* m_owner;  // 持有者，事件转交目标（非拥有，不负责释放）
};
#pragma GCC diagnostic pop
