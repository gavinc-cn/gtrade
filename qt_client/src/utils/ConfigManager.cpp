#include "utils/ConfigManager.h"
#include <QFile>
#include <QDebug>
#include <yaml-cpp/yaml.h>
#include <fstream>

ConfigManager& ConfigManager::instance() {
    static ConfigManager instance;
    return instance;
}

ConfigManager::ConfigManager() {
    loadDefaults();
}

void ConfigManager::loadDefaults() {
    // Set default values (already initialized in header)
    qDebug() << "Using default configuration values";
}

bool ConfigManager::load(const QString& configPath) {
    if (!QFile::exists(configPath)) {
        qWarning() << "Config file not found:" << configPath << "- using defaults";
        loadDefaults();
        return false;
    }

    try {
        // Load YAML file
        YAML::Node config = YAML::LoadFile(configPath.toStdString());

        // Load Desktop Gateway settings
        if (config["desktop_gateway"]) {
            const auto& dg = config["desktop_gateway"];
            // Read server_address field (matches config.yml)
            if (dg["server_address"]) {
                m_desktopGatewayEndpoint = QString::fromStdString(dg["server_address"].as<std::string>());
            }
            // Read certificate paths for mTLS
            if (dg["ca_cert"]) {
                m_caCertPath = QString::fromStdString(dg["ca_cert"].as<std::string>());
            }
            if (dg["client_cert"]) {
                m_clientCertPath = QString::fromStdString(dg["client_cert"].as<std::string>());
            }
            if (dg["client_key"]) {
                m_clientKeyPath = QString::fromStdString(dg["client_key"].as<std::string>());
            }
            // Legacy public_key support (if present)
            if (dg["public_key"]) {
                m_desktopGatewayPublicKey = QString::fromStdString(dg["public_key"].as<std::string>());
            }
        }

        // Load legacy HTTP backend settings
        if (config["http_backend"]) {
            const auto& http = config["http_backend"];
            if (http["web_server_url"]) {
                m_webServerUrl = QString::fromStdString(http["web_server_url"].as<std::string>());
            }
            if (http["gtrade_http_url"]) {
                m_gtradeHttpUrl = QString::fromStdString(http["gtrade_http_url"].as<std::string>());
            }
        }

        // Load general settings
        if (config["general"]) {
            const auto& general = config["general"];
            if (general["backend_type"]) {
                QString backendTypeStr = QString::fromStdString(general["backend_type"].as<std::string>()).toLower();
                if (backendTypeStr == "http") {
                    m_backendType = BackendType::HTTP;
                } else {
                    m_backendType = BackendType::DesktopGateway;
                }
            }
            if (general["default_username"]) {
                m_defaultUsername = QString::fromStdString(general["default_username"].as<std::string>());
            }
            if (general["debug"]) {
                m_debugEnabled = general["debug"].as<bool>();
            }
        }

        // Load logging settings
        if (config["logging"]) {
            const auto& logging = config["logging"];
            if (logging["log_file"]) {
                m_logFilePath = QString::fromStdString(logging["log_file"].as<std::string>());
            }
            if (logging["log_levels"]) {
                m_logLevels = QString::fromStdString(logging["log_levels"].as<std::string>()).toLower();
            }
            if (logging["show_level"]) {
                m_showLevel = QString::fromStdString(logging["show_level"].as<std::string>()).toLower();
            }
        }

        qDebug() << "Configuration loaded from" << configPath;
        qDebug() << "  Backend type:" << backendTypeString();
        qDebug() << "  Desktop Gateway endpoint:" << m_desktopGatewayEndpoint;
        qDebug() << "  CA cert:" << (m_caCertPath.isEmpty() ? "(not set)" : m_caCertPath);
        qDebug() << "  Client cert:" << (m_clientCertPath.isEmpty() ? "(not set)" : m_clientCertPath);
        qDebug() << "  Client key:" << (m_clientKeyPath.isEmpty() ? "(not set)" : m_clientKeyPath);
        qDebug() << "  Desktop Gateway public key:" << (m_desktopGatewayPublicKey.isEmpty() ? "(not set)" : "(set)");
        qDebug() << "  Default username:" << m_defaultUsername;
        qDebug() << "  Debug enabled:" << m_debugEnabled;
        qDebug() << "  Log file:" << m_logFilePath;
        qDebug() << "  Log levels:" << m_logLevels;
        qDebug() << "  Show level:" << m_showLevel;

        return true;
    } catch (const YAML::Exception& e) {
        qWarning() << "Failed to parse YAML config:" << e.what() << "- using defaults";
        loadDefaults();
        return false;
    }
}

QString ConfigManager::backendTypeString() const {
    switch (m_backendType) {
        case BackendType::DesktopGateway:
            return "DesktopGateway (gRPC)";
        case BackendType::HTTP:
            return "HTTP (Legacy)";
        default:
            return "Unknown";
    }
}
