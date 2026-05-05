#include "models/Order.h"
#include "utils/DateTimeUtils.h"

Order::Order(const QJsonObject& json) {
    m_entno = json["entno"].toString();
    m_policyNo = json["policy_no"].toString();
    m_instId = json["inst_id"].toString();
    m_bsSide = json["bs_side"].toString();
    m_price = json["price"].toDouble(0.0);
    m_amount = json["amount"].toDouble(0.0);
    m_filled = json["filled"].toDouble(0.0);
    m_status = json["status"].toString();
    m_entTime = json["ent_time"].toVariant().toLongLong(0);
}

QJsonObject Order::toJson() const {
    QJsonObject json;
    json["entno"] = m_entno;
    json["policy_no"] = m_policyNo;
    json["inst_id"] = m_instId;
    json["bs_side"] = m_bsSide;
    json["price"] = m_price;
    json["amount"] = m_amount;
    json["filled"] = m_filled;
    json["status"] = m_status;
    json["ent_time"] = QJsonValue::fromVariant(QVariant::fromValue(m_entTime));
    return json;
}

Order Order::fromJson(const QJsonObject& json) {
    return Order(json);
}

bool Order::canCancel() const {
    // Can only cancel orders with status 0 (submitted) or 1 (partially filled)
    return m_status == "0" || m_status == "1";
}

QString Order::formattedEntTime() const {
    return DateTimeUtils::formatNanoseconds(m_entTime);
}
