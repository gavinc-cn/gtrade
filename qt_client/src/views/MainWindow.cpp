#include "views/MainWindow.h"
#include "views/StrategyManager.h"
#include "views/OrderManager.h"
#include "views/TradeManager.h"
#include "views/PositionManager.h"
#include "views/PortfolioPositionManager.h"
#include "views/LoginDialog.h"
#include "services/DictService.h"
#include "utils/NetworkManager.h"
#include "utils/DateTimeUtils.h"
#include <spdlog/spdlog.h>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QToolBar>
#include <QMessageBox>
#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QStackedWidget>
#include <QTimer>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    qDebug() << "MainWindow constructor start";

    qDebug() << "Calling setupUI()";
    setupUI();

    qDebug() << "Calling loadDataDictionary()";
    loadDataDictionary();

    // TODO: Connect unauthorized signal when GrpcNetworkManager supports it
    // connect(&NetworkManager::instance(), &NetworkManager::unauthorized,
    //         this, &MainWindow::onUnauthorized);

    qDebug() << "Setting up clock timer";
    // Start clock timer
    m_clockTimer = new QTimer(this);
    connect(m_clockTimer, &QTimer::timeout, this, &MainWindow::updateClock);
    m_clockTimer->start(1000); // Update every second
    updateClock();

    qDebug() << "MainWindow constructor complete";
}

MainWindow::~MainWindow() {
}

void MainWindow::setupUI() {
    qDebug() << "setupUI: setWindowTitle";
    setWindowTitle("GTrade Client");
    setMinimumSize(1200, 800);

    qDebug() << "setupUI: creating central widget";
    // Central widget
    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    qDebug() << "setupUI: calling setupPages()";
    // Content area (must be created BEFORE setupNavigation to avoid null pointer crash)
    setupPages();
    qDebug() << "setupUI: setupPages() complete";

    qDebug() << "setupUI: creating right layout";
    // Right side layout (header + content)
    QVBoxLayout* rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(0);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    qDebug() << "setupUI: calling setupToolBar()";
    // Setup toolbar (must be created BEFORE setupNavigation to avoid m_titleLabel null pointer)
    setupToolBar();
    qDebug() << "setupUI: setupToolBar() complete";

    qDebug() << "setupUI: calling setupNavigation()";
    // Setup navigation sidebar
    setupNavigation();
    qDebug() << "setupUI: setupNavigation() complete";
    mainLayout->addWidget(m_navigationList);

    rightLayout->addWidget(m_stackedWidget, 1);

    qDebug() << "setupUI: adding layout";
    mainLayout->addLayout(rightLayout, 1);

    qDebug() << "setupUI: setCentralWidget";
    setCentralWidget(centralWidget);
    qDebug() << "setupUI: complete";
}

void MainWindow::setupNavigation() {
    m_navigationList = new QListWidget(this);
    m_navigationList->setMaximumWidth(200);
    m_navigationList->setMinimumWidth(200);

    m_pageTitles << "Strategy Manager"
                 << "Order Manager"
                 << "Trade Manager"
                 << "Position Manager"
                 << "Portfolio Positions";

    for (const QString& title : m_pageTitles) {
        m_navigationList->addItem(title);
    }

    connect(m_navigationList, &QListWidget::currentRowChanged,
            this, &MainWindow::onNavigationItemClicked);

    // Select first item by default
    m_navigationList->setCurrentRow(0);
}

void MainWindow::setupPages() {
    qDebug() << "setupPages: creating QStackedWidget";
    m_stackedWidget = new QStackedWidget(this);

    qDebug() << "setupPages: creating StrategyManager";
    m_strategyManager = new StrategyManager(this);
    qDebug() << "setupPages: StrategyManager created";

    qDebug() << "setupPages: creating OrderManager";
    m_orderManager = new OrderManager(this);
    qDebug() << "setupPages: OrderManager created";

    qDebug() << "setupPages: creating TradeManager";
    m_tradeManager = new TradeManager(this);
    qDebug() << "setupPages: TradeManager created";

    qDebug() << "setupPages: creating PositionManager";
    m_positionManager = new PositionManager(this);
    qDebug() << "setupPages: PositionManager created";

    qDebug() << "setupPages: creating PortfolioPositionManager";
    m_portfolioPositionManager = new PortfolioPositionManager(this);
    qDebug() << "setupPages: PortfolioPositionManager created";

    qDebug() << "setupPages: adding widgets to stack";
    m_stackedWidget->addWidget(m_strategyManager);
    m_stackedWidget->addWidget(m_orderManager);
    m_stackedWidget->addWidget(m_tradeManager);
    m_stackedWidget->addWidget(m_positionManager);
    m_stackedWidget->addWidget(m_portfolioPositionManager);
    qDebug() << "setupPages: complete";
}

void MainWindow::setupToolBar() {
    QToolBar* toolbar = new QToolBar(this);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);

    // Title label
    m_titleLabel = new QLabel("Strategy Manager", this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    toolbar->addWidget(m_titleLabel);

    toolbar->addSeparator();

    // Spacer
    QWidget* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    // Clock label
    m_clockLabel = new QLabel(this);
    toolbar->addWidget(m_clockLabel);

    toolbar->addSeparator();

    // Logout button
    m_logoutButton = new QPushButton("Logout", this);
    connect(m_logoutButton, &QPushButton::clicked, this, &MainWindow::onLogoutClicked);
    toolbar->addWidget(m_logoutButton);

    addToolBar(toolbar);
}

void MainWindow::loadDataDictionary() {
    qDebug() << "loadDataDictionary start";
    try {
        DictService::instance().load();
        qDebug() << "DictService loaded successfully";
    } catch (const std::exception& e) {
        qDebug() << "DictService load failed:" << e.what();
    } catch (...) {
        qDebug() << "DictService load failed with unknown exception";
    }
    qDebug() << "loadDataDictionary complete";
}

void MainWindow::onNavigationItemClicked(int index) {
    // Safety check: ensure widgets are initialized
    if (!m_stackedWidget) {
        qWarning() << "onNavigationItemClicked called before m_stackedWidget initialized";
        return;
    }
    if (!m_titleLabel) {
        qWarning() << "onNavigationItemClicked called before m_titleLabel initialized";
        return;
    }

    if (index >= 0 && index < m_stackedWidget->count()) {
        m_stackedWidget->setCurrentIndex(index);

        // Update title
        if (index < m_pageTitles.size()) {
            m_titleLabel->setText(m_pageTitles[index]);
        }
    }
}

void MainWindow::onLogoutClicked() {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Confirm Logout",
        "Are you sure you want to logout?",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        // TODO: Clear token when GrpcNetworkManager supports it
        // NetworkManager::instance().clearToken();
        close();
        QApplication::quit();
    }
}

void MainWindow::updateClock() {
    m_clockLabel->setText(DateTimeUtils::getCurrentTime());
}

void MainWindow::onUnauthorized() {
    QString errorMsg = "Session expired - user needs to login again";
    SPDLOG_WARN("[MainWindow] {}", errorMsg.toStdString());
    QMessageBox::warning(this, "Session Expired",
                        "Your session has expired. Please login again.");
    // TODO: Clear token when GrpcNetworkManager supports it
    // NetworkManager::instance().clearToken();
    close();
    QApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Confirm Exit",
        "Are you sure you want to exit?",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        event->accept();
    } else {
        event->ignore();
    }
}
