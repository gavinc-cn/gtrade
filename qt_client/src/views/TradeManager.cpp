#include "views/TradeManager.h"
#include "services/DictService.h"
#include "utils/GrpcNetworkManager.h"
#include <spdlog/spdlog.h>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QJsonArray>

TradeManager::TradeManager(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void TradeManager::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Filter card
    m_filterCard = new FilterCard(this);
    m_filterCard->addTextField("Strategy Name", "strat_id");
    m_filterCard->addTextField("Symbol", "instrument");

    QMap<QString, QString> sideOptions = DictService::instance().getOptions("TradeSide");
    m_filterCard->addComboBox("Side", "td_side", sideOptions);

    mainLayout->addWidget(m_filterCard);

    // Table
    m_tableWidget = new QTableWidget(this);
    m_tableWidget->setColumnCount(10);
    m_tableWidget->setHorizontalHeaderLabels({
        "Trade ID", "Order ID", "Strategy", "Symbol", "Side",
        "Price", "Qty", "Amount", "Pos Side", "Time"
    });

    m_tableWidget->horizontalHeader()->setStretchLastSection(true);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);

    mainLayout->addWidget(m_tableWidget, 1);

    // Pagination
    m_pagination = new PaginationWidget(this);
    mainLayout->addWidget(m_pagination);

    setLayout(mainLayout);

    // Connect signals
    connect(m_filterCard, &FilterCard::queryClicked, this, &TradeManager::onQueryClicked);
    connect(m_filterCard, &FilterCard::resetClicked, this, &TradeManager::onResetClicked);
    connect(m_filterCard, &FilterCard::refreshClicked, this, &TradeManager::onRefreshClicked);
    connect(m_pagination, &PaginationWidget::pageChanged, this, &TradeManager::onPageChanged);
    connect(m_pagination, &PaginationWidget::pageSizeChanged, this, &TradeManager::onPageSizeChanged);

    // Initial load
    loadTrades();
}

void TradeManager::onQueryClicked() {
    loadTrades();
}

void TradeManager::onResetClicked() {
    loadTrades();
}

void TradeManager::onRefreshClicked() {
    loadTrades();
}

void TradeManager::onPageChanged(int page) {
    loadTrades();
}

void TradeManager::onPageSizeChanged(int size) {
    loadTrades();
}

void TradeManager::loadTrades() {
    QMap<QString, QString> params;
    params["page"] = QString::number(m_pagination->currentPage());
    params["page_size"] = QString::number(m_pagination->pageSize());

    // Add filter values
    QMap<QString, QString> filterValues = m_filterCard->getAllValues();
    for (auto it = filterValues.begin(); it != filterValues.end(); ++it) {
        params[it.key()] = it.value();
    }

    GrpcNetworkManager::instance().get(
        "/api/trades",
        [this](const QJsonObject& response, bool success, const QString& error) {
            if (success) {
                QJsonArray tradesArray = response["trades"].toArray();
                int total = response["total"].toInt(0);

                m_trades.clear();
                for (const QJsonValue& value : tradesArray) {
                    m_trades.append(Trade::fromJson(value.toObject()));
                }

                m_totalItems = total;
                m_pagination->setTotalItems(total);
                populateTable(m_trades);
            } else {
                QString errorMsg = "Failed to load trades: " + error;
                SPDLOG_ERROR("[TradeManager] {}", errorMsg.toStdString());
                QMessageBox::warning(this, "Error", errorMsg);
            }
        },
        params
    );
}

void TradeManager::populateTable(const QList<Trade>& trades) {
    m_tableWidget->setRowCount(0);

    for (int row = 0; row < trades.size(); ++row) {
        const Trade& trade = trades[row];

        m_tableWidget->insertRow(row);

        m_tableWidget->setItem(row, 0, new QTableWidgetItem(trade.tdno()));
        m_tableWidget->setItem(row, 1, new QTableWidgetItem(trade.ordno()));
        m_tableWidget->setItem(row, 2, new QTableWidgetItem(trade.stratId()));
        m_tableWidget->setItem(row, 3, new QTableWidgetItem(trade.instrument()));

        // Side
        QString sideName = DictService::instance().getName("TradeSide", trade.tdSide());
        QTableWidgetItem* sideItem = new QTableWidgetItem(sideName);
        QString sideColor = trade.tdSide() == "B" ? "#67c23a" : "#f56c6c";
        sideItem->setForeground(QColor(sideColor));
        m_tableWidget->setItem(row, 4, sideItem);

        m_tableWidget->setItem(row, 5, new QTableWidgetItem(
            QString::number(trade.tdPx(), 'f', 4)));

        m_tableWidget->setItem(row, 6, new QTableWidgetItem(
            QString::number(trade.tdQty(), 'f', 4)));

        m_tableWidget->setItem(row, 7, new QTableWidgetItem(
            QString::number(trade.amount(), 'f', 2)));

        // Pos Side
        QString posSideName = DictService::instance().getName("PosSide", trade.posSide());
        m_tableWidget->setItem(row, 8, new QTableWidgetItem(posSideName));

        m_tableWidget->setItem(row, 9, new QTableWidgetItem(trade.formattedFilledTime()));
    }

    m_tableWidget->resizeColumnsToContents();
}
