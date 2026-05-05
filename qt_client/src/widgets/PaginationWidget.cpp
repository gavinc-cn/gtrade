#include "widgets/PaginationWidget.h"
#include <QHBoxLayout>
#include <QLabel>
#include <algorithm>

PaginationWidget::PaginationWidget(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void PaginationWidget::setupUI() {
    QHBoxLayout* layout = new QHBoxLayout(this);

    // Page size selector
    layout->addWidget(new QLabel("Items per page:", this));
    m_pageSizeCombo = new QComboBox(this);
    m_pageSizeCombo->addItem("10", 10);
    m_pageSizeCombo->addItem("20", 20);
    m_pageSizeCombo->addItem("50", 50);
    m_pageSizeCombo->addItem("100", 100);
    m_pageSizeCombo->setCurrentIndex(1); // Default 20
    layout->addWidget(m_pageSizeCombo);

    layout->addStretch();

    // Navigation buttons
    m_firstButton = new QPushButton("First", this);
    m_prevButton = new QPushButton("Previous", this);
    m_nextButton = new QPushButton("Next", this);
    m_lastButton = new QPushButton("Last", this);

    layout->addWidget(m_firstButton);
    layout->addWidget(m_prevButton);

    // Page number spin box
    layout->addWidget(new QLabel("Page:", this));
    m_pageSpinBox = new QSpinBox(this);
    m_pageSpinBox->setMinimum(1);
    m_pageSpinBox->setMaximum(1);
    m_pageSpinBox->setValue(1);
    layout->addWidget(m_pageSpinBox);

    layout->addWidget(m_nextButton);
    layout->addWidget(m_lastButton);

    // Info label
    m_infoLabel = new QLabel(this);
    layout->addWidget(m_infoLabel);

    // Connect signals
    connect(m_firstButton, &QPushButton::clicked, this, &PaginationWidget::onFirstClicked);
    connect(m_prevButton, &QPushButton::clicked, this, &PaginationWidget::onPrevClicked);
    connect(m_nextButton, &QPushButton::clicked, this, &PaginationWidget::onNextClicked);
    connect(m_lastButton, &QPushButton::clicked, this, &PaginationWidget::onLastClicked);
    connect(m_pageSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PaginationWidget::onPageSizeChanged);
    connect(m_pageSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]() { onPageNumberChanged(); });

    setLayout(layout);
    updateUI();
}

int PaginationWidget::totalPages() const {
    if (m_pageSize <= 0) return 1;
    return std::max(1, (m_totalItems + m_pageSize - 1) / m_pageSize);
}

void PaginationWidget::setCurrentPage(int page) {
    m_currentPage = std::max(1, std::min(page, totalPages()));
    updateUI();
}

void PaginationWidget::setPageSize(int size) {
    m_pageSize = size;
    m_currentPage = 1; // Reset to first page when changing page size
    updateUI();
}

void PaginationWidget::setTotalItems(int total) {
    m_totalItems = total;
    updateUI();
}

void PaginationWidget::updateUI() {
    int pages = totalPages();

    // Update spin box range
    m_pageSpinBox->setMaximum(pages);
    m_pageSpinBox->setValue(m_currentPage);

    // Update buttons enabled state
    m_firstButton->setEnabled(m_currentPage > 1);
    m_prevButton->setEnabled(m_currentPage > 1);
    m_nextButton->setEnabled(m_currentPage < pages);
    m_lastButton->setEnabled(m_currentPage < pages);

    // Update info label
    int startItem = (m_currentPage - 1) * m_pageSize + 1;
    int endItem = std::min(m_currentPage * m_pageSize, m_totalItems);

    if (m_totalItems == 0) {
        m_infoLabel->setText("No items");
    } else {
        m_infoLabel->setText(QString("Showing %1-%2 of %3")
                            .arg(startItem)
                            .arg(endItem)
                            .arg(m_totalItems));
    }
}

void PaginationWidget::onPrevClicked() {
    if (m_currentPage > 1) {
        m_currentPage--;
        updateUI();
        emit pageChanged(m_currentPage);
    }
}

void PaginationWidget::onNextClicked() {
    if (m_currentPage < totalPages()) {
        m_currentPage++;
        updateUI();
        emit pageChanged(m_currentPage);
    }
}

void PaginationWidget::onFirstClicked() {
    if (m_currentPage != 1) {
        m_currentPage = 1;
        updateUI();
        emit pageChanged(m_currentPage);
    }
}

void PaginationWidget::onLastClicked() {
    int pages = totalPages();
    if (m_currentPage != pages) {
        m_currentPage = pages;
        updateUI();
        emit pageChanged(m_currentPage);
    }
}

void PaginationWidget::onPageSizeChanged(int index) {
    int newSize = m_pageSizeCombo->itemData(index).toInt();
    if (newSize != m_pageSize) {
        setPageSize(newSize);
        emit pageSizeChanged(m_pageSize);
    }
}

void PaginationWidget::onPageNumberChanged() {
    int newPage = m_pageSpinBox->value();
    if (newPage != m_currentPage) {
        m_currentPage = newPage;
        updateUI();
        emit pageChanged(m_currentPage);
    }
}
