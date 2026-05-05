#include "views/LoginDialog.h"
#include "utils/ConfigManager.h"
#include "utils/CredentialManager.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QJsonObject>
#include <QTimer>

#include "GrpcNetworkManager.h"

LoginDialog::LoginDialog(QWidget* parent) : QDialog(parent) {
    setupUI();
    loadSavedCredentials();
}

void LoginDialog::setupUI() {
    setWindowTitle("GTrade Client - Login");
    setModal(true);
    setMinimumWidth(400);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Title
    m_titleLabel = new QLabel("GTrade Trading System", this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_titleLabel);

    mainLayout->addSpacing(20);

    // Form layout
    QFormLayout* formLayout = new QFormLayout();

    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText("Enter username");
    formLayout->addRow("Username:", m_usernameEdit);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setPlaceholderText("Enter password");
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    formLayout->addRow("Password:", m_passwordEdit);

    mainLayout->addLayout(formLayout);

    // Remember me checkbox
    m_rememberCheckBox = new QCheckBox("Remember credentials", this);
    m_rememberCheckBox->setChecked(false);
    mainLayout->addWidget(m_rememberCheckBox);

    // Message label
    m_messageLabel = new QLabel(this);
    m_messageLabel->setAlignment(Qt::AlignCenter);
    m_messageLabel->setWordWrap(true);
    mainLayout->addWidget(m_messageLabel);

    // Login button
    m_loginButton = new QPushButton("Login", this);
    m_loginButton->setDefault(true);
    mainLayout->addWidget(m_loginButton);

    setLayout(mainLayout);

    // Connect signals
    connect(m_loginButton, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginDialog::onLoginClicked);
}

void LoginDialog::onLoginClicked() {
    QString username = m_usernameEdit->text().trimmed();
    QString password = m_passwordEdit->text();

    if (username.isEmpty()) {
        m_messageLabel->setText("Please enter username");
        m_messageLabel->setStyleSheet("color: #f56c6c;");
        return;
    }

    if (password.isEmpty()) {
        m_messageLabel->setText("Please enter password");
        m_messageLabel->setStyleSheet("color: #f56c6c;");
        return;
    }

    performLogin(username, password);
}

void LoginDialog::loadSavedCredentials() {
    CredentialManager& credMgr = CredentialManager::instance();
    if (credMgr.hasCredentials()) {
        QString username = credMgr.loadUsername();
        QString password = credMgr.loadPassword();

        if (!username.isEmpty() && !password.isEmpty()) {
            m_usernameEdit->setText(username);
            m_passwordEdit->setText(password);
            m_rememberCheckBox->setChecked(true);
            qDebug() << "Loaded saved credentials for user:" << username;
        }
    }
}

void LoginDialog::performLogin(const QString& username, const QString& password) {
    m_loginButton->setEnabled(false);
    m_messageLabel->setText("Logging in...");
    m_messageLabel->setStyleSheet("color: #409eff;");

    // Use GrpcNetworkManager for desktop_gateway communication
    GrpcNetworkManager::instance().login(
        username,
        password,
        [this, username, password](const QJsonObject& responseData, bool success, const QString& error) {
            qDebug() << "Login callback received: success=" << success;
            m_loginButton->setEnabled(true);

            if (success && responseData.contains("token")) {
                QString token = responseData["token"].toString();
                qDebug() << "Got token, length=" << token.length();

                if (!token.isEmpty()) {
                    qDebug() << "Token valid, updating UI";
                    m_messageLabel->setText("Login successful!");
                    m_messageLabel->setStyleSheet("color: #67c23a;");

                    // Save credentials if remember is checked
                    CredentialManager::instance().saveCredentials(
                        username, password, m_rememberCheckBox->isChecked());

                    qDebug() << "Emitting loginSuccessful signal";
                    // Emit success signal
                    emit loginSuccessful(token);

                    qDebug() << "Closing dialog in 500ms";
                    // Close dialog after short delay
                    QTimer::singleShot(500, this, &QDialog::accept);
                } else {
                    m_messageLabel->setText("Login failed: invalid token");
                    m_messageLabel->setStyleSheet("color: #f56c6c;");
                }
            } else {
                QString errorMsg = error.isEmpty() ? "Login failed" : error;
                m_messageLabel->setText("Error: " + errorMsg);
                m_messageLabel->setStyleSheet("color: #f56c6c;");
            }
        }
    );
}
