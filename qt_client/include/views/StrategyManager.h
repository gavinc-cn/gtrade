#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QComboBox>
#include <QTimer>
#include <QList>
#include <QMap>
#include "models/Strategy.h"
#include "models/Order.h"
#include "models/Trade.h"
#include "models/Position.h"

class StrategyManager : public QWidget {
    Q_OBJECT

public:
    explicit StrategyManager(QWidget* parent = nullptr);

private slots:
    void onTemplateTabChanged(int index);
    void onRefreshClicked();
    void onAddStrategyClicked();
    void onBatchStartClicked();
    void onBatchStopClicked();
    void onBatchRestartClicked();
    void onBatchDeleteClicked();
    void onAutoRefreshChanged(int index);
    void onAutoRefreshTimeout();
    void onTableCellClicked(int row, int column);
    void onStrategyContextMenu(const QPoint& pos);
    void onStrategyStart();
    void onStrategyStop();
    void onStrategyRestart();
    void onStrategyDelete();
    void onDetailsTabChanged(int index);

private:
    void setupUI();
    void loadTemplates();
    void loadStrategies(const QString& templateName);
    void populateStrategyTable(const QList<Strategy>& strategies);
    void updateStrategyColumns(const QString& templateName);
    void showStrategyDetails(const Strategy& strategy);
    void loadStrategyOrders(const QString& strategyId);
    void loadStrategyTrades(const QString& strategyId);
    void loadStrategyPositions(const QString& strategyId);
    void performStrategyAction(const QString& action, const QList<QString>& strategyIds);
    QList<QString> getSelectedStrategyIds() const;

    QTabWidget* m_templateTabs{nullptr};
    QTableWidget* m_strategyTable{nullptr};
    QPushButton* m_refreshButton{nullptr};
    QPushButton* m_addButton{nullptr};
    QPushButton* m_batchStartButton{nullptr};
    QPushButton* m_batchStopButton{nullptr};
    QPushButton* m_batchRestartButton{nullptr};
    QPushButton* m_batchDeleteButton{nullptr};
    QComboBox* m_autoRefreshCombo{nullptr};
    QTimer* m_autoRefreshTimer{nullptr};

    // Strategy details panel
    QWidget* m_detailsPanel{nullptr};
    QTabWidget* m_detailsTabs{nullptr};
    QTableWidget* m_ordersTable{nullptr};
    QTableWidget* m_tradesTable{nullptr};
    QTableWidget* m_positionsTable{nullptr};

    QStringList m_templateNames;
    QMap<QString, QList<Strategy>> m_strategies;
    QMap<QString, QStringList> m_templateColumns;
    QString m_currentTemplate;
    int m_selectedStrategyRow{-1};
    QString m_selectedStrategyId;

    // Details lazy loading flags
    bool m_ordersLoaded{false};
    bool m_tradesLoaded{false};
    bool m_positionsLoaded{false};
};
