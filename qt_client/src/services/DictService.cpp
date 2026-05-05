#include "services/DictService.h"
#include <QJsonArray>

#include "NetworkManager.h"

DictService& DictService::instance() {
    static DictService instance;
    return instance;
}

DictService::DictService() {
    initializeFallbackDicts();
}

void DictService::initializeFallbackDicts() {
    // Fallback dictionaries - used if API is unavailable
    // EntrustStatus / OrderStatus
    QMap<QString, DictItem> entrustStatus;
    entrustStatus["0"] = {"0", "Submitted", "info"};
    entrustStatus["1"] = {"1", "Partially Filled", "warning"};
    entrustStatus["2"] = {"2", "Filled", "success"};
    entrustStatus["3"] = {"3", "Cancelled", "info"};
    entrustStatus["4"] = {"4", "Rejected", "danger"};
    m_dicts[ENTRUST_STATUS] = entrustStatus;
    m_dicts[ORDER_STATUS] = entrustStatus;

    // BuySellSide / TradeSide
    QMap<QString, DictItem> buySellSide;
    buySellSide["B"] = {"B", "Buy", "success"};
    buySellSide["S"] = {"S", "Sell", "danger"};
    m_dicts[BUY_SELL_SIDE] = buySellSide;
    m_dicts[TRADE_SIDE] = buySellSide;

    // PosSide
    QMap<QString, DictItem> posSide;
    posSide["l"] = {"l", "Long", "success"};
    posSide["s"] = {"s", "Short", "danger"};
    posSide["n"] = {"n", "Net", "info"};
    m_dicts[POS_SIDE] = posSide;

    // InstType
    QMap<QString, DictItem> instType;
    instType["SPOT"] = {"SPOT", "Spot", "info"};
    instType["SWAP"] = {"SWAP", "Perpetual Swap", "warning"};
    instType["FUTURES"] = {"FUTURES", "Futures", "warning"};
    instType["OPTION"] = {"OPTION", "Option", "info"};
    m_dicts[INST_TYPE] = instType;

    // TradeMode
    QMap<QString, DictItem> tradeMode;
    tradeMode["cross"] = {"cross", "Cross", "info"};
    tradeMode["isolated"] = {"isolated", "Isolated", "warning"};
    m_dicts[TRADE_MODE] = tradeMode;

    // PosSource
    QMap<QString, DictItem> posSource;
    posSource["exchange"] = {"exchange", "Exchange", "success"};
    posSource["local"] = {"local", "Local", "info"};
    posSource["reconciled"] = {"reconciled", "Reconciled", "success"};
    m_dicts[POS_SOURCE] = posSource;
}

void DictService::load() {
    // TODO: Implement gRPC dictionary loading when needed
    // For now, just use fallback dictionaries
    qDebug() << "DictService::load() - using fallback dictionaries (gRPC not implemented yet)";
    m_loaded = true;
    emit loaded();

    /* DISABLED - NetworkManager not compatible with GrpcNetworkManager
    // DictService uses HTTP REST API, not gRPC
    NetworkManager::instance().get(
        NetworkManager::Backend::WebServer,
        "/api/dict",
        [this](const QJsonObject& response, bool success, const QString& error) {
            if (success) {
                parseApiResponse(response);
                m_loaded = true;
                emit loaded();
            } else {
                // Use fallback dicts
                m_loaded = true;
                emit loadError(error);
            }
        }
    );
    */
}

void DictService::reload() {
    m_loaded = false;
    load();
}

void DictService::parseApiResponse(const QJsonObject& response) {
    // Expected format:
    // {
    //   "EntrustStatus": {
    //     "0": {"name": "Submitted", "type": "info"},
    //     "1": {"name": "Partially Filled", "type": "warning"},
    //     ...
    //   },
    //   ...
    // }

    for (const QString& dictType : response.keys()) {
        QJsonObject dictObj = response[dictType].toObject();
        QMap<QString, DictItem> items;

        for (const QString& value : dictObj.keys()) {
            QJsonObject itemObj = dictObj[value].toObject();
            DictItem item;
            item.value = value;
            item.name = itemObj["name"].toString(value);
            item.type = itemObj["type"].toString("info");
            items[value] = item;
        }

        m_dicts[dictType] = items;
    }
}

QString DictService::getName(const QString& dictType, const QString& value) const {
    if (m_dicts.contains(dictType) && m_dicts[dictType].contains(value)) {
        return m_dicts[dictType][value].name;
    }
    return value; // Return original value if not found
}

QString DictService::getType(const QString& dictType, const QString& value) const {
    if (m_dicts.contains(dictType) && m_dicts[dictType].contains(value)) {
        return m_dicts[dictType][value].type;
    }
    return "info"; // Default type
}

QList<DictItem> DictService::getDict(const QString& dictType) const {
    if (m_dicts.contains(dictType)) {
        return m_dicts[dictType].values();
    }
    return {};
}

QMap<QString, QString> DictService::getOptions(const QString& dictType) const {
    QMap<QString, QString> options;
    if (m_dicts.contains(dictType)) {
        for (const DictItem& item : m_dicts[dictType].values()) {
            options[item.name] = item.value;
        }
    }
    return options;
}
