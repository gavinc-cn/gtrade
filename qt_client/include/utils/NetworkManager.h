#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonDocument>
#include <QString>
#include <functional>

class NetworkManager : public QObject {
    Q_OBJECT

public:
    using ResponseCallback = std::function<void(const QJsonObject&, bool success, const QString& error)>;

    enum class Backend {
        WebServer,   // Flask web_server on port 5000
        GTradeHTTP   // GTrade HTTP gateway on port 8080
    };

    static NetworkManager& instance();

    // Set backend URLs
    void setWebServerUrl(const QString& url) { m_webServerUrl = url; }
    void setGTradeUrl(const QString& url) { m_gtradeUrl = url; }

    // Set authentication token
    void setToken(const QString& token);
    QString getToken() const;
    void clearToken();

    // HTTP methods
    void get(Backend backend, const QString& path, ResponseCallback callback,
             const QMap<QString, QString>& params = {});
    void post(Backend backend, const QString& path, const QJsonObject& data,
              ResponseCallback callback);
    void put(Backend backend, const QString& path, const QJsonObject& data,
             ResponseCallback callback);
    void del(Backend backend, const QString& path, ResponseCallback callback);

signals:
    void unauthorized(); // 401 - need to login again
    void networkError(const QString& message);

private:
    NetworkManager();
    ~NetworkManager() = default;
    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    QString getBaseUrl(Backend backend) const;
    QNetworkRequest createRequest(Backend backend, const QString& path) const;
    void handleResponse(QNetworkReply* reply, ResponseCallback callback);

    QNetworkAccessManager* m_manager{nullptr};
    QString m_token;
    QString m_webServerUrl{"http://localhost:5000"};
    QString m_gtradeUrl{"http://localhost:8080"};
    int m_timeout{15000}; // 15 seconds
};
