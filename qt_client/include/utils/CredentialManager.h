#pragma once

#include <QString>
#include <QSettings>

/**
 * @brief Manages secure storage of user credentials
 *
 * Uses QSettings for persistent storage with simple XOR obfuscation.
 * Note: This provides basic obfuscation, not cryptographic security.
 * For production use, consider using platform-specific secure storage
 * (Windows Credential Manager, macOS Keychain, etc.)
 */
class CredentialManager {
public:
    static CredentialManager& instance();

    /**
     * @brief Save credentials to persistent storage
     * @param username Username to save
     * @param password Password to save (will be obfuscated)
     * @param remember Whether to save credentials
     */
    void saveCredentials(const QString& username, const QString& password, bool remember);

    /**
     * @brief Load saved username
     * @return Saved username or empty string if none
     */
    QString loadUsername() const;

    /**
     * @brief Load saved password
     * @return Decrypted password or empty string if none
     */
    QString loadPassword() const;

    /**
     * @brief Check if credentials are saved
     * @return true if remember_credentials is enabled
     */
    bool hasCredentials() const;

    /**
     * @brief Clear saved credentials
     */
    void clearCredentials();

private:
    CredentialManager();
    ~CredentialManager() = default;
    CredentialManager(const CredentialManager&) = delete;
    CredentialManager& operator=(const CredentialManager&) = delete;

    /**
     * @brief Simple XOR obfuscation for password storage
     */
    QString obfuscate(const QString& data) const;
    QString deobfuscate(const QString& data) const;

    QSettings m_settings;
    static constexpr const char* k_username = "credentials/username";
    static constexpr const char* k_password = "credentials/password";
    static constexpr const char* k_remember = "credentials/remember";
};
