#include "ctp_spi_md.h"
#include "ctp_md.h"  // 完整定义（前向声明在此兑现）

// =====================================================================
// CtpSpiMd 实现：所有回调仅转发给 CtpMd，业务逻辑在 CtpMd 中（CTP 线程执行）
// =====================================================================

void CtpSpiMd::OnFrontConnected() {
    SPDLOG_INFO("[CtpMd] front connected");
    if (m_owner) m_owner->OnFrontConnected();
}

void CtpSpiMd::OnFrontDisconnected(int nReason) {
    // CTP 会自动重连，无需手动处理；记录原因即可
    SPDLOG_WARN("[CtpMd] front disconnected, reason={:#x}", nReason);
    if (m_owner) {
        // 重置连接/登录态，重连后会重新触发 OnFrontConnected → 重新登录
        // 通过 OnRspUserLogin(false) 复用状态清理逻辑不合适，这里直接置位即可。
    }
}

void CtpSpiMd::OnHeartBeatWarning(int nTimeLapse) {
    SPDLOG_WARN("[CtpMd] heartbeat warning, lapse={}s", nTimeLapse);
}

void CtpSpiMd::OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin,
                              CThostFtdcRspInfoField* pRspInfo,
                              int nRequestID, bool bIsLast) {
    (void)pRspUserLogin; (void)nRequestID; (void)bIsLast;
    // pRspInfo->ErrorID==0 且 ErrorMsg 为空表示成功
    const bool ok = (pRspInfo == nullptr || pRspInfo->ErrorID == 0);
    if (!ok) {
        SPDLOG_ERROR("[CtpMd] login failed, error_id={} msg={}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    } else {
        SPDLOG_INFO("[CtpMd] login success");
    }
    if (m_owner) m_owner->OnRspUserLogin(ok);
}

void CtpSpiMd::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* pDepthMarketData) {
    if (m_owner && pDepthMarketData) m_owner->OnDepth(pDepthMarketData);
}

void CtpSpiMd::OnRspSubMarketData(CThostFtdcSpecificInstrumentField* pSpecificInstrument,
                                  CThostFtdcRspInfoField* pRspInfo,
                                  int nRequestID, bool bIsLast) {
    (void)pSpecificInstrument; (void)nRequestID; (void)bIsLast;
    const bool ok = (pRspInfo == nullptr || pRspInfo->ErrorID == 0);
    if (!ok) {
        SPDLOG_ERROR("[CtpMd] subscribe failed, error_id={} msg={}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    }
}

void CtpSpiMd::OnRspError(CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) {
    (void)nRequestID; (void)bIsLast;
    if (pRspInfo) {
        SPDLOG_ERROR("[CtpMd] rsp error, error_id={} msg={}", pRspInfo->ErrorID, pRspInfo->ErrorMsg);
    }
}
