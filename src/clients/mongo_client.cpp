#include <cryptopp/hmac.h>
#include <cryptopp/sha.h>
#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include "type_define.h"
#include "mongo_client.h"
#include "dict.h"
#include "zrtools/zrt_time-inl.h"
#include "sonic_helper.h"
#include "i_client.h"
#include "http_utils.h"
#include "dict_mapping.h"
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/cursor.hpp>


MongoClient::MongoClient(const Account& account):
m_account(account)
{
    Connect();
}

bool MongoClient::Connect() {
    if (m_client) return true;
    try {
        // 初始化MongoDB实例
        static mongocxx::instance instance{};
        
        // 创建MongoDB客户端连接
        mongocxx::uri uri{"mongodb://localhost:27017"};
        m_client = std::make_unique<mongocxx::client>(uri);
        
        // 选择数据库
        m_database = (*m_client)["default"];
        
        return true;
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("MongoDB connection failed: {}", e.what());
        return false;
    }
}

bool MongoClient::QryKLine(TBufferPtr& buf, const KLineQryReq& req) {
    using bsoncxx::builder::stream::document;
    using bsoncxx::builder::stream::open_document;
    using bsoncxx::builder::stream::close_document;
    using bsoncxx::builder::stream::finalize;
    
    try {
        // 构建集合名称
        std::string collection_name = fmt::format("kline_{}{}", req.coefficient, req.scale);
        auto collection = m_database[collection_name];
        
        // 构建查询条件
        auto filter = document{}
            << "ex_time" << open_document
                << "$gte" << req.start_time
                << "$lte" << req.end_time
            << close_document
            << finalize;
        
        // 执行查询
        auto cursor = collection.find(filter.view());
        
        std::map<int64_t, KLine> kline_map {};
        
        for (auto&& doc : cursor) {
            KLine kline {};
            
            // 从MongoDB文档中提取数据
            auto view = doc;
            
            if (view["market"]) {
                zrt::fill_field(kline.market, view["market"].get_utf8().value.to_string());
            }
            if (view["instrument"]) {
                zrt::fill_field(kline.instrument, view["instrument"].get_utf8().value.to_string());
            }
            if (view["ex_time"]) {
                zrt::fill_field(kline.ex_time, view["ex_time"].get_int64().value);
            }
            if (view["ex_time_h"]) {
                zrt::fill_field(kline.datetime, view["ex_time_h"].get_date().value);
            }
            if (view["open"]) {
                zrt::fill_field(kline.open, view["open"].get_double().value);
            }
            if (view["high"]) {
                zrt::fill_field(kline.high, view["high"].get_double().value);
            }
            if (view["low"]) {
                zrt::fill_field(kline.low, view["low"].get_double().value);
            }
            if (view["close"]) {
                zrt::fill_field(kline.close, view["close"].get_double().value);
            }
            if (view["volume"]) {
                zrt::fill_field(kline.volume, view["volume"].get_double().value);
            }
            
            zrt::fill_field(kline.coefficient, req.coefficient);
            zrt::fill_field(kline.scale, req.scale);
            
            kline_map[kline.ex_time] = std::move(kline);
        }
        
        // 构建返回结果
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
        
        return true;
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("process data failed: {}", e.what());
        return false;
    }
}