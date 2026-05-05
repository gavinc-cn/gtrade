#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>

class PaginationWidget : public QWidget {
    Q_OBJECT

public:
    explicit PaginationWidget(QWidget* parent = nullptr);

    // Getters
    int currentPage() const { return m_currentPage; }
    int pageSize() const { return m_pageSize; }
    int totalItems() const { return m_totalItems; }
    int totalPages() const;

    // Setters
    void setCurrentPage(int page);
    void setPageSize(int size);
    void setTotalItems(int total);

signals:
    void pageChanged(int page);
    void pageSizeChanged(int size);

private slots:
    void onPrevClicked();
    void onNextClicked();
    void onFirstClicked();
    void onLastClicked();
    void onPageSizeChanged(int index);
    void onPageNumberChanged();

private:
    void setupUI();
    void updateUI();

    int m_currentPage{1};
    int m_pageSize{20};
    int m_totalItems{0};

    QPushButton* m_firstButton{nullptr};
    QPushButton* m_prevButton{nullptr};
    QPushButton* m_nextButton{nullptr};
    QPushButton* m_lastButton{nullptr};
    QLabel* m_infoLabel{nullptr};
    QComboBox* m_pageSizeCombo{nullptr};
    QSpinBox* m_pageSpinBox{nullptr};
};
