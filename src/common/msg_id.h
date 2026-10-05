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
    // HTTP 交易接口（MCP）
    kHttpPlaceOrder,
    kHttpCancelOrder,
    kHttpGetDepth,
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
    // ── 标的范围订阅（web 设置页）────────────────────────────────────────────
    kStratUnsubscribeQuote,      // 引擎→行情服务：退订（与 SubscribeQuote 对称）
    kHttpQueryInstruments,       // 网关→引擎：查询全量标的
    kHttpGetInstrumentScope,     // 网关→引擎：查询标的范围与订阅现状
    kHttpSetInstrumentScope,     // 网关→引擎：应用标的范围（订阅/退订）
    kDbSetInstrumentScope,       // 引擎→DB：全量替换写标的范围（异步）
    kDbQueryInstrumentScopeReq,  // 引擎→DB：启动加载标的范围（同步）
    // ── 启动高水位（ID 号段基数）────────────────────────────────────────────
    kDbQueryMaxIdsReq,           // 引擎→DB：查询 order/trade 的最大号，供设置号段基数（同步）
    // ── 补查接口（web_server → 引擎，读内存权威态）──────────────────────────
    kHttpQueryOrdersReq,         // 网关/推送端→引擎：按 entno 游标或委托号集合补查委托（同步）
    kHttpQueryTradesReq,         // 网关/推送端→引擎：按 tdno 游标补查成交（同步）
    kEnd,
};