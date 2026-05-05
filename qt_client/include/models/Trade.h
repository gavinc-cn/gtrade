#pragma once

#include <QString>
#include <QJsonObject>
#include <cstdint>

class Trade {
public:
    Trade() = default;
    explicit Trade(const QJsonObject& json);

    // Getters
    QString tdno() const { return m_tdno; }
    QString ordno() const { return m_ordno; }
    QString stratId() const { return m_stratId; }
    QString instrument() const { return m_instrument; }
    QString tdSide() const { return m_tdSide; }
    double tdPx() const { return m_tdPx; }
    double tdQty() const { return m_tdQty; }
    QString posSide() const { return m_posSide; }
    int64_t filledTime() const { return m_filledTime; }

    // Setters
    void setTdno(const QString& tdno) { m_tdno = tdno; }
    void setOrdno(const QString& ordno) { m_ordno = ordno; }
    void setStratId(const QString& stratId) { m_stratId = stratId; }
    void setInstrument(const QString& instrument) { m_instrument = instrument; }
    void setTdSide(const QString& tdSide) { m_tdSide = tdSide; }
    void setTdPx(double tdPx) { m_tdPx = tdPx; }
    void setTdQty(double tdQty) { m_tdQty = tdQty; }
    void setPosSide(const QString& posSide) { m_posSide = posSide; }
    void setFilledTime(int64_t filledTime) { m_filledTime = filledTime; }

    // Convert to/from JSON
    QJsonObject toJson() const;
    static Trade fromJson(const QJsonObject& json);

    // Helper methods
    double amount() const { return m_tdPx * m_tdQty; }
    QString formattedFilledTime() const;

private:
    QString m_tdno;         // Trade ID
    QString m_ordno;        // Order ID
    QString m_stratId;      // Strategy name
    QString m_instrument;   // Symbol
    QString m_tdSide;       // B or S
    double m_tdPx{0.0};     // Price
    double m_tdQty{0.0};    // Quantity
    QString m_posSide;      // l, s, or n
    int64_t m_filledTime{0}; // Nanosecond timestamp
};
