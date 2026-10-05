//
// Created by dell on 2026/3/12.
//


#include "strategy_engine.h"

BufPtr StrategyEngine::OnHttpQueryTrades(int msg_id, const BufPtr buffer) {
    // 补查成交（web_server 断线重连后调用）：按 tdno 游标取内存权威态，不查库。
    // 响应 = 若干条 Trade 记录；空响应表示没有更多数据（异常另外记 ERROR）。
    const auto& req = buffer->RefData<HttpQueryTradesReq>();
    BufPtr rsp_buf = std::make_shared<TBuffer>();

    const int limit = (req.limit > 0) ? std::min(req.limit, kQueryMaxRows) : kQueryDefaultRows;
    const std::vector<Trade> trades =
        m_order_manager.QueryTradesAfter(req.cursor_tdno, static_cast<size_t>(limit));
    SPDLOG_INFO("query trades after tdno={}: returned={} (limit={})", req.cursor_tdno, trades.size(), limit);

    for (const Trade& trade : trades) {
        rsp_buf->Append(trade);
    }
    return rsp_buf;
}

void StrategyEngine::OnQueryTradeRsp(int msg_id, const BufPtr buffer) {
    // using RecvData = MarketInfo;
    // const auto* recv_data_ptr = reinterpret_cast<const RecvData*>(buffer->Data());
    // for (size_t i=0; i<buffer->GetSize()/sizeof(RecvData); ++i) {
    //     const RecvData& recv_data = recv_data_ptr[i];
    //     SPDLOG_INFO("{}", zrt::to_str(recv_data));
    //     m_market_info_map[recv_data.market][recv_data.instrument] = recv_data;
    // }
}