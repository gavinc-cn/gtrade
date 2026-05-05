#include "HuobiClient.h"
#include "restful.h"
#include "signature.h"
#include "misc.h"
#include "str_utils.h"
#include "http_client.h"

struct PlaceOrderRequest {
    long accountId;
    std::string symbol;
    std::string type;
    std::string amount;
    std::string price;
    std::string clientOrderId;
    std::string stopPrice;
    std::string source;
    std::string operator_;
};

HuobiClient::HuobiClient(const std::string &apiKey, const std::string &apiSecret):
m_api_key(apiKey),
m_api_secret(apiSecret)
{
}

std::string HuobiClient::getExSymbol(const std::string &symbol) {
    std::vector<string> elements = utils::split(symbol, '/');
    return utils::lower(elements[0]) + utils::lower(elements[1]);
}

Ohlcv HuobiClient::getOhlcv(const std::string &symbol, const std::string &timeframe, const std::string &since,
                           const std::string &limit) {
    return Ohlcv();
}

MarketInfo HuobiClient::GetMarketInfo(const std::string &symbol) {

    return MarketInfo();
}

Ticker HuobiClient::getTicker(const std::string &symbol)
{
    return Ticker();
}

Depth HuobiClient::GetDepth(const std::string &symbol, const int &limit)
{
    std::string endpoint = "/market/depth";
    Params params = {
            {"symbol", getExSymbol(symbol)},
            {"depth", to_string(limit)},
            {"type", "step0"}
    };
    std::string response = httpRequest(endpoint, params, GET);
    assert(!response.empty());
    rapidjson::Document d;
    rapidjson::Value &asks = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(response.c_str())["tick"]["asks"];
    rapidjson::Value &bids = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(response.c_str())["tick"]["bids"];
    Depth depth;
    depth.ex_time = std::stol(d.Parse<rapidjson::kParseNumbersAsStringsFlag>(response.c_str())["ts"].GetString());
//    for (int i = 0; i < asks.Size(); i++) {
//        depth.asks.push_back(vector<double>({stod(asks[i][0].GetString()), stod(asks[i][1].GetString())}));
//    }
//    for (int i = 0; i < bids.Size(); i++) {
//        depth.bids.push_back(vector<double>({stod(bids[i][0].GetString()), stod(bids[i][1].GetString())}));
//    }
//    depth.ask1 = depth.asks[0][0];
//    depth.bid1 = depth.bids[0][0];
//    depth.mid = (depth.ask1 + depth.bid1) / 2;
    return depth;
}

std::string HuobiClient::getAccountId()
{
    if (!_accountId.empty()) {return _accountId;}
    std::string endpoint = "/v1/account/accounts";
    Params params;
    Body body;
    std::string response = signedRequest(endpoint, params, body, GET);
    rapidjson::Document d;
    rapidjson::Value &resJson = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(response.c_str());
    _accountId = resJson["data"][0]["id"].GetString();
    return _accountId;
}

Balance HuobiClient::GetBalance(const std::string& symbol)
{
    std::string accountId = getAccountId();
    std::string endpoint = "/v1/account/accounts/" + accountId + "/balance";
    Params params;
    Body body;
    std::string response = signedRequest(endpoint, params, body, GET);
    rapidjson::Document d;
    rapidjson::Value &res_json = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(response.c_str());
    rapidjson::Value &data = res_json["data"]["list"];
    Balance balance;
    for (auto i=data.Begin(); i!=data.End(); i++)
    {
        std::string exCurrency = (*i)["currency"].GetString();
        std::string currency = utils::upper(exCurrency);
        double free = 0.0, used = 0.0;
        if ((*i)["type"].GetString() == "trade")
        {
            free = std::stod((*i)["balance"].GetString());
        }
        else if ((*i)["type"].GetString() == "frozen")
        {
            used = std::stod((*i)["balance"].GetString());
        }
        double total = free + used;
//        balance.content[currency]["free"] = free;
//        balance.content[currency]["used"] = used;
//        balance.content[currency]["total"] = total;
//        balance.content["free"][currency] = free;
//        balance.content["used"][currency] = used;
//        balance.content["total"][currency] = total;
    }
//    balance.timestamp = getEpoch13();
//    balance.datetime = getUTCStr();
    return balance;
}

Order HuobiClient::createOrder(const Symbol &symbol, OrderType type, Side side, Amount amount, Price price, bool testMode)
{
    std::string endpoint = "/v1/order/orders/place";
    Params params;
    Body body = {
            {"account-id", getAccountId()},
            {"symbol", getExSymbol(symbol)},
            {"type", "buy-limit"},
            {"amount", utils::roundToStr(amount, 2)},
    };
    if (price!=0) { body["price"] = utils::roundToStr(price, 2); }
    std::string response = signedRequest(endpoint, params, body, POST);
    rapidjson::Document d;
    rapidjson::Value &res_json = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(response.c_str());
    Order order;

//    order.timestamp = getEpoch13();
//    order.datetime = getUTCStr();
//    order.id = res_json["data"].GetString();
//    order.symbol = symbol;
    order.type = type;
    order.side = side;
    order.amount = amount;
    order.price = price;
    return order;
}

Order HuobiClient::getOrder(const std::string &symbol, const std::string &id)
{
    std::string endpoint = "/v1/order/orders/" + id;
    Params params;
    Body body;
    std::string response = signedRequest(endpoint, params, body, GET);
    rapidjson::Document d;
    rapidjson::Value &res_json = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(response.c_str());
    auto &data = res_json["data"];
    return parseOrder(data);
}

Order HuobiClient::parseOrder(rapidjson::Value &data) {
    Order order;
    auto exType = utils::split(data["type"].GetString(), '-');
//    order.timestamp = getEpoch13();
//    order.datetime = getUTCStr();
//    order.id = data["id"].GetString();
//    order.symbol = data["symbol"].GetString();
//    order.type = order.getType(exType[1]);
//    order.side = order.getSide(exType[0]);
//    order.amount = std::stod(data["amount"].GetString());
//    order.price = std::stod(data["price"].GetString());
//    order.filled = std::stod(data["field-amount"].GetString());
//    order.remaining = order.amount - order.filled;
//    order.status = data["state"].GetString();
    return order;
}

bool HuobiClient::cancelOrder(const std::string &symbol, const std::string &id)
{
    std::string endpoint = "/v1/order/orders/" + id + "/submitcancel";
    Params params;
    Body body;
    std::string response = signedRequest(endpoint, params, body, POST);
    rapidjson::Document d;
    rapidjson::Value &res_json = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(response.c_str());
    return false;
}

std::vector<Order> HuobiClient::getOpenOrders(const std::string &symbol)
{
    std::string endpoint = "/v1/order/openOrders";
    Params params;
    Body body = {
            {"account-id", getAccountId()},
            {"symbol", getExSymbol(symbol)},
            {"size", "500"},
    };
    std::string response = signedRequest(endpoint, params, body, GET);
    rapidjson::Document d;
    rapidjson::Value &res_json = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(response.c_str());
    std::vector<Order> result;
    auto &data = res_json["data"];
    for (auto i=data.Begin(); i!=data.End(); i++)
    {
        auto exType = utils::split((*i)["type"].GetString(), '-');

        Order order;
//        order.timestamp = getEpoch13();
//        order.datetime = getUTCStr();
//        order.id = (*i)["id"].GetString();
//        order.symbol = symbol;
//        order.type = order.getType(exType[1]);
//        order.side = order.getSide(exType[0]);
//        order.amount = std::stod((*i)["amount"].GetString());
//        order.price = std::stod((*i)["price"].GetString());
//        order.filled = std::stod((*i)["filled-amount"].GetString());
//        order.remaining = order.amount - order.filled;
//        order.status = (*i)["state"].GetString();
        result.push_back(order);
    }
    return result;
}

void HuobiClient::cancelOpenOrders(const std::string &symbol)
{
    std::string endpoint = "/v1/order/orders/batchCancelOpenOrders";
    Params params;
    Body body = {
            {"account-id", getAccountId()},
            {"symbol", getExSymbol(symbol)},
    };
    std::string response = signedRequest(endpoint, params, body, POST);
    rapidjson::Document d;
    rapidjson::Value &res_json = d.Parse<rapidjson::kParseNumbersAsStringsFlag>(response.c_str());
}

std::string HuobiClient::httpRequest(Endpoint& endpoint, Params& params, Method& method)
{
    std::string url = m_base_url + endpoint;
    Headers headers;
    HttpClient httpClient;
    std::string response = httpClient.get(url, params, headers);
    if (response.empty())
    {
        cerr << "the response is empty" << endl;
        throw std::exception();
    }
    return response;
}

std::string HuobiClient::signedRequest(Endpoint& endpoint, Params& params, Body& body, Method& method)
{
    std::string url = m_base_url + endpoint;
    Headers headers;
    HttpClient httpClient;

    time_t nowtime = time(0);
    struct tm *utc = gmtime(&nowtime);
    char buf[50];
    strftime(buf, 50, "%Y-%m-%dT%H:%M:%S", utc);
    CURL *curl = curl_easy_init();
    std::string timestamp = Rest::encode(buf);
//    timestamp = "2020-08-29T08%3A17%3A40";
    std::string signature;
    params["AccessKeyId"] = m_api_key;
    params["SignatureMethod"] = "HmacSHA256";
    params["SignatureVersion"] = "2";
    params["Timestamp"] = timestamp;
    signature = sign(endpoint, params, method);
    params["Signature"] = signature;
    std::string response = httpClient.request(method, url, params, body, headers);
    if (response.empty())
    {
        cerr << "the response is empty" << endl;
        throw std::exception();
    }
    return response;
}

std::string HuobiClient::sign(const std::string& endpoint, Params& params, Method& method)
{
    std::string paramsStr;
    const std::string domainName = utils::split(m_base_url, "//")[1];
    paramsStr.append(method).append("\n")
            .append(HOST).append("\n")
            .append(endpoint).append("\n");
    std::string spliceStr;
    for (auto iter = params.begin(); iter != params.end(); ++iter) {
        spliceStr.append(iter->first).append("=").append(iter->second);
        if (iter != --params.end()) {
            spliceStr.append("&");
        }
    }
    paramsStr.append(spliceStr);
    cout << "preSignStr:" << paramsStr << endl;
    unsigned int md_len;
    unsigned char *str = HMAC(EVP_sha256(),
                              m_api_secret.c_str(), m_api_secret.size(),
                              reinterpret_cast<const unsigned char *>(paramsStr.c_str()),
                              paramsStr.size(),
                              nullptr,
                              &md_len);
    //signature buf afer hmac and base64
    char signature[100];
    EVP_EncodeBlock(reinterpret_cast<unsigned char *>(signature), str, static_cast<int>(md_len));
    return Rest::encode(signature);

}




