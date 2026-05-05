#include "widgets/FilterCard.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>

FilterCard::FilterCard(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void FilterCard::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Filter fields group
    QGroupBox* filterGroup = new QGroupBox("Filters", this);
    m_formLayout = new QFormLayout(filterGroup);
    mainLayout->addWidget(filterGroup);

    // Button layout
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    m_queryButton = new QPushButton("Query", this);
    m_resetButton = new QPushButton("Reset", this);
    m_refreshButton = new QPushButton("Refresh", this);

    buttonLayout->addWidget(m_queryButton);
    buttonLayout->addWidget(m_resetButton);
    buttonLayout->addWidget(m_refreshButton);
    buttonLayout->addStretch();

    mainLayout->addLayout(buttonLayout);

    // Connect signals
    connect(m_queryButton, &QPushButton::clicked, this, &FilterCard::queryClicked);
    connect(m_resetButton, &QPushButton::clicked, this, &FilterCard::resetClicked);
    connect(m_refreshButton, &QPushButton::clicked, this, &FilterCard::refreshClicked);

    // Connect reset to clearFilters
    connect(m_resetButton, &QPushButton::clicked, this, &FilterCard::clearFilters);

    setLayout(mainLayout);
}

void FilterCard::addTextField(const QString& label, const QString& fieldName) {
    QLineEdit* lineEdit = new QLineEdit(this);
    lineEdit->setPlaceholderText("Enter " + label.toLower());
    m_textFields[fieldName] = lineEdit;
    m_formLayout->addRow(label + ":", lineEdit);
}

void FilterCard::addComboBox(const QString& label, const QString& fieldName,
                             const QMap<QString, QString>& options) {
    QComboBox* comboBox = new QComboBox(this);
    comboBox->addItem("All", "");

    for (auto it = options.begin(); it != options.end(); ++it) {
        comboBox->addItem(it.key(), it.value());
    }

    m_comboBoxes[fieldName] = comboBox;
    m_formLayout->addRow(label + ":", comboBox);
}

QString FilterCard::getFieldValue(const QString& fieldName) const {
    if (m_textFields.contains(fieldName)) {
        return m_textFields[fieldName]->text();
    }

    if (m_comboBoxes.contains(fieldName)) {
        return m_comboBoxes[fieldName]->currentData().toString();
    }

    return "";
}

QMap<QString, QString> FilterCard::getAllValues() const {
    QMap<QString, QString> values;

    for (auto it = m_textFields.begin(); it != m_textFields.end(); ++it) {
        QString value = it.value()->text();
        if (!value.isEmpty()) {
            values[it.key()] = value;
        }
    }

    for (auto it = m_comboBoxes.begin(); it != m_comboBoxes.end(); ++it) {
        QString value = it.value()->currentData().toString();
        if (!value.isEmpty()) {
            values[it.key()] = value;
        }
    }

    return values;
}

void FilterCard::clearFilters() {
    for (QLineEdit* lineEdit : m_textFields.values()) {
        lineEdit->clear();
    }

    for (QComboBox* comboBox : m_comboBoxes.values()) {
        comboBox->setCurrentIndex(0); // Set to "All"
    }
}
