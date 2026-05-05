#include "views/StrategyManager.h"
#include "services/DictService.h"
#include "utils/GrpcNetworkManager.h"
#include "utils/DateTimeUtils.h"
#include <spdlog/spdlog.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QJsonArray>
#include <QSplitter>
#include <QMenu>
#include <QSettings>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTimer>
#include <QAction>

StrategyManager::StrategyManager(QWidget* parent) : QWidget(parent) {
    setupUI();
    loadTemplates();

    // Setup auto-refresh timer (MUST be created before setCurrentIndex to avoid null pointer crash)
    m_autoRefreshTimer = new QTimer(this);
    connect(m_autoRefreshTimer, &QTimer::timeout, this, &StrategyManager::onAutoRefreshTimeout);

    // Load saved auto-refresh interval
    // Block signals temporarily to avoid triggering onAutoRefreshChanged during initialization
    QSettings settings("GTrade", "GTradeClient");
    int refreshIndex = settings.value("strategy/auto_refresh_index", 2).toInt(); // Default 5s

    m_autoRefreshCombo->blockSignals(true);
    m_autoRefreshCombo->setCurrentIndex(refreshIndex);
    m_autoRefreshCombo->blockSignals(false);

    // Manually trigger the auto-refresh setup now that everything is initialized
    onAutoRefreshChanged(refreshIndex);
}

void StrategyManager::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Top toolbar
    QHBoxLayout* toolbarLayout = new QHBoxLayout();

    m_refreshButton = new QPushButton("Refresh", this);
    toolbarLayout->addWidget(m_refreshButton);

    m_addButton = new QPushButton("Add Strategy", this);
    toolbarLayout->addWidget(m_addButton);

    toolbarLayout->addSpacing(20);

    m_batchStartButton = new QPushButton("Batch Start", this);
    toolbarLayout->addWidget(m_batchStartButton);

    m_batchStopButton = new QPushButton("Batch Stop", this);
    toolbarLayout->addWidget(m_batchStopButton);

    m_batchRestartButton = new QPushButton("Batch Restart", this);
    toolbarLayout->addWidget(m_batchRestartButton);

    m_batchDeleteButton = new QPushButton("Batch Delete", this);
    m_batchDeleteButton->setStyleSheet("QPushButton { background-color: #f56c6c; color: white; }");
    toolbarLayout->addWidget(m_batchDeleteButton);

    toolbarLayout->addStretch();

    toolbarLayout->addWidget(new QLabel("Auto Refresh:", this));
    m_autoRefreshCombo = new QComboBox(this);
    m_autoRefreshCombo->addItem("Off", 0);
    m_autoRefreshCombo->addItem("3s", 3000);
    m_autoRefreshCombo->addItem("5s", 5000);
    m_autoRefreshCombo->addItem("10s", 10000);
    m_autoRefreshCombo->addItem("30s", 30000);
    m_autoRefreshCombo->addItem("60s", 60000);
    toolbarLayout->addWidget(m_autoRefreshCombo);

    mainLayout->addLayout(toolbarLayout);

    // Template tabs
    m_templateTabs = new QTabWidget(this);
    connect(m_templateTabs, &QTabWidget::currentChanged, this, &StrategyManager::onTemplateTabChanged);

    // Splitter for strategy table and details
    QSplitter* splitter = new QSplitter(Qt::Vertical, this);

    // Strategy table widget (placeholder, will be created per template)
    QWidget* tableWidget = new QWidget(this);
    QVBoxLayout* tableLayout = new QVBoxLayout(tableWidget);
    tableLayout->setContentsMargins(0, 0, 0, 0);

    m_strategyTable = new QTableWidget(this);
    m_strategyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_strategyTable->setSelectionMode(QAbstractItemView::MultiSelection);
    m_strategyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_strategyTable->setContextMenuPolicy(Qt::CustomContextMenu);

    tableLayout->addWidget(m_strategyTable);
    splitter->addWidget(tableWidget);

    // Details panel
    m_detailsPanel = new QWidget(this);
    QVBoxLayout* detailsLayout = new QVBoxLayout(m_detailsPanel);
    detailsLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* detailsTitle = new QLabel("Strategy Details", this);
    QFont titleFont = detailsTitle->font();
    titleFont.setBold(true);
    detailsTitle->setFont(titleFont);
    detailsLayout->addWidget(detailsTitle);

    m_detailsTabs = new QTabWidget(this);

    // Orders tab
    m_ordersTable = new QTableWidget(this);
    m_ordersTable->setColumnCount(8);
    m_ordersTable->setHorizontalHeaderLabels({
        "Order ID", "Symbol", "Side", "Price", "Qty", "Filled", "Status", "Time"
    });
    m_ordersTable->horizontalHeader()->setStretchLastSection(true);
    m_detailsTabs->addTab(m_ordersTable, "Orders");

    // Trades tab
    m_tradesTable = new QTableWidget(this);
    m_tradesTable->setColumnCount(7);
    m_tradesTable->setHorizontalHeaderLabels({
        "Trade ID", "Order ID", "Symbol", "Side", "Price", "Qty", "Time"
    });
    m_tradesTable->horizontalHeader()->setStretchLastSection(true);
    m_detailsTabs->addTab(m_tradesTable, "Trades");

    // Positions tab
    m_positionsTable = new QTableWidget(this);
    m_positionsTable->setColumnCount(7);
    m_positionsTable->setHorizontalHeaderLabels({
        "Instrument", "Direction", "Available", "Avg Price", "P&L", "ROI%", "Update Time"
    });
    m_positionsTable->horizontalHeader()->setStretchLastSection(true);
    m_detailsTabs->addTab(m_positionsTable, "Positions");

    detailsLayout->addWidget(m_detailsTabs);
    splitter->addWidget(m_detailsPanel);

    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    m_templateTabs->addTab(splitter, "Loading...");
    mainLayout->addWidget(m_templateTabs, 1);

    m_detailsPanel->hide(); // Hide details initially

    setLayout(mainLayout);

    // Connect signals
    connect(m_refreshButton, &QPushButton::clicked, this, &StrategyManager::onRefreshClicked);
    connect(m_addButton, &QPushButton::clicked, this, &StrategyManager::onAddStrategyClicked);
    connect(m_batchStartButton, &QPushButton::clicked, this, &StrategyManager::onBatchStartClicked);
    connect(m_batchStopButton, &QPushButton::clicked, this, &StrategyManager::onBatchStopClicked);
    connect(m_batchRestartButton, &QPushButton::clicked, this, &StrategyManager::onBatchRestartClicked);
    connect(m_batchDeleteButton, &QPushButton::clicked, this, &StrategyManager::onBatchDeleteClicked);
    connect(m_autoRefreshCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &StrategyManager::onAutoRefreshChanged);
    connect(m_strategyTable, &QTableWidget::cellClicked, this, &StrategyManager::onTableCellClicked);
    connect(m_strategyTable, &QTableWidget::customContextMenuRequested,
            this, &StrategyManager::onStrategyContextMenu);
    connect(m_detailsTabs, &QTabWidget::currentChanged, this, &StrategyManager::onDetailsTabChanged);
}

void StrategyManager::loadTemplates() {
    GrpcNetworkManager::instance().get(
        "/api/template/list",
        [this](const QJsonObject& response, bool success, const QString& error) {
            if (success) {
                QJsonArray templatesArray = response["templates"].toArray();

                m_templateNames.clear();
                m_templateTabs->clear();

                for (const QJsonValue& value : templatesArray) {
                    QString templateName = value.toString();
                    m_templateNames.append(templateName);

                    // Create a tab for each template
                    m_templateTabs->addTab(new QWidget(), templateName);
                }

                // Load first template's strategies
                if (!m_templateNames.isEmpty()) {
                    m_currentTemplate = m_templateNames[0];
                    loadStrategies(m_currentTemplate);
                }
            } else {
                QString errorMsg = "Failed to load templates: " + error;
                SPDLOG_ERROR("[StrategyManager] {}", errorMsg.toStdString());
                QMessageBox::warning(this, "Error", errorMsg);
            }
        }
    );
}

void StrategyManager::onTemplateTabChanged(int index) {
    if (index >= 0 && index < m_templateNames.size()) {
        m_currentTemplate = m_templateNames[index];
        loadStrategies(m_currentTemplate);
        m_detailsPanel->hide();
    }
}

void StrategyManager::loadStrategies(const QString& templateName) {
    QString path = QString("/api/strategy/list/%1").arg(templateName);

    GrpcNetworkManager::instance().get(
        path,
        [this, templateName](const QJsonObject& response, bool success, const QString& error) {
            if (success) {
                QJsonArray strategiesArray = response["strategies"].toArray();

                QList<Strategy> strategies;
                for (const QJsonValue& value : strategiesArray) {
                    strategies.append(Strategy::fromJson(value.toObject()));
                }

                m_strategies[templateName] = strategies;
                updateStrategyColumns(templateName);
                populateStrategyTable(strategies);
            } else {
                QString errorMsg = QString("Failed to load strategies for %1: %2").arg(templateName).arg(error);
                SPDLOG_ERROR("[StrategyManager] {}", errorMsg.toStdString());
                QMessageBox::warning(this, "Error", errorMsg);
            }
        }
    );
}

void StrategyManager::updateStrategyColumns(const QString& templateName) {
    // Get template config to determine columns
    QString path = QString("/api/template/config/%1").arg(templateName);

    GrpcNetworkManager::instance().get(
        path,
        [this, templateName](const QJsonObject& response, bool success, const QString& error) {
            if (success) {
                QStringList columns;
                columns << "Select" << "ID" << "Name" << "Status" << "Update Time";

                // Add indicator columns
                QJsonObject indicators = response["indicators"].toObject();
                for (const QString& key : indicators.keys()) {
                    columns << key;
                }

                // Add parameter columns
                QJsonObject params = response["params"].toObject();
                for (const QString& key : params.keys()) {
                    columns << key;
                }

                m_templateColumns[templateName] = columns;

                // Update table header
                m_strategyTable->setColumnCount(columns.size());
                m_strategyTable->setHorizontalHeaderLabels(columns);
                m_strategyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
            }
        }
    );
}

void StrategyManager::populateStrategyTable(const QList<Strategy>& strategies) {
    m_strategyTable->setRowCount(0);

    for (int row = 0; row < strategies.size(); ++row) {
        const Strategy& strategy = strategies[row];

        m_strategyTable->insertRow(row);

        // Checkbox column
        QTableWidgetItem* checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        checkItem->setCheckState(Qt::Unchecked);
        m_strategyTable->setItem(row, 0, checkItem);

        // ID
        m_strategyTable->setItem(row, 1, new QTableWidgetItem(strategy.id()));

        // Name
        m_strategyTable->setItem(row, 2, new QTableWidgetItem(strategy.stratName()));

        // Status
        QTableWidgetItem* statusItem = new QTableWidgetItem(strategy.statusText());
        QString statusColor = strategy.isRunning() ? "#67c23a" : "#909399";
        statusItem->setForeground(QColor(statusColor));
        m_strategyTable->setItem(row, 3, statusItem);

        // Update Time with color based on age
        QTableWidgetItem* timeItem = new QTableWidgetItem(strategy.updateTime());
        if (DateTimeUtils::isTimestampOld(strategy.updateTime(), 60)) {
            timeItem->setForeground(QColor("#f56c6c")); // Red if > 60s
        } else if (DateTimeUtils::isTimestampOld(strategy.updateTime(), 20)) {
            timeItem->setForeground(QColor("#faad14")); // Yellow if > 20s
        }
        m_strategyTable->setItem(row, 4, timeItem);

        // Indicators
        int col = 5;
        for (const QString& key : strategy.indicators().keys()) {
            QVariant value = strategy.indicator(key);
            m_strategyTable->setItem(row, col++,
                new QTableWidgetItem(value.toString()));
        }

        // Params
        for (const QString& key : strategy.params().keys()) {
            QVariant value = strategy.param(key);
            m_strategyTable->setItem(row, col++,
                new QTableWidgetItem(value.toString()));
        }
    }

    m_strategyTable->resizeColumnsToContents();
}

void StrategyManager::onRefreshClicked() {
    if (!m_currentTemplate.isEmpty()) {
        loadStrategies(m_currentTemplate);
    }
}

void StrategyManager::onAddStrategyClicked() {
    QMessageBox::information(this, "Info", "Add strategy functionality not implemented yet");
}

void StrategyManager::onBatchStartClicked() {
    QList<QString> selectedIds = getSelectedStrategyIds();
    if (selectedIds.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select strategies first");
        return;
    }
    performStrategyAction("start", selectedIds);
}

void StrategyManager::onBatchStopClicked() {
    QList<QString> selectedIds = getSelectedStrategyIds();
    if (selectedIds.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select strategies first");
        return;
    }
    performStrategyAction("stop", selectedIds);
}

void StrategyManager::onBatchRestartClicked() {
    QList<QString> selectedIds = getSelectedStrategyIds();
    if (selectedIds.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select strategies first");
        return;
    }
    performStrategyAction("restart", selectedIds);
}

void StrategyManager::onBatchDeleteClicked() {
    QList<QString> selectedIds = getSelectedStrategyIds();
    if (selectedIds.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select strategies first");
        return;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Confirm Delete",
        QString("Are you sure you want to delete %1 strategies?").arg(selectedIds.size()),
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        performStrategyAction("delete", selectedIds);
    }
}

void StrategyManager::onAutoRefreshChanged(int index) {
    // Safety check: ensure widgets are initialized
    if (!m_autoRefreshCombo || !m_autoRefreshTimer) {
        qWarning() << "onAutoRefreshChanged called before widgets initialized";
        return;
    }

    int interval = m_autoRefreshCombo->itemData(index).toInt();

    // Save to settings
    QSettings settings("GTrade", "GTradeClient");
    settings.setValue("strategy/auto_refresh_index", index);

    if (interval > 0) {
        m_autoRefreshTimer->start(interval);
    } else {
        m_autoRefreshTimer->stop();
    }
}

void StrategyManager::onAutoRefreshTimeout() {
    onRefreshClicked();
}

void StrategyManager::onTableCellClicked(int row, int column) {
    if (row < 0 || column == 0) return; // Ignore checkbox column

    m_selectedStrategyRow = row;

    // Get strategy ID
    QTableWidgetItem* idItem = m_strategyTable->item(row, 1);
    if (idItem) {
        m_selectedStrategyId = idItem->text();

        // Find strategy in current list
        const QList<Strategy>& strategies = m_strategies[m_currentTemplate];
        for (const Strategy& strategy : strategies) {
            if (strategy.id() == m_selectedStrategyId) {
                showStrategyDetails(strategy);
                break;
            }
        }
    }
}

void StrategyManager::showStrategyDetails(const Strategy& strategy) {
    m_detailsPanel->show();

    // Reset lazy loading flags
    m_ordersLoaded = false;
    m_tradesLoaded = false;
    m_positionsLoaded = false;

    // Load current tab
    int currentTab = m_detailsTabs->currentIndex();
    onDetailsTabChanged(currentTab);
}

void StrategyManager::onDetailsTabChanged(int index) {
    if (m_selectedStrategyId.isEmpty()) return;

    switch (index) {
        case 0: // Orders
            if (!m_ordersLoaded) {
                loadStrategyOrders(m_selectedStrategyId);
                m_ordersLoaded = true;
            }
            break;
        case 1: // Trades
            if (!m_tradesLoaded) {
                loadStrategyTrades(m_selectedStrategyId);
                m_tradesLoaded = true;
            }
            break;
        case 2: // Positions
            if (!m_positionsLoaded) {
                loadStrategyPositions(m_selectedStrategyId);
                m_positionsLoaded = true;
            }
            break;
    }
}

void StrategyManager::loadStrategyOrders(const QString& strategyId) {
    QString path = QString("/api/strategy/%1/orders").arg(strategyId);

    GrpcNetworkManager::instance().get(
        path,
        [this](const QJsonObject& response, bool success, const QString& error) {
            if (success) {
                QJsonArray ordersArray = response["orders"].toArray();
                m_ordersTable->setRowCount(0);

                for (int row = 0; row < ordersArray.size(); ++row) {
                    Order order = Order::fromJson(ordersArray[row].toObject());

                    m_ordersTable->insertRow(row);
                    m_ordersTable->setItem(row, 0, new QTableWidgetItem(order.entno()));
                    m_ordersTable->setItem(row, 1, new QTableWidgetItem(order.instId()));

                    QString sideName = DictService::instance().getName("BuySellSide", order.bsSide());
                    m_ordersTable->setItem(row, 2, new QTableWidgetItem(sideName));

                    m_ordersTable->setItem(row, 3, new QTableWidgetItem(
                        QString::number(order.price(), 'f', 4)));
                    m_ordersTable->setItem(row, 4, new QTableWidgetItem(
                        QString::number(order.amount(), 'f', 4)));
                    m_ordersTable->setItem(row, 5, new QTableWidgetItem(
                        QString::number(order.filled(), 'f', 4)));

                    QString statusName = DictService::instance().getName("EntrustStatus", order.status());
                    m_ordersTable->setItem(row, 6, new QTableWidgetItem(statusName));

                    m_ordersTable->setItem(row, 7, new QTableWidgetItem(order.formattedEntTime()));
                }

                m_ordersTable->resizeColumnsToContents();
            }
        }
    );
}

void StrategyManager::loadStrategyTrades(const QString& strategyId) {
    QString path = QString("/api/strategy/%1/trades").arg(strategyId);

    GrpcNetworkManager::instance().get(
        path,
        [this](const QJsonObject& response, bool success, const QString& error) {
            if (success) {
                QJsonArray tradesArray = response["trades"].toArray();
                m_tradesTable->setRowCount(0);

                for (int row = 0; row < tradesArray.size(); ++row) {
                    Trade trade = Trade::fromJson(tradesArray[row].toObject());

                    m_tradesTable->insertRow(row);
                    m_tradesTable->setItem(row, 0, new QTableWidgetItem(trade.tdno()));
                    m_tradesTable->setItem(row, 1, new QTableWidgetItem(trade.ordno()));
                    m_tradesTable->setItem(row, 2, new QTableWidgetItem(trade.instrument()));

                    QString sideName = DictService::instance().getName("TradeSide", trade.tdSide());
                    m_tradesTable->setItem(row, 3, new QTableWidgetItem(sideName));

                    m_tradesTable->setItem(row, 4, new QTableWidgetItem(
                        QString::number(trade.tdPx(), 'f', 4)));
                    m_tradesTable->setItem(row, 5, new QTableWidgetItem(
                        QString::number(trade.tdQty(), 'f', 4)));
                    m_tradesTable->setItem(row, 6, new QTableWidgetItem(trade.formattedFilledTime()));
                }

                m_tradesTable->resizeColumnsToContents();
            }
        }
    );
}

void StrategyManager::loadStrategyPositions(const QString& strategyId) {
    QString path = QString("/api/strategy/%1/positions").arg(strategyId);

    GrpcNetworkManager::instance().get(
        path,
        [this](const QJsonObject& response, bool success, const QString& error) {
            if (success) {
                QJsonArray positionsArray = response["positions"].toArray();
                m_positionsTable->setRowCount(0);

                for (int row = 0; row < positionsArray.size(); ++row) {
                    Position pos = Position::fromJson(positionsArray[row].toObject());

                    m_positionsTable->insertRow(row);
                    m_positionsTable->setItem(row, 0, new QTableWidgetItem(pos.instrument()));

                    QString posSideName = DictService::instance().getName("PosSide", pos.posSide());
                    m_positionsTable->setItem(row, 1, new QTableWidgetItem(posSideName));

                    m_positionsTable->setItem(row, 2, new QTableWidgetItem(
                        QString::number(pos.available(), 'f', 4)));
                    m_positionsTable->setItem(row, 3, new QTableWidgetItem(
                        QString::number(pos.avgPx(), 'f', 4)));

                    QTableWidgetItem* uplItem = new QTableWidgetItem(
                        QString::number(pos.upl(), 'f', 2));
                    QString uplColor = pos.isProfitable() ? "#67c23a" : "#f56c6c";
                    uplItem->setForeground(QColor(uplColor));
                    m_positionsTable->setItem(row, 4, uplItem);

                    QTableWidgetItem* roiItem = new QTableWidgetItem(pos.uplRatioPercent());
                    roiItem->setForeground(QColor(uplColor));
                    m_positionsTable->setItem(row, 5, roiItem);

                    m_positionsTable->setItem(row, 6, new QTableWidgetItem(pos.datetime()));
                }

                m_positionsTable->resizeColumnsToContents();
            }
        }
    );
}

void StrategyManager::onStrategyContextMenu(const QPoint& pos) {
    QTableWidgetItem* item = m_strategyTable->itemAt(pos);
    if (!item) return;

    int row = item->row();
    QTableWidgetItem* idItem = m_strategyTable->item(row, 1);
    if (!idItem) return;

    QString strategyId = idItem->text();

    QMenu contextMenu(this);
    QAction* startAction = contextMenu.addAction("Start");
    QAction* stopAction = contextMenu.addAction("Stop");
    QAction* restartAction = contextMenu.addAction("Restart");
    contextMenu.addSeparator();
    QAction* deleteAction = contextMenu.addAction("Delete");

    startAction->setProperty("strategyId", strategyId);
    stopAction->setProperty("strategyId", strategyId);
    restartAction->setProperty("strategyId", strategyId);
    deleteAction->setProperty("strategyId", strategyId);

    connect(startAction, &QAction::triggered, this, &StrategyManager::onStrategyStart);
    connect(stopAction, &QAction::triggered, this, &StrategyManager::onStrategyStop);
    connect(restartAction, &QAction::triggered, this, &StrategyManager::onStrategyRestart);
    connect(deleteAction, &QAction::triggered, this, &StrategyManager::onStrategyDelete);

    contextMenu.exec(m_strategyTable->mapToGlobal(pos));
}

void StrategyManager::onStrategyStart() {
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action) return;

    QString strategyId = action->property("strategyId").toString();
    performStrategyAction("start", {strategyId});
}

void StrategyManager::onStrategyStop() {
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action) return;

    QString strategyId = action->property("strategyId").toString();
    performStrategyAction("stop", {strategyId});
}

void StrategyManager::onStrategyRestart() {
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action) return;

    QString strategyId = action->property("strategyId").toString();
    performStrategyAction("restart", {strategyId});
}

void StrategyManager::onStrategyDelete() {
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action) return;

    QString strategyId = action->property("strategyId").toString();

    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Confirm Delete",
        QString("Are you sure you want to delete strategy %1?").arg(strategyId),
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        performStrategyAction("delete", {strategyId});
    }
}

void StrategyManager::performStrategyAction(const QString& action, const QList<QString>& strategyIds) {
    if (strategyIds.isEmpty()) return;

    QString path;
    QJsonObject requestData;

    if (strategyIds.size() == 1) {
        // Single strategy action
        QString strategyId = strategyIds.first();

        if (action == "start") {
            path = QString("/api/strategy/start/%1").arg(strategyId);
        } else if (action == "stop") {
            path = QString("/api/strategy/stop/%1").arg(strategyId);
        } else if (action == "restart") {
            path = QString("/api/strategy/restart/%1").arg(strategyId);
        } else if (action == "delete") {
            // Use DELETE method
            GrpcNetworkManager::instance().del(
                        QString("/api/strategy/delete/%1").arg(strategyId),
                [this](const QJsonObject& response, bool success, const QString& error) {
                    if (success) {
                        SPDLOG_INFO("[StrategyManager] Strategy deleted successfully");
                        QMessageBox::information(this, "Success", "Strategy deleted successfully");
                        QTimer::singleShot(3000, this, &StrategyManager::onRefreshClicked);
                    } else {
                        QString errorMsg = "Failed to delete strategy: " + error;
                        SPDLOG_ERROR("[StrategyManager] {}", errorMsg.toStdString());
                        QMessageBox::warning(this, "Error", errorMsg);
                    }
                }
            );
            return;
        }

        GrpcNetworkManager::instance().post(
                path,
            requestData,
            [this, action](const QJsonObject& response, bool success, const QString& error) {
                if (success) {
                    QString successMsg = QString("Strategy %1 successfully").arg(action);
                    SPDLOG_INFO("[StrategyManager] {}", successMsg.toStdString());
                    QMessageBox::information(this, "Success", successMsg);
                    QTimer::singleShot(3000, this, &StrategyManager::onRefreshClicked);
                } else {
                    QString errorMsg = QString("Failed to %1 strategy: %2").arg(action).arg(error);
                    SPDLOG_ERROR("[StrategyManager] {}", errorMsg.toStdString());
                    QMessageBox::warning(this, "Error", errorMsg);
                }
            }
        );
    } else {
        // Batch action
        QJsonArray idsArray;
        for (const QString& id : strategyIds) {
            idsArray.append(id);
        }
        requestData["strategy_ids"] = idsArray;
        requestData["action"] = action;

        GrpcNetworkManager::instance().post(
                "/api/strategy/batch",
            requestData,
            [this, action](const QJsonObject& response, bool success, const QString& error) {
                if (success) {
                    QString successMsg = QString("Batch %1 completed successfully").arg(action);
                    SPDLOG_INFO("[StrategyManager] {}", successMsg.toStdString());
                    QMessageBox::information(this, "Success", successMsg);
                    QTimer::singleShot(3000, this, &StrategyManager::onRefreshClicked);
                } else {
                    QString errorMsg = QString("Failed to batch %1: %2").arg(action).arg(error);
                    SPDLOG_ERROR("[StrategyManager] {}", errorMsg.toStdString());
                    QMessageBox::warning(this, "Error", errorMsg);
                }
            }
        );
    }
}

QList<QString> StrategyManager::getSelectedStrategyIds() const {
    QList<QString> selectedIds;

    for (int row = 0; row < m_strategyTable->rowCount(); ++row) {
        QTableWidgetItem* checkItem = m_strategyTable->item(row, 0);
        if (checkItem && checkItem->checkState() == Qt::Checked) {
            QTableWidgetItem* idItem = m_strategyTable->item(row, 1);
            if (idItem) {
                selectedIds.append(idItem->text());
            }
        }
    }

    return selectedIds;
}
