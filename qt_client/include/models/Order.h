#pragma once

#include <QString>
#include <QJsonObject>
#include <cstdint>

class Order {
public:
    Order() = default;
    explicit Order(const QJsonObject& json);

    // Getters
    QString entno() const { return m_entno; }
    QString policyNo() const { return m_policyNo; }
    QString instId() const { return m_instId; }
    QString bsSide() const { return m_bsSide; }
    double price() const { return m_price; }
    double amount() const { return m_amount; }
    double filled() const { return m_filled; }
    QString status() const { return m_status; }
    int64_t entTime() const { return m_entTime; }

    // Setters
    void setEntno(const QString& entno) { m_entno = entno; }
    void setPolicyNo(const QString& policyNo) { m_policyNo = policyNo; }
    void setInstId(const QString& instId) { m_instId = instId; }
    void setBsSide(const QString& bsSide) { m_bsSide = bsSide; }
    void setPrice(double price) { m_price = price; }
    void setAmount(double amount) { m_amount = amount; }
    void setFilled(double filled) { m_filled = filled; }
    void setStatus(const QString& status) { m_status = status; }
    void setEntTime(int64_t entTime) { m_entTime = entTime; }

    // Convert to/from JSON
    QJsonObject toJson() const;
    static Order fromJson(const QJsonObject& json);

    // Helper methods
    bool canCancel() const;
    QString formattedEntTime() const;

private:
    QString m_entno;        // Order ID
    QString m_policyNo;     // Strategy name
    QString m_instId;       // Symbol
    QString m_bsSide;       // B or S
    double m_price{0.0};
    double m_amount{0.0};   // Quantity
    double m_filled{0.0};   // Filled quantity
    QString m_status;       // Order status code
    int64_t m_entTime{0};   // Nanosecond timestamp
};
