#include "utils/NetworkManager.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QTimer>
#include <QSettings>
#include <QMessageBox>

NetworkManager& NetworkManager::instance() {
    static NetworkManager instance;
    return instance;
}

NetworkManager::NetworkManager() {
    m_manager = new QNetworkAccessManager(this);

    // Load token from settings
    const QSettings settings("GTrade", "GTradeClient");
    m_token = settings.value("auth/token", "").toString();
}

void NetworkManager::setToken(const QString& token) {
    m_token = token;
    QSettings settings("GTrade", "GTradeClient");
    settings.setValue("auth/token", token);
}

QString NetworkManager::getToken() const {
    return m_token;
}

void NetworkManager::clearToken() {
    m_token.clear();
    QSettings settings("GTrade", "GTradeClient");
    settings.remove("auth/token");
}

QString NetworkManager::getBaseUrl(const Backend backend) const {
    switch (backend) {
        case Backend::WebServer:
            return m_webServerUrl;
        case Backend::GTradeHTTP:
            return m_gtradeUrl;
        default:
            return m_webServerUrl;
    }
}

QNetworkRequest NetworkManager::createRequest(const Backend backend, const QString& path) const {
    const QString url = getBaseUrl(backend) + path;
    QNetworkRequest request(url);

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // Add Authorization header for WebServer backend
    if (backend == Backend::WebServer && !m_token.isEmpty()) {
        request.setRawHeader("Authorization", QString("Bearer %1").arg(m_token).toUtf8());
    }

    return request;
}

void NetworkManager::get(const Backend backend, const QString& path,
                         ResponseCallback callback,
                         const QMap<QString, QString>& params) {
    QString fullPath = path;

    // Add query parameters
    if (!params.isEmpty()) {
        QUrlQuery query;
        for (auto it = params.begin(); it != params.end(); ++it) {
            query.addQueryItem(it.key(), it.value());
        }
        fullPath += "?" + query.toString();
    }

    QNetworkRequest request = createRequest(backend, fullPath);
    QNetworkReply* reply = m_manager->get(request);

    handleResponse(reply, callback);
}

void NetworkManager::post(const Backend backend, const QString& path,
                          const QJsonObject& data,
                          ResponseCallback callback) {
    QNetworkRequest request = createRequest(backend, path);

    QJsonDocument doc(data);
    QByteArray jsonData = doc.toJson();

    QNetworkReply* reply = m_manager->post(request, jsonData);
    handleResponse(reply, callback);
}

void NetworkManager::put(const Backend backend, const QString& path,
                         const QJsonObject& data,
                         ResponseCallback callback) {
    const QNetworkRequest request = createRequest(backend, path);

    const QJsonDocument doc(data);
    QByteArray jsonData = doc.toJson();

    QNetworkReply* reply = m_manager->put(request, jsonData);
    handleResponse(reply, callback);
}

void NetworkManager::del(const Backend backend, const QString& path,
                         ResponseCallback callback) {
    const QNetworkRequest request = createRequest(backend, path);
    QNetworkReply* reply = m_manager->deleteResource(request);

    handleResponse(reply, callback);
}

void NetworkManager::handleResponse(QNetworkReply* reply, ResponseCallback callback) {
    // Set timeout
    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(m_timeout);

    connect(timer, &QTimer::timeout, this, [reply, callback, this]() {
        reply->abort();
        if (callback) {
            callback(QJsonObject(), false, "Request timeout");
        }
        emit networkError("Request timeout");
    });

    timer->start();

    connect(reply, &QNetworkReply::finished, this, [reply, callback, timer, this]() {
        timer->stop();
        timer->deleteLater();

        QNetworkReply::NetworkError error = reply->error();
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (error == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(responseData);

            if (doc.isObject()) {
                if (callback) {
                    callback(doc.object(), true, "");
                }
            } else {
                if (callback) {
                    callback(QJsonObject(), false, "Invalid JSON response");
                }
            }
        } else {
            QString errorMsg;

            switch (statusCode) {
                case 401:
                    errorMsg = "Unauthorized - please login again";
                    emit unauthorized();
                    clearToken();
                    break;
                case 403:
                    errorMsg = "Forbidden - permission denied";
                    break;
                case 404:
                    errorMsg = "Not found";
                    break;
                case 500:
                    errorMsg = "Server error";
                    break;
                default:
                    errorMsg = reply->errorString();
                    break;
            }

            if (callback) {
                callback(QJsonObject(), false, errorMsg);
            }

            emit networkError(errorMsg);
        }

        reply->deleteLater();
    });
}
