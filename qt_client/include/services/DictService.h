#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QJsonObject>
#include <QVariant>

struct DictItem {
    QString value;
    QString name;
    QString type; // success, warning, danger, info
};

class DictService : public QObject {
    Q_OBJECT

public:
    static DictService& instance();

    // Load dictionaries from API
    void load();
    void reload();

    // Get translated name for a value
    QString getName(const QString& dictType, const QString& value) const;

    // Get tag type for styling
    QString getType(const QString& dictType, const QString& value) const;

    // Get all items for a dictionary type
    QList<DictItem> getDict(const QString& dictType) const;

    // Get options for dropdown (name -> value mapping)
    QMap<QString, QString> getOptions(const QString& dictType) const;

    bool isLoaded() const { return m_loaded; }

signals:
    void loaded();
    void loadError(const QString& error);

private:
    DictService();
    ~DictService() = default;
    DictService(const DictService&) = delete;
    DictService& operator=(const DictService&) = delete;

    void initializeFallbackDicts();
    void parseApiResponse(const QJsonObject& response);

    QMap<QString, QMap<QString, DictItem>> m_dicts;
    bool m_loaded{false};

    // Dictionary type names
    static constexpr const char* ENTRUST_STATUS = "EntrustStatus";
    static constexpr const char* ORDER_STATUS = "OrderStatus";
    static constexpr const char* BUY_SELL_SIDE = "BuySellSide";
    static constexpr const char* TRADE_SIDE = "TradeSide";
    static constexpr const char* POS_SIDE = "PosSide";
    static constexpr const char* INST_TYPE = "InstType";
    static constexpr const char* TRADE_MODE = "TradeMode";
    static constexpr const char* POS_SOURCE = "PosSource";
};
