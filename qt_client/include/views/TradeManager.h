#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QList>
#include "models/Trade.h"
#include "widgets/FilterCard.h"
#include "widgets/PaginationWidget.h"

class TradeManager : public QWidget {
    Q_OBJECT

public:
    explicit TradeManager(QWidget* parent = nullptr);

private slots:
    void onQueryClicked();
    void onResetClicked();
    void onRefreshClicked();
    void onPageChanged(int page);
    void onPageSizeChanged(int size);

private:
    void setupUI();
    void loadTrades();
    void populateTable(const QList<Trade>& trades);

    FilterCard* m_filterCard{nullptr};
    QTableWidget* m_tableWidget{nullptr};
    PaginationWidget* m_pagination{nullptr};

    QList<Trade> m_trades;
    int m_totalItems{0};
};
