#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QTimer>
#include <QList>
#include "models/Position.h"
#include "widgets/FilterCard.h"
#include "widgets/PaginationWidget.h"

class PositionManager : public QWidget {
    Q_OBJECT

public:
    explicit PositionManager(QWidget* parent = nullptr);

private slots:
    void onQueryClicked();
    void onResetClicked();
    void onRefreshClicked();
    void onPageChanged(int page);
    void onPageSizeChanged(int size);
    void updateReconciliationStatus();

private:
    void setupUI();
    void loadPositions();
    void populateTable(const QList<Position>& positions);
    void updateSummary();

    FilterCard* m_filterCard{nullptr};
    QTableWidget* m_tableWidget{nullptr};
    PaginationWidget* m_pagination{nullptr};

    QLabel* m_totalPositionsLabel{nullptr};
    QLabel* m_totalUplLabel{nullptr};
    QLabel* m_inconsistentLabel{nullptr};
    QLabel* m_reconciliationLabel{nullptr};

    QTimer* m_reconciliationTimer{nullptr};

    QList<Position> m_positions;
    int m_totalItems{0};
    double m_totalUpl{0.0};
    int m_inconsistentCount{0};
};
