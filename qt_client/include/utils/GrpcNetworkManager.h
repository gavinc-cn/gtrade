#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>
#include <functional>
#include <grpcpp/grpcpp.h>
#include "desktop_gateway.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

/**
 * @brief GrpcNetworkManager - Qt wrapper for gRPC client communication with Desktop Gateway
 *
 * This class provides a Qt-friendly interface for communicating with the backend
 * Desktop Gateway service using gRPC with mTLS authentication.
 *
 * Features:
 * - mTLS certificate-based authentication
 * - JWT token authentication at application level
 * - Asynchronous API calls with callback support
 * - Automatic token management
 */
class GrpcNetworkManager : public QObject {
    Q_OBJECT

public:
    using ResponseCallback = std::function<void(const QJsonObject& data, bool success, const QString& error)>;

    static GrpcNetworkManager& instance();

    ~GrpcNetworkManager() override;

    /**
     * @brief Connect to the gRPC server with mTLS
     * @param server_address Server address (e.g., "localhost:50051")
     * @param ca_cert_path Path to CA certificate
     * @param client_cert_path Path to client certificate
     * @param client_key_path Path to client private key
     * @return true if connection successful
     */
    bool connect(const QString& server_address,
                const QString& ca_cert_path,
                const QString& client_cert_path,
                const QString& client_key_path);
    bool connect(const QString& server_address, const QString& public_key);

    /**
     * @brief Check if connected to server
     */
    bool isConnected() const { return m_connected; }

    /**
     * @brief Disconnect from server
     */
    void disconnect();

    /**
     * @brief Set JWT authentication token
     */
    void setToken(const QString& token);

    /**
     * @brief Get current JWT token
     */
    QString token() const { return m_token; }
    QString getToken() const { return m_token; }

    // ========== Authentication ==========

    /**
     * @brief Login to the server
     * @param username User name
     * @param password Password
     * @param callback Callback function
     */
    void login(const QString& username, const QString& password, ResponseCallback callback);

    // ========== Strategy Management ==========

    void getStrategies(const QString& template_filter, ResponseCallback callback);
    void getStrategy(const QString& strat_id, ResponseCallback callback);
    void addStrategy(const QString& config_json, ResponseCallback callback);
    void deleteStrategy(const QString& strat_id, ResponseCallback callback);
    void startStrategy(const QString& strat_id, ResponseCallback callback);
    void stopStrategy(const QString& strat_id, ResponseCallback callback);
    void restartStrategy(const QString& strat_id, ResponseCallback callback);
    void batchOperation(const QStringList& strat_ids, const QString& operation, ResponseCallback callback);

    // ========== Order Management ==========

    void getOrders(const QString& strat_id, const QString& instrument, const QString& status,
                  int page, int page_size, ResponseCallback callback);

    // ========== Trade Management ==========

    void getTrades(const QString& strat_id, const QString& instrument,
                  int page, int page_size, ResponseCallback callback);

    // ========== Position Management ==========

    void getPositions(const QString& strat_id, const QString& instrument, ResponseCallback callback);

    // ========== Template Management ==========

    void getTemplates(ResponseCallback callback);
    void getTemplateConfig(const QString& template_name, ResponseCallback callback);

    // ========== Dictionary ==========

    void getDictionary(ResponseCallback callback);

    // ========== HTTP Proxy ==========
    void get(const QString& path, ResponseCallback callback, const QMap<QString, QString>& params = {});
    void post(const QString& path, const QJsonObject& data, ResponseCallback callback);
    void del(const QString& path, ResponseCallback callback);

signals:
    void connected();
    void disconnected();
    void error(const QString& errorMessage);

private:
    explicit GrpcNetworkManager(QObject* parent = nullptr);
    GrpcNetworkManager(const GrpcNetworkManager&) = delete;
    GrpcNetworkManager& operator=(const GrpcNetworkManager&) = delete;

    // Helper methods
    void setAuthMetadata(ClientContext* context) const;
    QJsonObject statusToJson(const gtrade::desktop_gateway::Status& status) const;
    QString readFile(const QString& path) const;

    // gRPC channel and stub
    std::shared_ptr<Channel> m_channel;
    std::unique_ptr<gtrade::desktop_gateway::DesktopGateway::Stub> m_stub;

    // Connection state
    bool m_connected{false};
    QString m_server_address;

    // Authentication
    QString m_token;
    qint64 m_token_expires_at{0};
};
