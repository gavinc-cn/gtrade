#pragma once

#include <QString>
#include <QJsonObject>

class Position {
public:
    Position() = default;
    explicit Position(const QJsonObject& json);

    // Getters
    QString market() const { return m_market; }
    QString accountId() const { return m_accountId; }
    QString instrument() const { return m_instrument; }
    QString posSide() const { return m_posSide; }
    QString instType() const { return m_instType; }
    double available() const { return m_available; }
    double avgPx() const { return m_avgPx; }
    double upl() const { return m_upl; }
    double uplRatio() const { return m_uplRatio; }
    double notionalUsd() const { return m_notionalUsd; }
    QString marginMode() const { return m_marginMode; }
    QString posSource() const { return m_posSource; }
    QString datetime() const { return m_datetime; }
    QString portfolio() const { return m_portfolio; }
    bool isInconsistent() const { return m_isInconsistent; }

    // Setters
    void setMarket(const QString& market) { m_market = market; }
    void setAccountId(const QString& accountId) { m_accountId = accountId; }
    void setInstrument(const QString& instrument) { m_instrument = instrument; }
    void setPosSide(const QString& posSide) { m_posSide = posSide; }
    void setInstType(const QString& instType) { m_instType = instType; }
    void setAvailable(double available) { m_available = available; }
    void setAvgPx(double avgPx) { m_avgPx = avgPx; }
    void setUpl(double upl) { m_upl = upl; }
    void setUplRatio(double uplRatio) { m_uplRatio = uplRatio; }
    void setNotionalUsd(double notionalUsd) { m_notionalUsd = notionalUsd; }
    void setMarginMode(const QString& marginMode) { m_marginMode = marginMode; }
    void setPosSource(const QString& posSource) { m_posSource = posSource; }
    void setDatetime(const QString& datetime) { m_datetime = datetime; }
    void setPortfolio(const QString& portfolio) { m_portfolio = portfolio; }
    void setIsInconsistent(bool isInconsistent) { m_isInconsistent = isInconsistent; }

    // Convert to/from JSON
    QJsonObject toJson() const;
    static Position fromJson(const QJsonObject& json);

    // Helper methods
    QString uplRatioPercent() const;
    bool isProfitable() const { return m_upl > 0; }

private:
    QString m_market;
    QString m_accountId;
    QString m_instrument;
    QString m_posSide;      // l, s, or n
    QString m_instType;
    double m_available{0.0};
    double m_avgPx{0.0};
    double m_upl{0.0};      // Unrealized P&L
    double m_uplRatio{0.0}; // P&L ratio (0-1)
    double m_notionalUsd{0.0};
    QString m_marginMode;
    QString m_posSource;
    QString m_datetime;
    QString m_portfolio;    // For portfolio positions
    bool m_isInconsistent{false};
};
