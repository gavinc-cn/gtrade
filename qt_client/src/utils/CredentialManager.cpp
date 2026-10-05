#include "utils/CredentialManager.h"
#include <QCoreApplication>
#include <QByteArray>
#include <QDebug>

CredentialManager& CredentialManager::instance() {
    static CredentialManager instance;
    return instance;
}

CredentialManager::CredentialManager()
    : m_settings(QCoreApplication::organizationName(),
                 QCoreApplication::applicationName()) {
}

void CredentialManager::saveCredentials(const QString& username, const QString& password, bool remember) {
    if (remember) {
        m_settings.setValue(k_username, username);
        m_settings.setValue(k_password, obfuscate(password));
        m_settings.setValue(k_remember, true);
        qDebug() << "Credentials saved for user:" << username;
    } else {
        clearCredentials();
    }
    m_settings.sync();
}

QString CredentialManager::loadUsername() const {
    if (!hasCredentials()) {
        return QString{};
    }
    return m_settings.value(k_username).toString();
}

QString CredentialManager::loadPassword() const {
    if (!hasCredentials()) {
        return QString{};
    }
    QString obfuscated = m_settings.value(k_password).toString();
    return deobfuscate(obfuscated);
}

bool CredentialManager::hasCredentials() const {
    return m_settings.value(k_remember, false).toBool();
}

void CredentialManager::clearCredentials() {
    m_settings.remove(k_username);
    m_settings.remove(k_password);
    m_settings.remove(k_remember);
    m_settings.sync();
    qDebug() << "Credentials cleared";
}

QString CredentialManager::obfuscate(const QString& data) const {
    // Simple XOR obfuscation - NOT cryptographically secure
    // This is just to prevent casual viewing of credentials in plain text
    const QByteArray key = "GTrade_Qt_Client_2025";
    QByteArray input = data.toUtf8();
    QByteArray output;

    for (int i = 0; i < input.size(); ++i) {
        output.append(input[i] ^ key[i % key.size()]);
    }

    return QString::fromLatin1(output.toBase64());
}

QString CredentialManager::deobfuscate(const QString& data) const {
    const QByteArray key = "GTrade_Qt_Client_2025";
    QByteArray input = QByteArray::fromBase64(data.toLatin1());
    QByteArray output;

    for (int i = 0; i < input.size(); ++i) {
        output.append(input[i] ^ key[i % key.size()]);
    }

    return QString::fromUtf8(output);
}
