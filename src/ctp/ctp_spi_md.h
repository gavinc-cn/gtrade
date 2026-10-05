#pragma once

#include "pch.h"
#include "ThostFtdcMdApi.h"  // CThostFtdcMdSpi / CThostFtdcMdApi / 各 *Field

// 前向声明：避免与 ctp_md.h 互相包含
class CtpMd;

// =====================================================================
// CtpSpiMd —— CTP 行情 SPI（回调接口）薄转发层
// ---------------------------------------------------------------------
// CTP API 在其内部线程上回调 SPI 方法；这里只做最小转发，把事件交给
// 持有它的 CtpMd 处理（转换 + Post 到策略引擎）。CtpMd 负责线程安全。
//
// 设计原因：CTP 行情接口为 SPI 回调模型（非轮询），必须有子类重写回调；
// 把所有业务逻辑集中在 CtpMd，SPI 仅做"接到事件→转交"。
// =====================================================================
// CThostFtdcMdSpi 析构函数非虚（CTP C 风格 API）。本对象生命周期由 CtpMd 持有，
// 绝不通过 CThostFtdcMdSpi* 基类指针 delete（CTP 库自身通过 Release() 释放 API），
// 故 -Wnon-virtual-dtor 属误报，在本类定义处局部抑制。
// 注意：在派生类加 virtual 析构并不能消除该告警——告警针对的是"基类可访问的
// 非虚析构"，只能靠此 pragma 在类定义处关闭。
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
class CtpSpiMd : public CThostFtdcMdSpi {
public:
    explicit CtpSpiMd(CtpMd* owner) : m_owner(owner) {}
    // 多态回调类保持虚析构，便于安全销毁派生对象；基类析构非虚，故不能用 override。
    virtual ~CtpSpiMd() = default;

    // ===== 连接管理 =====
    // 与交易前置建立连接成功（登录前）→ 触发登录
    void OnFrontConnected() override;
    // 与前置断开（CTP 会自动重连）
    void OnFrontDisconnected(int nReason) override;
    // 心跳超时告警
    void OnHeartBeatWarning(int nTimeLapse) override;

    // ===== 登录应答 =====
    void OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin,
                        CThostFtdcRspInfoField* pRspInfo,
                        int nRequestID, bool bIsLast) override;

    // ===== 行情推送（核心）=====
    // 收到一帧深度行情 → 转交 CtpMd 转换为 GTrade Depth 并投递 kDepth1
    void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* pDepthMarketData) override;

    // ===== 订阅应答 =====
    void OnRspSubMarketData(CThostFtdcSpecificInstrumentField* pSpecificInstrument,
                            CThostFtdcRspInfoField* pRspInfo,
                            int nRequestID, bool bIsLast) override;
    void OnRspError(CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) override;

private:
    CtpMd* m_owner;  // 持有者，事件转交目标（非拥有，不负责释放）
};
#pragma GCC diagnostic pop
