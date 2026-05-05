#pragma once

#include <QString>
#include <QJsonObject>
#include <QMap>
#include <QVariant>

class Strategy {
public:
    Strategy() = default;
    explicit Strategy(const QJsonObject& json);

    // Getters
    QString id() const { return m_id; }
    QString stratName() const { return m_stratName; }
    int status() const { return m_status; }
    QString createTime() const { return m_createTime; }
    QString updateTime() const { return m_updateTime; }

    QMap<QString, QVariant> params() const { return m_params; }
    QMap<QString, QVariant> indicators() const { return m_indicators; }

    QVariant param(const QString& key) const { return m_params.value(key); }
    QVariant indicator(const QString& key) const { return m_indicators.value(key); }

    // Setters
    void setId(const QString& id) { m_id = id; }
    void setStratName(const QString& name) { m_stratName = name; }
    void setStatus(int status) { m_status = status; }
    void setCreateTime(const QString& time) { m_createTime = time; }
    void setUpdateTime(const QString& time) { m_updateTime = time; }
    void setParams(const QMap<QString, QVariant>& params) { m_params = params; }
    void setIndicators(const QMap<QString, QVariant>& indicators) { m_indicators = indicators; }

    // Convert to/from JSON
    QJsonObject toJson() const;
    static Strategy fromJson(const QJsonObject& json);

    // Status helpers
    bool isRunning() const { return m_status == 1; }
    QString statusText() const { return m_status == 1 ? "运行中" : "已停止"; }

private:
    QString m_id;
    QString m_stratName;
    int m_status{0}; // 0 = stopped, 1 = running
    QString m_createTime;
    QString m_updateTime;
    QMap<QString, QVariant> m_params;
    QMap<QString, QVariant> m_indicators;
};
