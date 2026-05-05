#include "models/Position.h"

Position::Position(const QJsonObject& json) {
    m_market = json["market"].toString();
    m_accountId = json["account_id"].toString();
    m_instrument = json["instrument"].toString();
    m_posSide = json["pos_side"].toString();
    m_instType = json["inst_type"].toString();
    m_available = json["available"].toDouble(0.0);
    m_avgPx = json["avg_px"].toDouble(0.0);
    m_upl = json["upl"].toDouble(0.0);
    m_uplRatio = json["upl_ratio"].toDouble(0.0);
    m_notionalUsd = json["notional_usd"].toDouble(0.0);
    m_marginMode = json["margin_mode"].toString();
    m_posSource = json["pos_source"].toString();
    m_datetime = json["datetime"].toString();
    m_portfolio = json["portfolio"].toString();
    m_isInconsistent = json["is_inconsistent"].toBool(false);
}

QJsonObject Position::toJson() const {
    QJsonObject json;
    json["market"] = m_market;
    json["account_id"] = m_accountId;
    json["instrument"] = m_instrument;
    json["pos_side"] = m_posSide;
    json["inst_type"] = m_instType;
    json["available"] = m_available;
    json["avg_px"] = m_avgPx;
    json["upl"] = m_upl;
    json["upl_ratio"] = m_uplRatio;
    json["notional_usd"] = m_notionalUsd;
    json["margin_mode"] = m_marginMode;
    json["pos_source"] = m_posSource;
    json["datetime"] = m_datetime;
    json["portfolio"] = m_portfolio;
    json["is_inconsistent"] = m_isInconsistent;
    return json;
}

Position Position::fromJson(const QJsonObject& json) {
    return Position(json);
}

QString Position::uplRatioPercent() const {
    return QString::number(m_uplRatio * 100.0, 'f', 2) + "%";
}
