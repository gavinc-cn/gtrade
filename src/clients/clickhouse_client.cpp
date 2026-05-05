#include <cryptopp/hmac.h>
#include <cryptopp/sha.h>
#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include "type_define.h"
#include "clickhouse_client.h"
#include "dict.h"
#include "zrtools/zrt_time-inl.h"
#include "sonic_helper.h"
#include "i_client.h"
#include "http_utils.h"
#include "dict_mapping.h"


ClickhouseClient::ClickhouseClient(const Account& account):
m_account(account)
{
    Connect();
}

bool ClickhouseClient::Connect() {
    if (m_client) return true;
    m_client = std::make_unique<clickhouse::Client>(clickhouse::ClientOptions()
        .SetHost("localhost")      // ClickHouse服务器地址
        .SetPort(9000)             // 默认端口
        .SetUser("default")
        .SetPassword("")
        .SetDefaultDatabase("default")); // 数据库名
    return true;
}

bool ClickhouseClient::QryKLine(TBufferPtr& buf, const KLineQryReq& req) {
    using clickhouse::ColumnString;
    using clickhouse::ColumnInt64;
    using clickhouse::ColumnFloat64;
    using clickhouse::ColumnDateTime;
    try {
        m_client->Select(fmt::format("SELECT market, instrument, ex_time, ex_time_h, open, high, low, close, volume FROM kline_{}{} final where ex_time >= {} and ex_time <= {}", req.coefficient, req.scale, req.start_time, req.end_time),
            [&buf,&req](const clickhouse::Block& block) {
                std::map<int64_t,KLine> kline_map {};
                for (size_t i = 0; i < block.GetRowCount(); ++i) {
                    KLine kline {};
                    zrt::fill_field(kline.market, block[0]->As<ColumnString>()->At(i));
                    zrt::fill_field(kline.instrument, block[1]->As<ColumnString>()->At(i));
                    zrt::fill_field(kline.ex_time, block[2]->As<ColumnInt64>()->At(i));
                    zrt::fill_field(kline.datetime, block[3]->As<ColumnDateTime>()->At(i));
                    zrt::fill_field(kline.open, block[4]->As<ColumnFloat64>()->At(i));
                    zrt::fill_field(kline.high, block[5]->As<ColumnFloat64>()->At(i));
                    zrt::fill_field(kline.low, block[6]->As<ColumnFloat64>()->At(i));
                    zrt::fill_field(kline.close, block[7]->As<ColumnFloat64>()->At(i));
                    zrt::fill_field(kline.volume, block[8]->As<ColumnFloat64>()->At(i));
                    zrt::fill_field(kline.coefficient, req.coefficient);
                    zrt::fill_field(kline.scale, req.scale);
                    // print(kline.market);
                    kline_map[kline.ex_time] = std::move(kline);
                }

                KLineRange kline_range {};
                zrt::fill_field(kline_range.req_id, req.req_id);
                zrt::fill_field(kline_range.market, req.market);
                zrt::fill_field(kline_range.instrument, req.instrument);
                zrt::fill_field(kline_range.coefficient, req.coefficient);
                zrt::fill_field(kline_range.scale, req.scale);
                zrt::fill_field(kline_range.start_time, req.start_time);
                zrt::fill_field(kline_range.end_time, req.end_time);
                zrt::fill_field(kline_range.count, kline_map.size());
                buf->Append(kline_range);
                for (const auto& [ex_time, kline] : kline_map) {
                    buf->Append(kline);
                }
                SPDLOG_INFO("got {} {}{} klines for {} in {}", kline_range.count, kline_range.coefficient, kline_range.scale, kline_range.instrument, kline_range.market);
            });
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("process data failed: {}", e.what());
        return false;
    }
    return true;
}
