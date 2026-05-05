
#ifndef GTRADE_HUOBICLIENT_H
#define GTRADE_HUOBICLIENT_H

#include "pch.h"
//#include "../utils/signature.h"
//#include "../utils/restful.h"
//#include "../utils/http_client.h"
//#include "../utils/str_utils.h"
//#include "../utils/kronos.h"
#include "BaseClient.h"
#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "http_utils.h"

class HuobiClient: public BaseClient
{
public:
    HuobiClient(const std::string& apiKey, const std::string& apiSecret);

    std::string getExSymbol(const std::string& symbol);
    Ohlcv getOhlcv(const std::string& symbol, const std::string& timeframe, const std::string& since, const std::string& limit) override ;
    MarketInfo GetMarketInfo(const std::string& symbol);
    Ticker getTicker(const std::string& symbol) override;
    Depth GetDepth(const std::string& symbol, const int& limit) override;
    std::string getAccountId();
    Balance GetBalance(const std::string& symbol) override;
    Order createOrder(const Symbol &symbol, OrderType type, Side side, Amount amount, Price price, bool testMode) override;
    Order getOrder(const std::string& symbol, const std::string& id) override;
    static Order parseOrder(rapidjson::Value& data);
    bool cancelOrder(const std::string& symbol, const std::string& id) override;
    std::vector<Order> getOpenOrders(const std::string& symbol) override;
    void cancelOpenOrders(cstr symbol) override;
    std::string httpRequest(Endpoint& endpoint, Params& params, Method& method);
    std::string signedRequest(Endpoint& endpoint, Params& params, Body& body, Method& method);
    std::string sign(const std::string& endpoint, Params& params, Method& method);
private:
    std::string m_base_url = "https://api.huobi.pro";
    std::string m_api_key {};
    std::string m_api_secret {};
    httplib::Client m_client {m_base_url};
    std::string _accountId;
};


#endif //GTRADE_HUOBICLIENT_H
