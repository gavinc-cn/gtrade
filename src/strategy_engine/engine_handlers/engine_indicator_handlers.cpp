//
// Created by dell on 2025/7/10.
//


#include "pch.h"
#include "str_types.h"
#include "strategy_engine.h"

void StrategyEngine::OnSubscribeKLine(int msg_id, const BufPtr buffer) {
    const KLineSub recv_data = *reinterpret_cast<const KLineSub*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    // 用来给策略分发, 是不是回测都需要
    m_kline_sub_map[{recv_data.market,recv_data.instrument,recv_data.coefficient,recv_data.scale}].emplace(recv_data.strat_id);
    if constexpr (!GlobalConst::IsRealTrading) {
        m_event_source_manager->SubscribeKLine({recv_data.market, recv_data.instrument, recv_data.coefficient, recv_data.scale});
    }
}

void StrategyEngine::OnIndicatorKlinePush(int msg_id, const BufPtr buffer) {
    const auto recv_data = *reinterpret_cast<const KLine*>(buffer->Data());
    SPDLOG_TRACE("market={} symbol={}", recv_data.market, recv_data.instrument);
    SendToSubedStrategies(MsgId::kIndicatorKLinePush, buffer, m_kline_sub_map[{recv_data.market,recv_data.instrument,recv_data.coefficient,recv_data.scale}]);
}

void StrategyEngine::OnSubscribeKLineOpen(int msg_id, const BufPtr buffer) {
    const KLineSub recv_data = *reinterpret_cast<const KLineSub*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    m_kline_open_sub_map[{recv_data.market,recv_data.instrument,recv_data.coefficient,recv_data.scale}].emplace(recv_data.strat_id);
    if constexpr (!GlobalConst::IsRealTrading) {
        m_event_source_manager->SubscribeKLine({recv_data.market, recv_data.instrument, recv_data.coefficient, recv_data.scale});
    }
}

void StrategyEngine::OnIndicatorKlineOpenPush(int msg_id, const BufPtr buffer) {
    const auto recv_data = *reinterpret_cast<const KLine*>(buffer->Data());
    SPDLOG_TRACE("market={} symbol={}", recv_data.market, recv_data.instrument);
    SendToSubedStrategies(MsgId::kIndicatorKLineOpenPush, buffer, m_kline_open_sub_map[{recv_data.market,recv_data.instrument,recv_data.coefficient,recv_data.scale}]);
}

void StrategyEngine::OnSubscribeKLineClose(int msg_id, const BufPtr buffer) {
    const KLineSub recv_data = *reinterpret_cast<const KLineSub*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));
    m_kline_close_sub_map[{recv_data.market,recv_data.instrument,recv_data.coefficient,recv_data.scale}].emplace(recv_data.strat_id);
    if constexpr (!GlobalConst::IsRealTrading) {
        m_event_source_manager->SubscribeKLine({recv_data.market, recv_data.instrument, recv_data.coefficient, recv_data.scale});
    }
}

void StrategyEngine::OnIndicatorKlineClosePush(int msg_id, const BufPtr buffer) {
    const auto recv_data = *reinterpret_cast<const KLine*>(buffer->Data());
    SPDLOG_TRACE("market={} symbol={}", recv_data.market, recv_data.instrument);
    SendToSubedStrategies(MsgId::kIndicatorKLineClosePush, buffer, m_kline_close_sub_map[{recv_data.market,recv_data.instrument,recv_data.coefficient,recv_data.scale}]);
}