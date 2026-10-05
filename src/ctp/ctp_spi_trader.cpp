#include "ctp_spi_trader.h"
#include "ctp_trader.h"  // 完整定义（前向声明在此兑现）

// =====================================================================
// CtpSpiTrader 实现：所有回调仅转发给 CtpTrader（CTP 线程执行）
// =====================================================================

void CtpSpiTrader::OnFrontConnected() {
    SPDLOG_INFO("[CtpTrader] front connected");
    if (m_owner) m_owner->OnFrontConnected();
}

void CtpSpiTrader::OnFrontDisconnected(int nReason) {
    SPDLOG_WARN("[CtpTrader] front disconnected, reason={:#x}", nReason);
    // CTP 自动重连，重连后重新触发 OnFrontConnected → 重新登录
}

void CtpSpiTrader::OnHeartBeatWarning(int nTimeLapse) {
    SPDLOG_WARN("[CtpTrader] heartbeat warning, lapse={}s", nTimeLapse);
}

void CtpSpiTrader::OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin,
                                  CThostFtdcRspInfoField* pRspInfo,
                                  int nRequestID, bool bIsLast) {
    (void)nRequestID; (void)bIsLast;
    const bool ok = (pRspInfo == nullptr || pRspInfo->ErrorID == 0);
    int front_id = 0, session_id = 0;
    if (ok && pRspUserLogin != nullptr) {
        front_id   = static_cast<int>(pRspUserLogin->FrontID);
        session_id = static_cast<int>(pRspUserLogin->SessionID);
        SPDLOG_INFO("[CtpTrader] login success, front_id={} session_id={}", front_id, session_id);
    } else if (pRspInfo != nullptr) {
        SPDLOG_ERROR("[CtpTrader] login failed, error_id={} msg={}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    }
    if (m_owner) m_owner->OnRspUserLogin(ok, front_id, session_id);
}

void CtpSpiTrader::OnRspSettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField* pSettlementInfoConfirm,
                                              CThostFtdcRspInfoField* pRspInfo,
                                              int nRequestID, bool bIsLast) {
    (void)pSettlementInfoConfirm; (void)nRequestID; (void)bIsLast;
    const bool ok = (pRspInfo == nullptr || pRspInfo->ErrorID == 0);
    if (!ok && pRspInfo != nullptr) {
        SPDLOG_ERROR("[CtpTrader] settlement confirm failed, error_id={} msg={}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    } else {
        SPDLOG_INFO("[CtpTrader] settlement confirmed");
    }
    if (m_owner && ok) m_owner->OnSettlementConfirmed();
}

void CtpSpiTrader::OnRspOrderInsert(CThostFtdcInputOrderField* pInputOrder,
                                    CThostFtdcRspInfoField* pRspInfo,
                                    int nRequestID, bool bIsLast) {
    (void)pInputOrder; (void)nRequestID; (void)bIsLast;
    // 报单被拒（同步应答）：记录错误。后续 OnRtnOrder 也会推送拒单状态。
    if (pRspInfo != nullptr) {
        SPDLOG_ERROR("[CtpTrader] order insert rejected, error_id={} msg={}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    }
}

void CtpSpiTrader::OnRspOrderAction(CThostFtdcInputOrderActionField* pInputOrderAction,
                                    CThostFtdcRspInfoField* pRspInfo,
                                    int nRequestID, bool bIsLast) {
    (void)pInputOrderAction; (void)nRequestID; (void)bIsLast;
    if (pRspInfo != nullptr && pRspInfo->ErrorID != 0) {
        SPDLOG_ERROR("[CtpTrader] order action(reject) error_id={} msg={}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    }
}

void CtpSpiTrader::OnRtnOrder(CThostFtdcOrderField* pOrder) {
    if (m_owner && pOrder) m_owner->OnRtnOrder(pOrder);
}

void CtpSpiTrader::OnRtnTrade(CThostFtdcTradeField* pTrade) {
    if (m_owner && pTrade) m_owner->OnRtnTrade(pTrade);
}

void CtpSpiTrader::OnRspQryInvestorPosition(CThostFtdcInvestorPositionField* pInvestorPosition,
                                            CThostFtdcRspInfoField* pRspInfo,
                                            int nRequestID, bool bIsLast) {
    (void)nRequestID; (void)bIsLast;
    const bool ok = (pRspInfo == nullptr || pRspInfo->ErrorID == 0);
    if (!ok && pRspInfo != nullptr) {
        SPDLOG_ERROR("[CtpTrader] qry position failed, error_id={} msg={}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    }
    if (m_owner && ok && pInvestorPosition) m_owner->OnPosition(pInvestorPosition);
}

void CtpSpiTrader::OnRspQryTradingAccount(CThostFtdcTradingAccountField* pTradingAccount,
                                          CThostFtdcRspInfoField* pRspInfo,
                                          int nRequestID, bool bIsLast) {
    (void)nRequestID; (void)bIsLast;
    const bool ok = (pRspInfo == nullptr || pRspInfo->ErrorID == 0);
    if (!ok && pRspInfo != nullptr) {
        SPDLOG_ERROR("[CtpTrader] qry account failed, error_id={} msg={}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    }
    if (m_owner && ok && pTradingAccount) m_owner->OnAccount(pTradingAccount);
}

void CtpSpiTrader::OnRspQryInstrument(CThostFtdcInstrumentField* pInstrument,
                                      CThostFtdcRspInfoField* pRspInfo,
                                      int nRequestID, bool bIsLast) {
    // 合约查询仅用于校验/记录，暂不向策略引擎投递
    (void)pInstrument; (void)pRspInfo; (void)nRequestID; (void)bIsLast;
}

void CtpSpiTrader::OnRspError(CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
    (void)nRequestID; (void)bIsLast;
    if (pRspInfo) {
        SPDLOG_ERROR("[CtpTrader] rsp error, error_id={} msg={}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    }
}
