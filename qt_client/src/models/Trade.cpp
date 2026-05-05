#include "models/Trade.h"
#include "utils/DateTimeUtils.h"

Trade::Trade(const QJsonObject& json) {
    m_tdno = json["tdno"].toString();
    m_ordno = json["ordno"].toString();
    m_stratId = json["strat_id"].toString();
    m_instrument = json["instrument"].toString();
    m_tdSide = json["td_side"].toString();
    m_tdPx = json["td_px"].toDouble(0.0);
    m_tdQty = json["td_qty"].toDouble(0.0);
    m_posSide = json["pos_side"].toString();
    m_filledTime = json["filled_time"].toVariant().toLongLong(0);
}

QJsonObject Trade::toJson() const {
    QJsonObject json;
    json["tdno"] = m_tdno;
    json["ordno"] = m_ordno;
    json["strat_id"] = m_stratId;
    json["instrument"] = m_instrument;
    json["td_side"] = m_tdSide;
    json["td_px"] = m_tdPx;
    json["td_qty"] = m_tdQty;
    json["pos_side"] = m_posSide;
    json["filled_time"] = QJsonValue::fromVariant(QVariant::fromValue(m_filledTime));
    return json;
}

Trade Trade::fromJson(const QJsonObject& json) {
    return Trade(json);
}

QString Trade::formattedFilledTime() const {
    return DateTimeUtils::formatNanoseconds(m_filledTime);
}
