#include "models/Strategy.h"
#include <QJsonObject>
#include <QJsonValue>

Strategy::Strategy(const QJsonObject& json) {
    m_id = json["id"].toString();
    m_stratName = json["strat_name"].toString();
    m_status = json["status"].toInt(0);
    m_createTime = json["create_time"].toString();
    m_updateTime = json["update_time"].toString();

    // Parse params
    QJsonObject paramsObj = json["params"].toObject();
    for (const QString& key : paramsObj.keys()) {
        m_params[key] = paramsObj[key].toVariant();
    }

    // Parse indicators
    QJsonObject indicatorsObj = json["indicators"].toObject();
    for (const QString& key : indicatorsObj.keys()) {
        m_indicators[key] = indicatorsObj[key].toVariant();
    }
}

QJsonObject Strategy::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["strat_name"] = m_stratName;
    json["status"] = m_status;
    json["create_time"] = m_createTime;
    json["update_time"] = m_updateTime;

    // Convert params
    QJsonObject paramsObj;
    for (auto it = m_params.begin(); it != m_params.end(); ++it) {
        paramsObj[it.key()] = QJsonValue::fromVariant(it.value());
    }
    json["params"] = paramsObj;

    // Convert indicators
    QJsonObject indicatorsObj;
    for (auto it = m_indicators.begin(); it != m_indicators.end(); ++it) {
        indicatorsObj[it.key()] = QJsonValue::fromVariant(it.value());
    }
    json["indicators"] = indicatorsObj;

    return json;
}

Strategy Strategy::fromJson(const QJsonObject& json) {
    return Strategy(json);
}
