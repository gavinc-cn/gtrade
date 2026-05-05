#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFormLayout>
#include <QMap>
#include <QString>
#include <functional>

class FilterCard : public QWidget {
    Q_OBJECT

public:
    explicit FilterCard(QWidget* parent = nullptr);

    // Add filter fields
    void addTextField(const QString& label, const QString& fieldName);
    void addComboBox(const QString& label, const QString& fieldName,
                     const QMap<QString, QString>& options);

    // Get filter values
    QString getFieldValue(const QString& fieldName) const;
    QMap<QString, QString> getAllValues() const;

    // Clear all filters
    void clearFilters();

signals:
    void queryClicked();
    void resetClicked();
    void refreshClicked();

private:
    QFormLayout* m_formLayout{nullptr};
    QMap<QString, QLineEdit*> m_textFields;
    QMap<QString, QComboBox*> m_comboBoxes;

    QPushButton* m_queryButton{nullptr};
    QPushButton* m_resetButton{nullptr};
    QPushButton* m_refreshButton{nullptr};

    void setupUI();
};
