//
// Created by dell on 2025/2/15.
//


#include "huobi_ws.h"

void HuobiWs::subscribe() {
    SPDLOG_INFO("subscribe");
    std::stringstream val;

    while (!IsOpen()) {
        SPDLOG_INFO("wait for HuobiWs ready");
        sleep(1);
    }
    val.str("market.btcusdt.depth.step0");
    std::string topic = "market.btcusdt.depth.step0";

    rapidjson::StringBuffer strBuf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(strBuf);
    writer.StartObject();
    writer.Key("sub");
    writer.String(topic.c_str());
    writer.Key("id");
    std::string id = "1";
    writer.String(id.c_str());
    writer.EndObject();

    SPDLOG_INFO("{}", strBuf.GetString());
    Send(strBuf.GetString());
}

void HuobiWs::on_message(websocketpp::connection_hdl, client::message_ptr msg) {
    std::string content = msg->get_payload();
    char sbuf[BUFF] {};
    gzDecompress(content.c_str(), content.size(), sbuf, BUFF);
//    cout << sbuf << endl;
    std::string msg_str(sbuf);
    if (msg_str.empty()) {
        SPDLOG_ERROR("empty message");
        return;
    }
    rapidjson::Document d {};
    rapidjson::Value &data = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(msg_str.c_str());
    if (data.HasMember("ch")
        && utils::split(data["ch"].GetString(), '.')[2] == "depth"
        && data.HasMember("tick"))
    {
        rapidjson::Value &asks = data["tick"]["asks"];
        rapidjson::Value &bids = data["tick"]["bids"];
        Depth depth {};
        zrt::fill_field(depth.ex_time, data["ts"]);
        depth.ex_time *= zrt::kMega;
        for (size_t i = 0; i < asks.Size() && i < 10; i++) {
            zrt::fill_field(depth.ask_price[i], asks[i][0]);
            zrt::fill_field(depth.ask_amount[i], asks[i][1]);
        }
        for (size_t i = 0; i < bids.Size() && i < 10; i++) {
            zrt::fill_field(depth.bid_price[i], bids[i][0]);
            zrt::fill_field(depth.bid_amount[i], bids[i][1]);
        }
        SPDLOG_INFO("{}", zrt::to_str(depth));
    }

    // std::cout << data.HasMember("tick") << std::endl;
//    rapidjson::Value &asks = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(msg_str.c_str())["tick"]["asks"];
//    rapidjson::Value &bids = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(response.c_str())["tick"]["bids"];
//    Depth depth;
//    depth.timestamp = stol(d.Parse<rapidjson::kParseNumbersAsStringsFlag>(response.c_str())["ts"].GetString());
//    for (int i = 0; i < asks.Size(); i++) {
//        depth.asks.push_back(std::vector<double>({stod(asks[i][0].GetString()), stod(asks[i][1].GetString())}));
//    }
//    for (int i = 0; i < bids.Size(); i++) {
//        depth.bids.push_back(std::vector<double>({stod(bids[i][0].GetString()), stod(bids[i][1].GetString())}));
//    }
//    depth.ask1 = depth.asks[0][0];
//    depth.bid1 = depth.bids[0][0];
//    depth.mid = (depth.ask1 + depth.bid1) / 2;

}