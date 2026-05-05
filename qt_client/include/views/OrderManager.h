#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QList>
#include "models/Order.h"
#include "widgets/FilterCard.h"
#include "widgets/PaginationWidget.h"

class OrderManager : public QWidget {
    Q_OBJECT

public:
    explicit OrderManager(QWidget* parent = nullptr);

private slots:
    void onQueryClicked();
    void onResetClicked();
    void onRefreshClicked();
    void onPageChanged(int page);
    void onPageSizeChanged(int size);
    void onCancelOrderClicked();

private:
    void setupUI();
    void loadOrders();
    void populateTable(const QList<Order>& orders);
    QString getStatusTagColor(const QString& status) const;
    QString getSideTagColor(const QString& side) const;

    FilterCard* m_filterCard{nullptr};
    QTableWidget* m_tableWidget{nullptr};
    PaginationWidget* m_pagination{nullptr};

    QList<Order> m_orders;
    int m_totalItems{0};
};
