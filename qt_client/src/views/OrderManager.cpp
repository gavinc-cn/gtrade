#include "views/OrderManager.h"
#include "services/DictService.h"
#include "utils/DateTimeUtils.h"
#include <spdlog/spdlog.h>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QJsonArray>

#include "GrpcNetworkManager.h"

OrderManager::OrderManager(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void OrderManager::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Filter card
    m_filterCard = new FilterCard(this);
    m_filterCard->addTextField("Strategy Name", "policy_no");
    m_filterCard->addTextField("Symbol", "inst_id");

    // Add status filter
    QMap<QString, QString> statusOptions = DictService::instance().getOptions("EntrustStatus");
    m_filterCard->addComboBox("Status", "status", statusOptions);

    mainLayout->addWidget(m_filterCard);

    // Table
    m_tableWidget = new QTableWidget(this);
    m_tableWidget->setColumnCount(10);
    m_tableWidget->setHorizontalHeaderLabels({
        "Order ID", "Strategy", "Symbol", "Side", "Price",
        "Quantity", "Filled Qty", "Status", "Create Time", "Actions"
    });

    m_tableWidget->horizontalHeader()->setStretchLastSection(false);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);

    mainLayout->addWidget(m_tableWidget, 1);

    // Pagination
    m_pagination = new PaginationWidget(this);
    mainLayout->addWidget(m_pagination);

    setLayout(mainLayout);

    // Connect signals
    connect(m_filterCard, &FilterCard::queryClicked, this, &OrderManager::onQueryClicked);
    connect(m_filterCard, &FilterCard::resetClicked, this, &OrderManager::onResetClicked);
    connect(m_filterCard, &FilterCard::refreshClicked, this, &OrderManager::onRefreshClicked);
    connect(m_pagination, &PaginationWidget::pageChanged, this, &OrderManager::onPageChanged);
    connect(m_pagination, &PaginationWidget::pageSizeChanged, this, &OrderManager::onPageSizeChanged);

    // Initial load
    loadOrders();
}

void OrderManager::onQueryClicked() {
    loadOrders();
}

void OrderManager::onResetClicked() {
    loadOrders();
}

void OrderManager::onRefreshClicked() {
    loadOrders();
}

void OrderManager::onPageChanged(int page) {
    loadOrders();
}

void OrderManager::onPageSizeChanged(int size) {
    loadOrders();
}

void OrderManager::loadOrders() {
    QMap<QString, QString> params;
    params["page"] = QString::number(m_pagination->currentPage());
    params["page_size"] = QString::number(m_pagination->pageSize());

    // Add filter values
    QMap<QString, QString> filterValues = m_filterCard->getAllValues();
    for (auto it = filterValues.begin(); it != filterValues.end(); ++it) {
        params[it.key()] = it.value();
    }

    GrpcNetworkManager::instance().get(
        "/api/orders",
        [this](const QJsonObject& response, bool success, const QString& error) {
            if (success) {
                QJsonArray ordersArray = response["orders"].toArray();
                int total = response["total"].toInt(0);

                m_orders.clear();
                for (const QJsonValue& value : ordersArray) {
                    m_orders.append(Order::fromJson(value.toObject()));
                }

                m_totalItems = total;
                m_pagination->setTotalItems(total);
                populateTable(m_orders);
            } else {
                QString errorMsg = "Failed to load orders: " + error;
                SPDLOG_ERROR("[OrderManager] {}", errorMsg.toStdString());
                QMessageBox::warning(this, "Error", errorMsg);
            }
        },
        params
    );
}

void OrderManager::populateTable(const QList<Order>& orders) {
    m_tableWidget->setRowCount(0);

    for (int row = 0; row < orders.size(); ++row) {
        const Order& order = orders[row];

        m_tableWidget->insertRow(row);

        // Order ID
        m_tableWidget->setItem(row, 0, new QTableWidgetItem(order.entno()));

        // Strategy
        m_tableWidget->setItem(row, 1, new QTableWidgetItem(order.policyNo()));

        // Symbol
        m_tableWidget->setItem(row, 2, new QTableWidgetItem(order.instId()));

        // Side
        QString sideName = DictService::instance().getName("BuySellSide", order.bsSide());
        QTableWidgetItem* sideItem = new QTableWidgetItem(sideName);
        QString sideColor = getSideTagColor(order.bsSide());
        sideItem->setForeground(QColor(sideColor));
        m_tableWidget->setItem(row, 3, sideItem);

        // Price
        m_tableWidget->setItem(row, 4, new QTableWidgetItem(
            QString::number(order.price(), 'f', 4)));

        // Quantity
        m_tableWidget->setItem(row, 5, new QTableWidgetItem(
            QString::number(order.amount(), 'f', 4)));

        // Filled Qty
        m_tableWidget->setItem(row, 6, new QTableWidgetItem(
            QString::number(order.filled(), 'f', 4)));

        // Status
        QString statusName = DictService::instance().getName("EntrustStatus", order.status());
        QTableWidgetItem* statusItem = new QTableWidgetItem(statusName);
        QString statusColor = getStatusTagColor(order.status());
        statusItem->setForeground(QColor(statusColor));
        m_tableWidget->setItem(row, 7, statusItem);

        // Create Time
        m_tableWidget->setItem(row, 8, new QTableWidgetItem(order.formattedEntTime()));

        // Actions
        if (order.canCancel()) {
            QPushButton* cancelButton = new QPushButton("Cancel", this);
            cancelButton->setProperty("orderId", order.entno());
            connect(cancelButton, &QPushButton::clicked, this, &OrderManager::onCancelOrderClicked);
            m_tableWidget->setCellWidget(row, 9, cancelButton);
        } else {
            m_tableWidget->setItem(row, 9, new QTableWidgetItem("-"));
        }
    }

    m_tableWidget->resizeColumnsToContents();
}

QString OrderManager::getStatusTagColor(const QString& status) const {
    QString type = DictService::instance().getType("EntrustStatus", status);
    if (type == "success") return "#67c23a";
    if (type == "warning") return "#faad14";
    if (type == "danger") return "#f56c6c";
    return "#409eff";
}

QString OrderManager::getSideTagColor(const QString& side) const {
    if (side == "B") return "#67c23a"; // Buy = green
    if (side == "S") return "#f56c6c"; // Sell = red
    return "#409eff";
}

void OrderManager::onCancelOrderClicked() {
    QPushButton* button = qobject_cast<QPushButton*>(sender());
    if (!button) return;

    QString orderId = button->property("orderId").toString();

    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Confirm Cancel",
        QString("Are you sure you want to cancel order %1?").arg(orderId),
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        // TODO: Implement cancel order API call
        QMessageBox::information(this, "Info", "Cancel order API not implemented yet");
    }
}
