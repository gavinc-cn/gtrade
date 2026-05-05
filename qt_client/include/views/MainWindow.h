#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QListWidget>
#include <QLabel>
#include <QTimer>
#include <QPushButton>

class StrategyManager;
class OrderManager;
class TradeManager;
class PositionManager;
class PortfolioPositionManager;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNavigationItemClicked(int index);
    void onLogoutClicked();
    void updateClock();
    void onUnauthorized();

private:
    void setupUI();
    void setupNavigation();
    void setupPages();
    void setupToolBar();
    void loadDataDictionary();

    QStackedWidget* m_stackedWidget{nullptr};
    QListWidget* m_navigationList{nullptr};
    QLabel* m_titleLabel{nullptr};
    QLabel* m_clockLabel{nullptr};
    QPushButton* m_logoutButton{nullptr};

    // Pages
    StrategyManager* m_strategyManager{nullptr};
    OrderManager* m_orderManager{nullptr};
    TradeManager* m_tradeManager{nullptr};
    PositionManager* m_positionManager{nullptr};
    PortfolioPositionManager* m_portfolioPositionManager{nullptr};

    QTimer* m_clockTimer{nullptr};

    // Page titles
    QStringList m_pageTitles;
};
