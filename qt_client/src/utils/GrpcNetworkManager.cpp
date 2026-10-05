#include "utils/GrpcNetworkManager.h"
#include "utils/NetworkManager.h"
#include <spdlog/spdlog.h>
#include <QFile>
#include <QDebug>
#include <QJsonArray>
#include <grpcpp/security/credentials.h>
#include <fstream>
#include <sstream>

GrpcNetworkManager& GrpcNetworkManager::instance() {
    static GrpcNetworkManager instance;
    return instance;
}

GrpcNetworkManager::GrpcNetworkManager(QObject* parent)
    : QObject(parent) {
}

GrpcNetworkManager::~GrpcNetworkManager() {
    m_stub.reset();
    m_channel.reset();
}

bool GrpcNetworkManager::connect(const QString& server_address,
                                const QString& ca_cert_path,
                                const QString& client_cert_path,
                                const QString& client_key_path) {
    qDebug() << "GrpcNetworkManager: Connecting to" << server_address;

    try {
        // Configure mTLS credentials
        grpc::SslCredentialsOptions ssl_opts {};
        ssl_opts.pem_root_certs = readFile(ca_cert_path).toStdString();
        ssl_opts.pem_cert_chain = readFile(client_cert_path).toStdString();
        ssl_opts.pem_private_key = readFile(client_key_path).toStdString();

        const auto creds = grpc::SslCredentials(ssl_opts);

        // Create channel
        m_server_address = server_address;
        m_channel = grpc::CreateChannel(server_address.toStdString(), creds);

        if (!m_channel) {
            const QString error = "Failed to create gRPC channel";
            qWarning() << error;
            emit this->error(error);
            return false;
        }

        // Create stub
        m_stub = gtrade::desktop_gateway::DesktopGateway::NewStub(m_channel);

        if (!m_stub) {
            QString error = "Failed to create gRPC stub";
            qWarning() << error;
            emit this->error(error);
            return false;
        }

        m_connected = true;
        emit connected();

        qDebug() << "GrpcNetworkManager: Connected successfully";
        return true;

    } catch (const std::exception& e) {
        QString error = QString("Connection failed: %1").arg(e.what());
        qWarning() << error;
        emit this->error(error);
        return false;
    }
}

void GrpcNetworkManager::disconnect() {
    if (m_connected) {
        m_stub.reset();
        m_channel.reset();
        m_connected = false;
        m_token.clear();
        m_token_expires_at = 0;
        emit disconnected();
        qDebug() << "GrpcNetworkManager: Disconnected from server";
    }
}

void GrpcNetworkManager::setToken(const QString& token) {
    m_token = token;
    NetworkManager::instance().setToken(token);
}

bool GrpcNetworkManager::connect(const QString& server_address, const QString& public_key) {
    Q_UNUSED(public_key);
    try {
        m_server_address = server_address;
        const auto creds = grpc::InsecureChannelCredentials();
        m_channel = grpc::CreateChannel(server_address.toStdString(), creds);
        if (!m_channel) {
            emit this->error("Failed to create gRPC channel");
            return false;
        }
        m_stub = gtrade::desktop_gateway::DesktopGateway::NewStub(m_channel);
        if (!m_stub) {
            emit this->error("Failed to create gRPC stub");
            return false;
        }
        m_connected = true;
        emit connected();
        return true;
    } catch (const std::exception& e) {
        emit this->error(QString("Connection failed: %1").arg(e.what()));
        return false;
    }
}

void GrpcNetworkManager::setAuthMetadata(ClientContext* context) const {
    if (!m_token.isEmpty()) {
        context->AddMetadata("authorization", ("Bearer " + m_token).toStdString());
    }
}

QJsonObject GrpcNetworkManager::statusToJson(const gtrade::desktop_gateway::Status& status) const {
    QJsonObject obj;
    obj["code"] = status.code();
    obj["success"] = status.success();
    obj["message"] = QString::fromStdString(status.message());
    return obj;
}

QString GrpcNetworkManager::readFile(const QString& path) const {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error("Failed to open file: " + path.toStdString());
    }
    return QString::fromUtf8(file.readAll());
}

// ========== Authentication ==========

void GrpcNetworkManager::login(const QString& username, const QString& password, ResponseCallback callback) {
    SPDLOG_INFO("[Auth] Login attempt for user: {}", username.toStdString());

    if (!m_connected) {
        QString errorMsg = "Not connected to server";
        SPDLOG_ERROR("[Auth] Login failed for user {}: {}", username.toStdString(), errorMsg.toStdString());
        callback(QJsonObject{}, false, errorMsg);
        return;
    }

    gtrade::desktop_gateway::LoginRequest request;
    request.set_username(username.toStdString());
    request.set_password(password.toStdString());

    gtrade::desktop_gateway::LoginResponse response;
    ClientContext context;

    Status status = m_stub->Login(&context, request, &response);

    if (status.ok()) {
        // Debug: Log response details
        bool hasStatus = response.has_status();
        int statusCode = response.status().code();
        bool statusSuccess = response.status().success();
        std::string statusMessage = response.status().message();

        qDebug() << "Login response: has_status=" << hasStatus
                 << "code=" << statusCode
                 << "success=" << statusSuccess
                 << "message=" << QString::fromStdString(statusMessage);

        // Use success field AND check for valid token (code field seems to have transmission issue)
        if (response.status().success() && !response.token().empty()) {
            // Save token
            m_token = QString::fromStdString(response.token());
            m_token_expires_at = response.expires_at();

            qDebug() << "Login SUCCESS - about to call callback";
            SPDLOG_INFO("[Auth] Login successful for user: {}", username.toStdString());

            QJsonObject data;
            data["token"] = m_token;
            data["username"] = username;
            data["expires_at"] = static_cast<qint64>(response.expires_at());

            qDebug() << "Calling callback with success=true";
            callback(data, true, "");
            qDebug() << "Callback returned";
        } else {
            QString errorMsg = QString::fromStdString(response.status().message());
            SPDLOG_WARN("[Auth] Login failed for user {}: {}", username.toStdString(), errorMsg.toStdString());
            callback(QJsonObject{}, false, errorMsg);
        }
    } else {
        QString errorMsg = QString::fromStdString(status.error_message());
        SPDLOG_ERROR("[Auth] Login gRPC error for user {}: {} (code: {})",
                     username.toStdString(), errorMsg.toStdString(),
                     static_cast<int>(status.error_code()));
        callback(QJsonObject{}, false, errorMsg);
    }
}

// ========== Strategy Management ==========

void GrpcNetworkManager::getStrategies(const QString& template_filter, ResponseCallback callback) {
    if (!m_connected) {
        callback(QJsonObject{}, false, "Not connected to server");
        return;
    }

    gtrade::desktop_gateway::GetStrategiesRequest request;
    if (!template_filter.isEmpty()) {
        request.set_template_name(template_filter.toStdString());
    }

    gtrade::desktop_gateway::GetStrategiesResponse response;
    ClientContext context;
    setAuthMetadata(&context);

    Status status = m_stub->GetStrategies(&context, request, &response);

    if (status.ok()) {
        if (response.status().success()) {
            QJsonArray strategies;
            for (const auto& strat : response.strategies()) {
                QJsonObject obj;
                obj["strat_id"] = QString::fromStdString(strat.strat_id());
                obj["template_name"] = QString::fromStdString(strat.template_name());
                obj["status"] = QString::fromStdString(strat.status());
                obj["create_time"] = QString::fromStdString(strat.create_time());
                obj["update_time"] = QString::fromStdString(strat.update_time());
                obj["config_json"] = QString::fromStdString(strat.config_json());
                strategies.append(obj);
            }

            QJsonObject data;
            data["strategies"] = strategies;
            callback(data, true, "");
        } else {
            callback(QJsonObject{}, false, QString::fromStdString(response.status().message()));
        }
    } else {
        callback(QJsonObject{}, false, QString::fromStdString(status.error_message()));
    }
}

void GrpcNetworkManager::getStrategy(const QString& strat_id, ResponseCallback callback) {
    if (!m_connected) {
        callback(QJsonObject{}, false, "Not connected to server");
        return;
    }

    gtrade::desktop_gateway::GetStrategyRequest request;
    request.set_strat_id(strat_id.toStdString());

    gtrade::desktop_gateway::GetStrategyResponse response;
    ClientContext context;
    setAuthMetadata(&context);

    Status status = m_stub->GetStrategy(&context, request, &response);

    if (status.ok()) {
        if (response.status().success()) {
            const auto& strat = response.strategy();
            QJsonObject data;
            data["strat_id"] = QString::fromStdString(strat.strat_id());
            data["template_name"] = QString::fromStdString(strat.template_name());
            data["status"] = QString::fromStdString(strat.status());
            data["create_time"] = QString::fromStdString(strat.create_time());
            data["update_time"] = QString::fromStdString(strat.update_time());
            data["config_json"] = QString::fromStdString(strat.config_json());
            callback(data, true, "");
        } else {
            callback(QJsonObject{}, false, QString::fromStdString(response.status().message()));
        }
    } else {
        callback(QJsonObject{}, false, QString::fromStdString(status.error_message()));
    }
}

void GrpcNetworkManager::addStrategy(const QString& config_json, ResponseCallback callback) {
    if (!m_connected) {
        callback(QJsonObject{}, false, "Not connected to server");
        return;
    }

    gtrade::desktop_gateway::AddStrategyRequest request;
    request.set_config_json(config_json.toStdString());

    gtrade::desktop_gateway::AddStrategyResponse response;
    ClientContext context;
    setAuthMetadata(&context);

    Status status = m_stub->AddStrategy(&context, request, &response);

    if (status.ok()) {
        if (response.status().success()) {
            QJsonObject data;
            data["strat_id"] = QString::fromStdString(response.strat_id());
            callback(data, true, "");
        } else {
            callback(QJsonObject{}, false, QString::fromStdString(response.status().message()));
        }
    } else {
        callback(QJsonObject{}, false, QString::fromStdString(status.error_message()));
    }
}

void GrpcNetworkManager::deleteStrategy(const QString& strat_id, ResponseCallback callback) {
    if (!m_connected) {
        callback(QJsonObject{}, false, "Not connected to server");
        return;
    }

    gtrade::desktop_gateway::DeleteStrategyRequest request;
    request.set_strat_id(strat_id.toStdString());

    gtrade::desktop_gateway::DeleteStrategyResponse response;
    ClientContext context;
    setAuthMetadata(&context);

    Status status = m_stub->DeleteStrategy(&context, request, &response);

    if (status.ok()) {
        if (response.status().success()) {
            callback(QJsonObject{}, true, "");
        } else {
            callback(QJsonObject{}, false, QString::fromStdString(response.status().message()));
        }
    } else {
        callback(QJsonObject{}, false, QString::fromStdString(status.error_message()));
    }
}

void GrpcNetworkManager::startStrategy(const QString& strat_id, ResponseCallback callback) {
    if (!m_connected) {
        callback(QJsonObject{}, false, "Not connected to server");
        return;
    }

    gtrade::desktop_gateway::StartStrategyRequest request;
    request.set_strat_id(strat_id.toStdString());

    gtrade::desktop_gateway::StartStrategyResponse response;
    ClientContext context;
    setAuthMetadata(&context);

    Status status = m_stub->StartStrategy(&context, request, &response);

    if (status.ok()) {
        if (response.status().success()) {
            callback(QJsonObject{}, true, "");
        } else {
            callback(QJsonObject{}, false, QString::fromStdString(response.status().message()));
        }
    } else {
        callback(QJsonObject{}, false, QString::fromStdString(status.error_message()));
    }
}

void GrpcNetworkManager::stopStrategy(const QString& strat_id, ResponseCallback callback) {
    if (!m_connected) {
        callback(QJsonObject{}, false, "Not connected to server");
        return;
    }

    gtrade::desktop_gateway::StopStrategyRequest request;
    request.set_strat_id(strat_id.toStdString());

    gtrade::desktop_gateway::StopStrategyResponse response;
    ClientContext context;
    setAuthMetadata(&context);

    Status status = m_stub->StopStrategy(&context, request, &response);

    if (status.ok()) {
        if (response.status().success()) {
            callback(QJsonObject{}, true, "");
        } else {
            callback(QJsonObject{}, false, QString::fromStdString(response.status().message()));
        }
    } else {
        callback(QJsonObject{}, false, QString::fromStdString(status.error_message()));
    }
}

void GrpcNetworkManager::restartStrategy(const QString& strat_id, ResponseCallback callback) {
    if (!m_connected) {
        callback(QJsonObject{}, false, "Not connected to server");
        return;
    }

    gtrade::desktop_gateway::RestartStrategyRequest request;
    request.set_strat_id(strat_id.toStdString());

    gtrade::desktop_gateway::RestartStrategyResponse response;
    ClientContext context;
    setAuthMetadata(&context);

    Status status = m_stub->RestartStrategy(&context, request, &response);

    if (status.ok()) {
        if (response.status().success()) {
            callback(QJsonObject{}, true, "");
        } else {
            callback(QJsonObject{}, false, QString::fromStdString(response.status().message()));
        }
    } else {
        callback(QJsonObject{}, false, QString::fromStdString(status.error_message()));
    }
}

void GrpcNetworkManager::batchOperation(const QStringList& strat_ids, const QString& operation, ResponseCallback callback) {
    if (!m_connected) {
        callback(QJsonObject{}, false, "Not connected to server");
        return;
    }

    gtrade::desktop_gateway::BatchOperationRequest request;
    request.set_operation(operation.toStdString());
    for (const auto& id : strat_ids) {
        request.add_strat_ids(id.toStdString());
    }

    gtrade::desktop_gateway::BatchOperationResponse response;
    ClientContext context;
    setAuthMetadata(&context);

    Status status = m_stub->BatchOperation(&context, request, &response);

    if (status.ok()) {
        if (response.status().success()) {
            QJsonObject data;
            data["success_count"] = response.success_count();
            data["failed_count"] = response.failed_count();

            QJsonArray failed_ids;
            for (const auto& id : response.failed_ids()) {
                failed_ids.append(QString::fromStdString(id));
            }
            data["failed_ids"] = failed_ids;

            callback(data, true, "");
        } else {
            callback(QJsonObject{}, false, QString::fromStdString(response.status().message()));
        }
    } else {
        callback(QJsonObject{}, false, QString::fromStdString(status.error_message()));
    }
}

// ========== Order Management ==========

void GrpcNetworkManager::getOrders(const QString& strat_id, const QString& instrument, const QString& status_filter,
                                  int page, int page_size, ResponseCallback callback) {
    if (!m_connected) {
        callback(QJsonObject{}, false, "Not connected to server");
        return;
    }

    gtrade::desktop_gateway::GetOrdersRequest request;
    if (!strat_id.isEmpty()) request.set_strat_id(strat_id.toStdString());
    if (!instrument.isEmpty()) request.set_instrument(instrument.toStdString());
    if (!status_filter.isEmpty()) request.set_status(status_filter.toStdString());
    request.set_page(page);
    request.set_page_size(page_size);

    gtrade::desktop_gateway::GetOrdersResponse response;
    ClientContext context;
    setAuthMetadata(&context);

    Status status = m_stub->GetOrders(&context, request, &response);

    if (status.ok()) {
        if (response.status().success()) {
            QJsonArray orders;
            for (const auto& order : response.orders()) {
                QJsonObject obj;
                obj["order_id"] = QString::fromStdString(order.order_id());
                obj["strat_id"] = QString::fromStdString(order.strat_id());
                obj["instrument"] = QString::fromStdString(order.instrument());
                obj["side"] = QString::fromStdString(order.side());
                obj["order_type"] = QString::fromStdString(order.order_type());
                obj["price"] = order.price();
                obj["quantity"] = order.quantity();
                obj["status"] = QString::fromStdString(order.status());
                obj["create_time"] = QString::fromStdString(order.create_time());
                obj["update_time"] = QString::fromStdString(order.update_time());
                orders.append(obj);
            }

            QJsonObject data;
            data["orders"] = orders;
            data["total"] = response.total();
            data["page"] = response.page();
            data["page_size"] = response.page_size();

            callback(data, true, "");
        } else {
            callback(QJsonObject{}, false, QString::fromStdString(response.status().message()));
        }
    } else {
        callback(QJsonObject{}, false, QString::fromStdString(status.error_message()));
    }
}

// ========== Trade Management ==========

void GrpcNetworkManager::getTrades(const QString& strat_id, const QString& instrument,
                                  int page, int page_size, ResponseCallback callback) {
    if (!m_connected) {
        callback(QJsonObject{}, false, "Not connected to server");
        return;
    }

    gtrade::desktop_gateway::GetTradesRequest request;
    if (!strat_id.isEmpty()) request.set_strat_id(strat_id.toStdString());
    if (!instrument.isEmpty()) request.set_instrument(instrument.toStdString());
    request.set_page(page);
    request.set_page_size(page_size);

    gtrade::desktop_gateway::GetTradesResponse response;
    ClientContext context;
    setAuthMetadata(&context);

    Status status = m_stub->GetTrades(&context, request, &response);

    if (status.ok()) {
        if (response.status().success()) {
            QJsonArray trades;
            for (const auto& trade : response.trades()) {
                QJsonObject obj;
                obj["trade_id"] = QString::fromStdString(trade.trade_id());
                obj["order_id"] = QString::fromStdString(trade.order_id());
                obj["strat_id"] = QString::fromStdString(trade.strat_id());
                obj["instrument"] = QString::fromStdString(trade.instrument());
                obj["side"] = QString::fromStdString(trade.side());
                obj["price"] = trade.price();
                obj["quantity"] = trade.quantity();
                obj["fee"] = trade.fee();
                obj["trade_time"] = QString::fromStdString(trade.trade_time());
                trades.append(obj);
            }

            QJsonObject data;
            data["trades"] = trades;
            data["total"] = response.total();
            data["page"] = response.page();
            data["page_size"] = response.page_size();

            callback(data, true, "");
        } else {
            callback(QJsonObject{}, false, QString::fromStdString(response.status().message()));
        }
    } else {
        callback(QJsonObject{}, false, QString::fromStdString(status.error_message()));
    }
}

// ========== Position Management ==========

void GrpcNetworkManager::getPositions(const QString& strat_id, const QString& instrument, ResponseCallback callback) {
    if (!m_connected) {
        callback(QJsonObject{}, false, "Not connected to server");
        return;
    }

    gtrade::desktop_gateway::GetPositionsRequest request;
    if (!strat_id.isEmpty()) request.set_strat_id(strat_id.toStdString());
    if (!instrument.isEmpty()) request.set_instrument(instrument.toStdString());

    gtrade::desktop_gateway::GetPositionsResponse response;
    ClientContext context;
    setAuthMetadata(&context);

    Status status = m_stub->GetPositions(&context, request, &response);

    if (status.ok()) {
        if (response.status().success()) {
            QJsonArray positions;
            for (const auto& pos : response.positions()) {
                QJsonObject obj;
                obj["strat_id"] = QString::fromStdString(pos.strat_id());
                obj["instrument"] = QString::fromStdString(pos.instrument());
                obj["quantity"] = pos.quantity();
                obj["avg_price"] = pos.avg_price();
                obj["unrealized_pnl"] = pos.unrealized_pnl();
                obj["realized_pnl"] = pos.realized_pnl();
                obj["update_time"] = QString::fromStdString(pos.update_time());
                positions.append(obj);
            }

            QJsonObject data;
            data["positions"] = positions;
            callback(data, true, "");
        } else {
            callback(QJsonObject{}, false, QString::fromStdString(response.status().message()));
        }
    } else {
        callback(QJsonObject{}, false, QString::fromStdString(status.error_message()));
    }
}

// ========== Template Management ==========

void GrpcNetworkManager::getTemplates(ResponseCallback callback) {
    if (!m_connected) {
        callback(QJsonObject{}, false, "Not connected to server");
        return;
    }

    gtrade::desktop_gateway::GetTemplatesRequest request;
    gtrade::desktop_gateway::GetTemplatesResponse response;
    ClientContext context;
    setAuthMetadata(&context);

    Status status = m_stub->GetTemplates(&context, request, &response);

    if (status.ok()) {
        if (response.status().success()) {
            QJsonArray templates;
            for (const auto& tmpl : response.template_names()) {
                templates.append(QString::fromStdString(tmpl));
            }

            QJsonObject data;
            data["templates"] = templates;
            callback(data, true, "");
        } else {
            callback(QJsonObject{}, false, QString::fromStdString(response.status().message()));
        }
    } else {
        callback(QJsonObject{}, false, QString::fromStdString(status.error_message()));
    }
}

void GrpcNetworkManager::getTemplateConfig(const QString& template_name, ResponseCallback callback) {
    if (!m_connected) {
        callback(QJsonObject{}, false, "Not connected to server");
        return;
    }

    gtrade::desktop_gateway::GetTemplateConfigRequest request;
    request.set_template_name(template_name.toStdString());

    gtrade::desktop_gateway::GetTemplateConfigResponse response;
    ClientContext context;
    setAuthMetadata(&context);

    Status status = m_stub->GetTemplateConfig(&context, request, &response);

    if (status.ok()) {
        if (response.status().success()) {
            QJsonObject data;
            data["config_json"] = QString::fromStdString(response.config_json());
            callback(data, true, "");
        } else {
            callback(QJsonObject{}, false, QString::fromStdString(response.status().message()));
        }
    } else {
        callback(QJsonObject{}, false, QString::fromStdString(status.error_message()));
    }
}

// ========== Dictionary ==========

void GrpcNetworkManager::getDictionary(ResponseCallback callback) {
    if (!m_connected) {
        callback(QJsonObject{}, false, "Not connected to server");
        return;
    }

    gtrade::desktop_gateway::GetDictionaryRequest request;
    gtrade::desktop_gateway::GetDictionaryResponse response;
    ClientContext context;
    setAuthMetadata(&context);

    Status status = m_stub->GetDictionary(&context, request, &response);

    if (status.ok()) {
        if (response.status().success()) {
            QJsonArray groups;
            for (const auto& group : response.groups()) {
                QJsonObject group_obj;
                group_obj["name"] = QString::fromStdString(group.name());

                QJsonArray items;
                for (const auto& item : group.items()) {
                    QJsonObject item_obj;
                    item_obj["key"] = QString::fromStdString(item.key());
                    item_obj["value"] = QString::fromStdString(item.value());
                    item_obj["label"] = QString::fromStdString(item.label());
                    item_obj["color"] = QString::fromStdString(item.color());
                    items.append(item_obj);
                }
                group_obj["items"] = items;
                groups.append(group_obj);
            }

            QJsonObject data;
            data["groups"] = groups;
            callback(data, true, "");
        } else {
            callback(QJsonObject{}, false, QString::fromStdString(response.status().message()));
        }
    } else {
        callback(QJsonObject{}, false, QString::fromStdString(status.error_message()));
    }
}

void GrpcNetworkManager::get(const QString& path, ResponseCallback callback, const QMap<QString, QString>& params) {
    NetworkManager::instance().get(NetworkManager::Backend::WebServer, path, callback, params);
}

void GrpcNetworkManager::post(const QString& path, const QJsonObject& data, ResponseCallback callback) {
    NetworkManager::instance().post(NetworkManager::Backend::WebServer, path, data, callback);
}

void GrpcNetworkManager::del(const QString& path, ResponseCallback callback) {
    NetworkManager::instance().del(NetworkManager::Backend::WebServer, path, callback);
}
