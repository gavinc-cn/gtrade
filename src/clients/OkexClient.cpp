#include <cryptopp/hmac.h>
#include <cryptopp/sha.h>
#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include "type_define.h"
#include "OkexClient.h"
#include "dict.h"
#include "zrtools/zrt_time-inl.h"
#include "sonic_helper.h"
#include "i_client.h"
#include "http_utils.h"
#include "dict_mapping.h"
#include "my_utc.h"
#include "string_keys.h"

OkexClient::OkexClient(const GTradeConfig& gtrade_cfg, const Account& account):
m_gtrade_cfg(gtrade_cfg),
m_account(account)
{
    if (!m_gtrade_cfg.proxy_config.http.empty()) {
        std::vector<std::string> proxy_vec {};
        boost::split(proxy_vec, m_gtrade_cfg.proxy_config.http, boost::is_any_of(":"));
        if (proxy_vec.size() == 2) {
            m_client.set_proxy(proxy_vec[0], zrt::convert<int>(proxy_vec[1]));
            SPDLOG_INFO("set http proxy={}", m_gtrade_cfg.proxy_config.http);
        }
        else {
            SPDLOG_ERROR("set http proxy({}) failed", m_gtrade_cfg.proxy_config.http);
        }
    }
}

bool OkexClient::HttpGet(std::string endpoint, const Params& params, sonic_json::Document& d) {
    JoinUrl(endpoint, params);
    httplib::Headers headers {};
    headers.emplace("OK-ACCESS-KEY", m_account.key);
    SPDLOG_INFO("[HTTP_REQ] url={}{}", m_base_url, endpoint);
    auto res = m_client.Get(endpoint, headers);
    if (res) {
        if (res->status == 200) {
            SPDLOG_TRACE("[HTTP_RSP] url={}{} status={} body={}",
                m_base_url, endpoint, res->status, res->body);
        } else {
            SPDLOG_ERROR("[HTTP_RSP] url={}{} status={} reason={} headers={} body={}",
                m_base_url, endpoint, res->status, res->reason, zrt::to_str(res->headers), res->body);
            return false;
        }
    }
    else {
        SPDLOG_ERROR("http get failed: {}", GetHttpErrName(res.error()));
        return false;
    }
    if (d.Parse(res->body).HasParseError() || !d.IsObject()) {
        SPDLOG_ERROR("parse json failed: {}", res);
        return false;
    }
    return true;
}

std::string OkexClient::GetSign(const std::string& endpoint, const std::string& secret, const std::string& timestamp, const std::string& method, const std::string& body) {
    std::string raw_data = fmt::format("{}{}{}{}", timestamp, method, endpoint, body);
    CryptoPP::HMAC<CryptoPP::SHA256> hmac(reinterpret_cast<const CryptoPP::byte*>(secret.data()), secret.size());
    std::string digest {};
    CryptoPP::StringSource ss1(raw_data, true, new CryptoPP::HashFilter(hmac, new CryptoPP::StringSink(digest)));
    std::string encoded {};
    CryptoPP::StringSource ss2(digest, true, new CryptoPP::Base64Encoder(new CryptoPP::StringSink(encoded), false));
    return encoded;
}

bool OkexClient::HttpSignedGet(std::string endpoint, const Params& params, sonic_json::Document& d) {
    JoinUrl(endpoint, params);
    std::string ts = zrt::GetTimeStrUTC10();
    std::string sign = GetSign(endpoint, m_account.secret, ts, "GET", "");
    httplib::Headers headers {};
    headers.emplace("Content-Type", "application/json");
    if (zrt::equal(m_account.market, k_okx_dummy)) {
        headers.emplace("x-simulated-trading", "1");
    }
    headers.emplace("OK-ACCESS-KEY", m_account.key);
    headers.emplace("OK-ACCESS-SIGN", sign);
    headers.emplace("OK-ACCESS-TIMESTAMP", ts);
    headers.emplace("OK-ACCESS-PASSPHRASE", m_account.passphrase);
    SPDLOG_INFO("[HTTP_REQ] url={}{}", m_base_url, endpoint);
    auto res = m_client.Get(endpoint, headers);
    if (res) {
        if (res->status == 200) {
            SPDLOG_INFO("[HTTP_RSP] url={}{} status={}", m_base_url, endpoint, res->status);
            SPDLOG_TRACE("[HTTP_RSP] body={}", res->body);
        } else {
            SPDLOG_ERROR("[HTTP_RSP] url={}{} status={} reason={} headers={} body={}",
                m_base_url, endpoint, res->status, res->reason, zrt::to_str(res->headers), res->body);
            return false;
        }
    }
    else {
        SPDLOG_ERROR("http get failed: {}", GetHttpErrName(res.error()));
        return false;
    }
    if (d.Parse(res->body).HasParseError() || !d.IsObject()) {
        SPDLOG_ERROR("parse json failed: {}", res);
        return false;
    }
    return true;
}

bool OkexClient::GetMarketInfo(const TBufferPtr& buf, const MarketInfoQryReq& req) {
    std::string endpoint = "/api/v5/account/instruments";
    Params params {};
    zrt::fill_field(params["instType"], DictInstType2Okx(req.inst_type));
    sonic_json::Document d {};
    try {
        if (HttpSignedGet(endpoint, params, d)) {
            if (d["code"].GetString() != "0") {
                SPDLOG_ERROR("get market_info failed: {}", d["code"].GetString());
                return false;
            }
            auto& data_arr = d["data"];
            for (size_t data_i=0; data_i<data_arr.Size(); ++data_i) {
                auto& data = data_arr[data_i];
                MarketInfo rsp_body {};
                zrt::fill_field(rsp_body.market, req.market);
                zrt::fill_field(rsp_body.inst_type, DictInstTypeFromOkx(data["instType"].GetString()));
                zrt::fill_field(rsp_body.instrument, data["instId"].GetString());
                zrt::fill_field(rsp_body.base_ccy, data["baseCcy"].GetString());
                zrt::fill_field(rsp_body.quote_ccy, data["quoteCcy"].GetString());
                zrt::fill_field(rsp_body.settle_ccy, data["settleCcy"].GetString());
                zrt::fill_field(rsp_body.contract_val, data["ctVal"].GetString());
                zrt::fill_field(rsp_body.contract_multi, data["ctMult"].GetString());
                zrt::fill_field(rsp_body.contract_val_ccy, data["ctValCcy"].GetString());
                zrt::fill_field(rsp_body.list_time, data["listTime"].GetString());
                zrt::fill_field(rsp_body.exp_time, data["expTime"].GetString());
                zrt::fill_field(rsp_body.lever, data["lever"].GetString());
                zrt::fill_field(rsp_body.price_unit, data["tickSz"].GetString());
                zrt::fill_field(rsp_body.amt_unit, data["lotSz"].GetString());
                zrt::fill_field(rsp_body.min_amt, data["minSz"].GetString());
                zrt::fill_field(rsp_body.inst_state, data["state"].GetString());
                zrt::fill_field(rsp_body.inst_id_code, data["instIdCode"].GetInt64());
                buf->Append(rsp_body);
            }
        } else {
            SPDLOG_ERROR("get market_info failed");
            return false;
        }
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("get market_info failed: {}", e.what());
        return false;
    }
    return true;
}

bool OkexClient::GetDepth(TBufferPtr& buf, const DepthQryReq& req) {
    std::string endpoint = "/api/v5/market/books";
    Params params {
            {"instId", req.instrument},
        };
    sonic_json::Document d {};
    try {
        if (HttpGet(endpoint, params, d)) {
            for (size_t data_i=0; data_i<d["data"].Size(); ++data_i) {
                Depth depth {};
                auto& book = d["data"][data_i];
                for (size_t ask_i=0; ask_i<book["asks"].Size(); ++ask_i) {
                    auto& level = book["asks"][ask_i];
                    zrt::fill_field(depth.ask_price[ask_i],  level[0].GetString());
                    zrt::fill_field(depth.ask_amount[ask_i], level[1].GetString());
                }
                for (size_t bid_i=0; bid_i<book["bids"].Size(); ++bid_i) {
                    auto& level = book["bids"][bid_i];
                    zrt::fill_field(depth.bid_price[bid_i], level[0].GetString());
                    zrt::fill_field(depth.bid_amount[bid_i], level[1].GetString());
                }
                zrt::fill_field(depth.datetime, d["ts"].GetString());
                buf->Append(depth);
            }
        } else {
            SPDLOG_ERROR("get depth failed");
            return false;
        }
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("get depth failed: {}", e.what());
        return false;
    }
    return true;
}

bool OkexClient::QryKLine(TBufferPtr& buf, const KLineQryReq& req) {
    std::string endpoint = "/api/v5/market/candles";
    Params params {
        {"instId", req.instrument}
    };
    zrt::fill_if_not_empty(params["bar"], fmt::format("{}{}", req.coefficient, KLineScale2Okx(req.scale)));
    zrt::fill_if_not_empty(params["after"], req.end_time / zrt::kMega); // 不包含
    zrt::fill_if_not_empty(params["before"], req.start_time / zrt::kMega); // 不包含

    constexpr int limit = 100;
    zrt::fill_field(params["limit"], limit);
    sonic_json::Document d {};
    try {
        int result_cnt = 0;
        int64_t ts = 0;
        std::map<int64_t,KLine> kline_map {};
        for (;;) {
            if (!HttpGet(endpoint, params, d)) {
                SPDLOG_ERROR("qry kline failed: {}", d.Dump());
                return false;
            }
            if (d["code"].GetString() != "0") {
                SPDLOG_ERROR("qry kline with err: {}", d.Dump());
                return false;
            }
            auto& data_arr = d["data"];
            result_cnt = data_arr.Size();
            // 接口给出的结果是倒叙
            for (size_t data_i=0; data_i<data_arr.Size(); ++data_i) {
                auto& data = data_arr[data_i];
                KLine rsp_body {};
                zrt::fill_field(rsp_body.ex_time, data[0].GetString());
                rsp_body.ex_time *= zrt::kMega;
                zrt::fill_field(rsp_body.datetime, MyUTC(rsp_body.ex_time, 19).ToFormat());
                zrt::fill_field(rsp_body.local_time, MyUTC().Epoch19());
                zrt::fill_field(rsp_body.market, req.market);
                zrt::fill_field(rsp_body.instrument, req.instrument);
                zrt::fill_field(rsp_body.coefficient, req.coefficient);
                zrt::fill_field(rsp_body.scale, req.scale);
                zrt::fill_field(rsp_body.open, data[1].GetString());
                zrt::fill_field(rsp_body.high, data[2].GetString());
                zrt::fill_field(rsp_body.low, data[3].GetString());
                zrt::fill_field(rsp_body.close, data[4].GetString());
                zrt::fill_field(rsp_body.volume, data[5].GetString());
                ts = rsp_body.ex_time;
                kline_map[rsp_body.ex_time] = std::move(rsp_body);
            }
            if (result_cnt >= limit) {
                zrt::fill_field(params["after"], ts / zrt::kMega);
            } else {
                break;
            }
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
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("process history entrusts data failed: {}", e.what());
        return false;
    }
    return true;
}


bool OkexClient::GetBalance(TBufferPtr& buf, const BalanceQryReq& req) {
    std::string endpoint = "/api/v5/account/balance";
    Params params {
        {"ccy", req.currency},
    };
    sonic_json::Document d {};
    try {
        if (HttpSignedGet(endpoint, params, d)) {
            if (d["code"].GetString() != "0") {
                SPDLOG_ERROR("get balance failed: {}", d["code"].GetString());
                return false;
            }
            if (d["data"].Size() != 1) {
                SPDLOG_ERROR("unexpected data array len: {}", d["data"].Size());
            }
            auto& data = d["data"][0];
            for (size_t detail_i=0; detail_i<data["details"].Size(); ++detail_i) {
                auto& detail = data["details"][detail_i];
                Balance balance {};
                zrt::fill_field(balance.currency, detail["ccy"].GetString());
                zrt::fill_field(balance.available, detail["availEq"].GetString());
                zrt::fill_field(balance.frozen, detail["frozenBal"].GetString());
                zrt::fill_field(balance.total, detail["eq"].GetString());
                buf->Append(balance);
            }
        } else {
            SPDLOG_ERROR("get balance failed");
            return false;
        }
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("get balance failed: {}", e.what());
        return false;
    }
    return true;
}

bool OkexClient::QryEntrust(TBufferPtr& buf, const EntrustQryReq& req) {
    std::string endpoint = "/api/v5/trade/order";
    Params params {
        {"instId", req.instrument},
    };
    if (!zrt::is_empty(req.entno)) params["clOrdId"] = std::to_string(req.entno);
    if (!zrt::is_empty(req.ex_entno)) params["ordId"] = std::to_string(req.ex_entno);
    sonic_json::Document d {};
    try {
        if (HttpSignedGet(endpoint, params, d)) {
            if (d["code"].GetString() != "0") {
                SPDLOG_ERROR("qry entrust failed: {}", d.Dump());
                return false;
            }
            auto& data_arr = d["data"];
            for (size_t data_i=0; data_i<data_arr.Size(); ++data_i) {
                auto& data = data_arr[data_i];
                Order rsp_body {};
                zrt::fill_field(rsp_body.status, DictStatusFromOkx(data["state"].GetString()));
                zrt::fill_field(rsp_body.update_time, data["uTime"].GetString());
                zrt::fill_field(rsp_body.market, m_account.market);
                zrt::fill_field(rsp_body.account_id, m_account.id);
                // zrt::fill_field(rsp_body.inst_type, data["instType"].GetString());
                zrt::fill_field(rsp_body.inst_id, data["instId"].GetString());
                zrt::fill_field(rsp_body.ex_entno, data["ordId"].GetString());
                zrt::fill_field(rsp_body.entno, data["clOrdId"].GetString());
                zrt::fill_field(rsp_body.price, data["px"].GetString());
                zrt::fill_field(rsp_body.amount, data["sz"].GetString());
                zrt::fill_field(rsp_body.filled, data["accFillSz"].GetString());
                // zrt::fill_field(rsp_body.remain, Pridata["ordType"].GetString());
                // zrt::fill_field(rsp_body.draw_amt, Pridata["ordType"].GetString());
                // zrt::fill_field(rsp_body.price_type, Pridata["ordType"].GetString());
                zrt::fill_field(rsp_body.confirm_time, data["cTime"].GetString());
                zrt::fill_field(rsp_body.filled_time, data["fillTime"].GetString());
                if (zrt::equal(rsp_body.status, OrderStatus::_6)) {
                    zrt::fill_field(rsp_body.withdraw_time, rsp_body.update_time);
                }
                buf->Append(rsp_body);
            }
        } else {
            SPDLOG_ERROR("qry entrust failed");
            return false;
        }
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("qry entrust failed: {}", e.what());
        return false;
    }
    return true;
}

void OkexClient::FillEntrust(Order& entrust, const sonic_json::Node& data) {
    zrt::fill_field(entrust.status, DictStatusFromOkx(data["state"].GetString()));
    zrt::fill_field(entrust.update_time, data["uTime"].GetString());
    zrt::fill_field(entrust.market, m_account.market);
    zrt::fill_field(entrust.account_id, m_account.id);
    zrt::fill_field(entrust.inst_type, DictInstTypeFromOkx(data["instType"].GetString()));
    zrt::fill_field(entrust.inst_id, data["instId"].GetString());
    zrt::fill_field(entrust.ex_entno, data["ordId"].GetString());
    zrt::fill_field(entrust.entno, data["clOrdId"].GetString());
    zrt::fill_field(entrust.price, data["px"].GetString());
    zrt::fill_field(entrust.amount, data["sz"].GetString());
    zrt::fill_field(entrust.filled, data["accFillSz"].GetString());
    // zrt::fill_field(entrust.remain, Pridata["ordType"].GetString());
    // zrt::fill_field(entrust.draw_amt, Pridata["ordType"].GetString());
    // zrt::fill_field(entrust.price_type, Pridata["ordType"].GetString());
    zrt::fill_field(entrust.confirm_time, data["cTime"].GetString());
    zrt::fill_field(entrust.filled_time, data["fillTime"].GetString());
    if (zrt::equal(entrust.status, OrderStatus::_6)) {
        zrt::fill_field(entrust.withdraw_time, entrust.update_time);
    }
}

bool OkexClient::QryOpenEntrusts(TBufferPtr& buf, const OpenEntrustsQryReq& req) {
    std::string endpoint = "/api/v5/trade/orders-pending";
    Params params {};
    if (!zrt::is_empty(req.inst_type)) zrt::fill_field(params["instType"], DictInstType2Okx(req.inst_type));
    if (!zrt::is_empty(req.instrument)) zrt::fill_field(params["instId"], req.instrument);
    if (!zrt::is_empty(req.price_type)) zrt::fill_field(params["ordType"], DictPriceType2Okx(req.price_type));
    if (!zrt::is_empty(req.status)) zrt::fill_field(params["state"], DictStatus2Okx(req.status));

    constexpr int limit = 100;
    zrt::fill_field(params["limit"], limit);
    sonic_json::Document d {};
    try {
        int result_cnt = 0;
        int64_t ex_entno = 0;
        for (;;) {
            if (!HttpSignedGet(endpoint, params, d)) {
                SPDLOG_ERROR("qry open entrusts failed: {}", d.Dump());
                return false;
            }
            if (d["code"].GetString() != "0") {
                SPDLOG_ERROR("qry open entrusts with err: {}", d.Dump());
                return false;
            }
            auto& data_arr = d["data"];
            result_cnt = data_arr.Size();
            for (size_t data_i=0; data_i<data_arr.Size(); ++data_i) {
                auto& data = data_arr[data_i];
                Order rsp_body {};
                FillEntrust(rsp_body, data);
                buf->Append(rsp_body);
                ex_entno = rsp_body.ex_entno;
            }
            if (result_cnt >= limit) {
                zrt::fill_field(params["before"], ex_entno);
            } else {
                break;
            }
        }
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("process open entrusts data failed: {}", e.what());
        return false;
    }
    return true;
}

bool OkexClient::QryHisEntrusts(TBufferPtr& buf, const HisEntrustsQryReq& req) {
    std::string endpoint = "/api/v5/trade/orders-history";
    Params params {};
    zrt::fill_if_not_empty(params["instType"], DictInstType2Okx(req.inst_type));
    zrt::fill_if_not_empty(params["instId"], req.instrument);
    zrt::fill_if_not_empty(params["ordType"], DictPriceType2Okx(req.price_type));
    zrt::fill_if_not_empty(params["state"], DictStatus2Okx(req.status));
    zrt::fill_if_not_empty(params["begin"], req.start_time / zrt::kMega);
    zrt::fill_if_not_empty(params["end"], req.end_time / zrt::kMega);

    constexpr int limit = 100;
    zrt::fill_field(params["limit"], limit);
    sonic_json::Document d {};
    std::vector<std::string> inst_type_vec {req.inst_type};
    if (zrt::is_empty(req.inst_type)) {
        inst_type_vec = {k_SPOT, k_MARGIN, k_SWAP, k_FUTURES, k_OPTION};
    }
    try {
        for (const auto& inst_type: inst_type_vec) {
            zrt::fill_field(params["instType"], inst_type);
            int result_cnt = 0;
            int64_t ex_entno = 0;
            for (;;) {
                if (!HttpSignedGet(endpoint, params, d)) {
                    SPDLOG_ERROR("qry history entrusts failed: {}", d.Dump());
                    return false;
                }
                if (d["code"].GetString() != "0") {
                    SPDLOG_ERROR("qry history entrusts with err: {}", d.Dump());
                    return false;
                }
                auto& data_arr = d["data"];
                result_cnt = data_arr.Size();
                for (size_t data_i=0; data_i<data_arr.Size(); ++data_i) {
                    auto& data = data_arr[data_i];
                    Order rsp_body {};
                    FillEntrust(rsp_body, data);
                    buf->Append(rsp_body);
                    ex_entno = rsp_body.ex_entno;
                }
                if (result_cnt >= limit) {
                    zrt::fill_field(params["before"], ex_entno);
                } else {
                    break;
                }
            }
        }
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("process history entrusts data failed: {}", e.what());
        return false;
    }
    return true;
}

