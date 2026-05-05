#include "views/LoginDialog.h"
#include "views/MainWindow.h"
#include "utils/ConfigManager.h"
#include "utils/NetworkManager.h"
#include "logger_config.h"
#include "utils/TerminateHandlerBoost.h"
#include "services/DictService.h"
#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QFile>
#include <QMessageBox>
#include <QDebug>

#include "GrpcNetworkManager.h"

QString loadStyleSheet(const QString& fileName) {
    // Try multiple paths to find the stylesheet file
    QStringList searchPaths;

    // 1. Relative to executable (for installed/deployed application)
    searchPaths << QCoreApplication::applicationDirPath() + "/resources/styles/" + fileName;

    // 2. Relative to source directory (for development)
    searchPaths << "../resources/styles/" + fileName;
    searchPaths << "../../resources/styles/" + fileName;
    searchPaths << "../../../resources/styles/" + fileName;

    // 3. Absolute path from project root (for development in IDE)
    searchPaths << "/opt/win/gtrade/qt_client/resources/styles/" + fileName;

    for (const QString& path : searchPaths) {
        QFile file(path);
        if (file.exists() && file.open(QFile::ReadOnly | QFile::Text)) {
            QString styleSheet = QLatin1String(file.readAll());
            file.close();
            qDebug() << "Loaded stylesheet from:" << path;
            return styleSheet;
        }
    }

    // If file not found, log warning and return empty string
    qWarning() << "Failed to load stylesheet:" << fileName;
    qWarning() << "Searched paths:" << searchPaths;
    return QString();
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Set application metadata
    app.setApplicationName("GTrade Client");
    app.setOrganizationName("GTrade");
    app.setApplicationVersion("1.0.0");

    // Apply dark theme stylesheet
    QString styleSheet = loadStyleSheet("dark_theme.qss");
    if (!styleSheet.isEmpty()) {
        app.setStyleSheet(styleSheet);
    } else {
        qWarning() << "Failed to load stylesheet, using default theme";
    }

    // Load configuration
    ConfigManager& config = ConfigManager::instance();
    config.load("config.yml");  // Load from config.yml in current directory

    // Configure NetworkManager with backend URLs from config
    NetworkManager::instance().setWebServerUrl(config.webServerUrl());
    NetworkManager::instance().setGTradeUrl(config.gtradeHttpUrl());

    // Initialize logger using create_logger2
    zrt::logger_config loggerConfig;
    loggerConfig.m_log_file = config.logFilePath().toStdString();
    QString logLevelStr = config.logLevels().toLower();
    QString showLevelStr = config.showLevel().toLower();
    loggerConfig.m_log_level = logLevelStr.toStdString();
    loggerConfig.m_show_level = showLevelStr.toStdString();
    loggerConfig.m_async = true;
    loggerConfig.m_set_default = true;
    zrt::create_logger2(loggerConfig);

    // Install Boost.Stacktrace terminate handler for better crash diagnostics
    QtTerminate::installTerminateHandlerBoost();

    SPDLOG_INFO("[Main] GTrade Client v{} starting...", app.applicationVersion().toStdString());

    // Connect to desktop_gateway via gRPC
    SPDLOG_INFO("[Main] Connecting to desktop_gateway at {}", config.desktopGatewayEndpoint().toStdString());
    qDebug() << "Connecting to desktop_gateway at" << config.desktopGatewayEndpoint();

    bool connected = false;

    // Use mTLS if certificate paths are configured, otherwise use insecure connection
    if (!config.caCertPath().isEmpty() &&
        !config.clientCertPath().isEmpty() &&
        !config.clientKeyPath().isEmpty())
    {
        SPDLOG_INFO("[Main] Using mTLS authentication with certificates");
        qDebug() << "Using mTLS with CA cert:" << config.caCertPath();

        connected = GrpcNetworkManager::instance().connect(
            config.desktopGatewayEndpoint(),
            config.caCertPath(),
            config.clientCertPath(),
            config.clientKeyPath()
        );
    } else {
        SPDLOG_INFO("[Main] Using insecure connection (no certificates configured)");
        qDebug() << "Using insecure connection with public key";

        connected = GrpcNetworkManager::instance().connect(
            config.desktopGatewayEndpoint(),
            config.desktopGatewayPublicKey()
        );
    }

    if (!connected) {
        QString errorMsg = QString("Failed to connect to desktop_gateway at %1").arg(config.desktopGatewayEndpoint());
        SPDLOG_ERROR("[Main] {}", errorMsg.toStdString());
        SPDLOG_ERROR("[Main] Please ensure: 1. GTrade backend is running 2. config.yml has correct server_address 3. Network connectivity is available");

        qWarning() << "Failed to connect to desktop_gateway - will show login dialog anyway";
    } else {
        SPDLOG_INFO("[Main] Successfully connected to desktop_gateway");
        qDebug() << "Successfully connected to desktop_gateway";
    }

    // Always show login dialog if no token, regardless of connection status
    if (QString token = GrpcNetworkManager::instance().getToken();
        token.isEmpty()) {
        // Show login dialog
        LoginDialog loginDialog;

        // Show connection status in login dialog if not connected
        if (!connected) {
            loginDialog.setWindowTitle("GTrade Client - Login (Not Connected)");
        }

        if (loginDialog.exec() == QDialog::Accepted) {
            qDebug() << "Login dialog accepted, creating MainWindow";
            // Login successful, show main window
            MainWindow mainWindow;
            qDebug() << "MainWindow created, showing...";
            mainWindow.show();
            qDebug() << "MainWindow shown, entering event loop";
            return app.exec();
        } else {
            // Login cancelled
            if (connected) {
                GrpcNetworkManager::instance().disconnect();
            }
            return 0;
        }
    } else {
        // Token exists, show main window directly
        MainWindow mainWindow;
        mainWindow.show();
        return app.exec();
    }
}
