#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>

class LoginDialog : public QDialog {
    Q_OBJECT

public:
    explicit LoginDialog(QWidget* parent = nullptr);

signals:
    void loginSuccessful(const QString& token);

private slots:
    void onLoginClicked();

private:
    void setupUI();
    void loadSavedCredentials();
    void performLogin(const QString& username, const QString& password);

    QLineEdit* m_usernameEdit{nullptr};
    QLineEdit* m_passwordEdit{nullptr};
    QPushButton* m_loginButton{nullptr};
    QLabel* m_messageLabel{nullptr};
    QLabel* m_titleLabel{nullptr};
    QCheckBox* m_rememberCheckBox{nullptr};
};
