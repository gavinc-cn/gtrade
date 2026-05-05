#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QList>
#include "models/Position.h"
#include "widgets/FilterCard.h"
#include "widgets/PaginationWidget.h"

class PortfolioPositionManager : public QWidget {
    Q_OBJECT

public:
    explicit PortfolioPositionManager(QWidget* parent = nullptr);

private slots:
    void onQueryClicked();
    void onResetClicked();
    void onRefreshClicked();
    void onPageChanged(int page);
    void onPageSizeChanged(int size);

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

    QList<Position> m_positions;
    int m_totalItems{0};
    double m_totalUpl{0.0};
};
