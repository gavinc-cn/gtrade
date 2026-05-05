//
// Created by dell on 2025/12/5.
//

#pragma once



enum MsgId {
    kBegin,
    // 行情
    kOkxWsBboTbt,
    kDepth1,
    kKLine,
    // 策略消息
    kStratSubscribeQuote,
    kStratSubscribeTrade,
    kStratSubscribeKLine,
    kStratSubscribeKLineOpen,
    kStratSubscribeKLineClose,
    // 委托
    kPlaceOrder,
    kPlaceOrderRsp,
    kPlaceOrderConfirm,
    kOrderRecovery,  // websocket重连后的委托恢复（独立通道）
    // 批量委托
    kPlaceOrderBatch,
    kPlaceOrderBatchRsp,
    kPlaceOrderBatchConfirm,
    // 修改委托
    kModifyOrder,
    kModifyOrderRsp,
    kModifyOrderBatch,
    kModifyOrderBatchRsp,
    // 撤单
    kCancelOrder,
    kCancelOrderRsp,
    kCancelOrderBatch,
    kCancelOrderBatchRsp,
    // 成交
    kTradePush,
    // 持仓
    kPositionPush,
    // 策略持仓推送
    kPortfolioPosPush,
    // 资金
    kBalancePush,
    // 查询市场信息
    kQueryMarketInfoReq,
    kQueryMarketInfoRsp,
    kQueryMarketInfoErr,
    kQueryMarketInfoSync,  // 策略同步查询市场信息
    // 查询委托
    kQueryOrderReq,
    kQueryOrderRsp,
    kQueryOrderErr,
    // 通过private_no查询委托
    kQueryOrderByPrivateNoReq,
    kQueryOrderByPrivateNoRsp,
    kQueryOrderByPrivateNoErr,
    // 查询成交
    kQueryTradeReq,
    kQueryTradeRsp,
    kQueryTradeErr,
    // 查询持仓
    kQueryPositionReq,
    kQueryPositionRsp,
    kQueryPositionErr,
    // 查询资金
    kQueryBalanceReq,
    kQueryBalanceRsp,
    kQueryBalanceErr,
    // 查询历史委托
    kQueryHisOrdersReq,
    kQueryHisOrdersRsp,
    kQueryHisOrdersErr,
    // 查询K线
    kQueryKLineReq,
    kQueryKLineRsp,
    // kQueryKLineErr,
    kQueryKLinePatchReq,
    kQueryKLinePatchRsp,
    kQueryKLinePatchErr,
    // K线推送
    kIndicatorKLinePush,
    kIndicatorKLineOpenPush,
    kIndicatorKLineClosePush,
    // websocket
    kWebSocketOpenNotify,
    // 定时器
    kStartEpochGenerator,
    kSetTimer,
    kKillTimer,
    kClearAllTimer,
    kPauseAllTimer,
    kResumeAllTimer,
    kListAllTimer,
    kCsvQuoteTimerEvent,
    kBacktestTimerEvent,
    kTimerEvent,
    // 数据库操作
    kDbSetStrategyInfo,
    kDbDelStrategyInfo,
    kDbDelAllStrategyInfo,
    kDbDelOrder,
    kDbDelTrade,
    // kDbSetDone,
    kDbSetKLine,
    kDbDelKLine,
    kDbSetOrder,
    kDbSetTrade,
    kDbSetPosition,
    kDbSetPortfolioPosition,
    kDbSetStrategyLog,
    kDbQueryHisOrdersReq,
    // 外发通知消息
    kNotifyMessage,
    kNotifyMessageRsp,
    kNotifyMessageErr,
    // HTTP策略管理
    kHttpAddStrategy,
    kHttpDeleteStrategy,
    kHttpRestartStrategy,
    kHttpStartStrategy,
    kHttpStopStrategy,
    kHttpQueryAllStrategies,
    kHttpQueryStrategiesByTemplate,
    kHttpGetTemplates,
    kHttpGetTemplateConfig,
    // 系统管理
    kHttpSaveSnapshot,
    kHttpGetWalStats,
    // 策略生命周期同步控制（引擎→策略线程）
    kStratStartSync,
    kStratStopSync,
    // 子进程策略生命周期控制（通过 Channel C 共享内存发送给子进程）
    kSubprocStratStart,
    kSubprocStratStop,
    kSubprocStratPause,
    kSubprocStratResume,
    // ── 远程策略部署（ZMQ ROUTER/DEALER）──────────────────────────────────────
    kSubprocHeartbeat,        // Runner→Engine 心跳，无 payload；Engine 侧以收到时的本地时间计时
    kRemoteHandshake,         // Runner→Engine 握手，payload = RemoteHandshakePayload 结构体
    kRemoteSyncReq,           // Runner→Engine 状态同步请求，payload = pending client_order_ids（可为空）
    kRemoteSyncResp,          // Engine→Runner 状态同步响应，payload = 该策略全量活跃订单
    kRemoteStrategyConnected, // AcceptLoop→StrategyEngine 内部事件，不跨网络；payload = peer_id + RemoteHandshakePayload
    kRemoteChannelB,          // AcceptLoop→StrategyEngine 内部事件，非握手帧转发；payload = peer_id + msg_type + data
    kEnd,
};