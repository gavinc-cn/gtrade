#pragma once

#include <QString>
#include <memory>

/**
 * @brief Configuration manager for GTrade Qt Client
 *
 * Reads and manages application configuration from config.yml file (YAML format)
 */
class ConfigManager {
public:
    static ConfigManager& instance();

    /**
     * @brief Load configuration from file
     * @param configPath Path to config.yml file
     * @return true if configuration loaded successfully
     */
    bool load(const QString& configPath = "config.yml");

    // Desktop Gateway settings
    QString desktopGatewayEndpoint() const { return m_desktopGatewayEndpoint; }
    QString desktopGatewayPublicKey() const { return m_desktopGatewayPublicKey; }
    QString caCertPath() const { return m_caCertPath; }
    QString clientCertPath() const { return m_clientCertPath; }
    QString clientKeyPath() const { return m_clientKeyPath; }

    // Legacy HTTP backend settings (for fallback)
    QString webServerUrl() const { return m_webServerUrl; }
    QString gtradeHttpUrl() const { return m_gtradeHttpUrl; }

    // Backend type selection
    enum class BackendType {
        DesktopGateway,  // ZeroMQ desktop_gateway (recommended)
        HTTP             // Legacy HTTP backends
    };

    BackendType backendType() const { return m_backendType; }
    QString backendTypeString() const;

    // Default credentials (optional, for convenience)
    QString defaultUsername() const { return m_defaultUsername; }

    // Logging
    bool debugEnabled() const { return m_debugEnabled; }
    QString logFilePath() const { return m_logFilePath; }
    QString logLevels() const { return m_logLevels; }
    QString showLevel() const { return m_showLevel; }

private:
    ConfigManager();
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    void loadDefaults();

    // Desktop Gateway
    QString m_desktopGatewayEndpoint{"localhost:50051"};  // gRPC endpoint
    QString m_desktopGatewayPublicKey;
    QString m_caCertPath;
    QString m_clientCertPath;
    QString m_clientKeyPath;

    // Legacy HTTP backends
    QString m_webServerUrl{"http://localhost:5000"};
    QString m_gtradeHttpUrl{"http://localhost:8080"};

    // Backend selection
    BackendType m_backendType{BackendType::DesktopGateway};

    // Credentials
    QString m_defaultUsername{"admin"};

    // Logging
    bool m_debugEnabled{false};
    QString m_logFilePath{"logs/gtrade_client.log"};
    QString m_logLevels{"info,warn,error"};  // Comma-separated log levels for file output
    QString m_showLevel{"info"};  // Minimum level for console output
};
