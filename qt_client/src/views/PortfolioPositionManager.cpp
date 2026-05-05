#include "views/PortfolioPositionManager.h"
#include "services/DictService.h"
#include <spdlog/spdlog.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QJsonArray>
#include <QGroupBox>
#include <QLabel>

#include "GrpcNetworkManager.h"

PortfolioPositionManager::PortfolioPositionManager(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void PortfolioPositionManager::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Filter card
    m_filterCard = new FilterCard(this);
    m_filterCard->addTextField("Account ID", "account_id");
    m_filterCard->addTextField("Portfolio", "portfolio");
    m_filterCard->addTextField("Instrument", "instrument");
    mainLayout->addWidget(m_filterCard);

    // Summary bar
    QGroupBox* summaryBox = new QGroupBox("Summary", this);
    QHBoxLayout* summaryLayout = new QHBoxLayout(summaryBox);

    m_totalPositionsLabel = new QLabel("Total Positions: 0", this);
    summaryLayout->addWidget(m_totalPositionsLabel);

    m_totalUplLabel = new QLabel("Total Float P&L: 0.00", this);
    summaryLayout->addWidget(m_totalUplLabel);

    summaryLayout->addStretch();
    mainLayout->addWidget(summaryBox);

    // Table
    m_tableWidget = new QTableWidget(this);
    m_tableWidget->setColumnCount(13);
    m_tableWidget->setHorizontalHeaderLabels({
        "Market", "Account ID", "Portfolio", "Instrument", "Pos Side",
        "Inst Type", "Available Qty", "Avg Price", "Unrealized P&L",
        "ROI%", "Notional USD", "Margin Mode", "Update Time"
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
    connect(m_filterCard, &FilterCard::queryClicked, this, &PortfolioPositionManager::onQueryClicked);
    connect(m_filterCard, &FilterCard::resetClicked, this, &PortfolioPositionManager::onResetClicked);
    connect(m_filterCard, &FilterCard::refreshClicked, this, &PortfolioPositionManager::onRefreshClicked);
    connect(m_pagination, &PaginationWidget::pageChanged, this, &PortfolioPositionManager::onPageChanged);
    connect(m_pagination, &PaginationWidget::pageSizeChanged, this, &PortfolioPositionManager::onPageSizeChanged);

    // Initial load
    loadPositions();
}

void PortfolioPositionManager::onQueryClicked() {
    loadPositions();
}

void PortfolioPositionManager::onResetClicked() {
    loadPositions();
}

void PortfolioPositionManager::onRefreshClicked() {
    loadPositions();
}

void PortfolioPositionManager::onPageChanged(int page) {
    loadPositions();
}

void PortfolioPositionManager::onPageSizeChanged(int size) {
    loadPositions();
}

void PortfolioPositionManager::loadPositions() {
    QMap<QString, QString> params;
    params["page"] = QString::number(m_pagination->currentPage());
    params["page_size"] = QString::number(m_pagination->pageSize());

    QMap<QString, QString> filterValues = m_filterCard->getAllValues();
    for (auto it = filterValues.begin(); it != filterValues.end(); ++it) {
        params[it.key()] = it.value();
    }

    GrpcNetworkManager::instance().get(
        "/api/portfolio_positions",
        [this](const QJsonObject& response, bool success, const QString& error) {
            if (success) {
                QJsonArray positionsArray = response["positions"].toArray();
                int total = response["total"].toInt(0);

                m_positions.clear();
                m_totalUpl = 0.0;

                for (const QJsonValue& value : positionsArray) {
                    Position pos = Position::fromJson(value.toObject());
                    m_positions.append(pos);
                    m_totalUpl += pos.upl();
                }

                m_totalItems = total;
                m_pagination->setTotalItems(total);
                populateTable(m_positions);
                updateSummary();
            } else {
                QString errorMsg = "Failed to load portfolio positions: " + error;
                SPDLOG_ERROR("[PortfolioPositionManager] {}", errorMsg.toStdString());
                QMessageBox::warning(this, "Error", errorMsg);
            }
        },
        params
    );
}

void PortfolioPositionManager::populateTable(const QList<Position>& positions) {
    m_tableWidget->setRowCount(0);

    for (int row = 0; row < positions.size(); ++row) {
        const Position& pos = positions[row];

        m_tableWidget->insertRow(row);

        m_tableWidget->setItem(row, 0, new QTableWidgetItem(pos.market()));
        m_tableWidget->setItem(row, 1, new QTableWidgetItem(pos.accountId()));
        m_tableWidget->setItem(row, 2, new QTableWidgetItem(pos.portfolio()));
        m_tableWidget->setItem(row, 3, new QTableWidgetItem(pos.instrument()));

        QString posSideName = DictService::instance().getName("PosSide", pos.posSide());
        m_tableWidget->setItem(row, 4, new QTableWidgetItem(posSideName));

        QString instTypeName = DictService::instance().getName("InstType", pos.instType());
        m_tableWidget->setItem(row, 5, new QTableWidgetItem(instTypeName));

        m_tableWidget->setItem(row, 6, new QTableWidgetItem(
            QString::number(pos.available(), 'f', 4)));

        m_tableWidget->setItem(row, 7, new QTableWidgetItem(
            QString::number(pos.avgPx(), 'f', 4)));

        QTableWidgetItem* uplItem = new QTableWidgetItem(
            QString::number(pos.upl(), 'f', 2));
        QString uplColor = pos.isProfitable() ? "#67c23a" : "#f56c6c";
        uplItem->setForeground(QColor(uplColor));
        m_tableWidget->setItem(row, 8, uplItem);

        QTableWidgetItem* roiItem = new QTableWidgetItem(pos.uplRatioPercent());
        roiItem->setForeground(QColor(uplColor));
        m_tableWidget->setItem(row, 9, roiItem);

        m_tableWidget->setItem(row, 10, new QTableWidgetItem(
            QString::number(pos.notionalUsd(), 'f', 2)));

        m_tableWidget->setItem(row, 11, new QTableWidgetItem(pos.marginMode()));
        m_tableWidget->setItem(row, 12, new QTableWidgetItem(pos.datetime()));
    }

    m_tableWidget->resizeColumnsToContents();
}

void PortfolioPositionManager::updateSummary() {
    m_totalPositionsLabel->setText(QString("Total Positions: %1").arg(m_totalItems));

    QString uplText = QString("Total Float P&L: %1").arg(m_totalUpl, 0, 'f', 2);
    m_totalUplLabel->setText(uplText);
    QString uplColor = m_totalUpl >= 0 ? "#67c23a" : "#f56c6c";
    m_totalUplLabel->setStyleSheet(QString("color: %1;").arg(uplColor));
}
