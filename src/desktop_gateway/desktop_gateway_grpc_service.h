//
// Desktop Gateway gRPC Service
// Provides gRPC interface for Qt client communication with mTLS authentication
//

#pragma once

#include "pch.h"
#include "type_define.h"
#include "service_map.h"
#include "desktop_gateway.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/server_credentials.h>
#include <memory>
#include <string>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using gtrade::desktop_gateway::DesktopGateway;

class StrategyEngine;
class MySqlGateway;

class DesktopGatewayGrpcService final : public MyHandler, public DesktopGateway::Service {
public:
    DesktopGatewayGrpcService(ServiceMap& service_map, const GTradeConfig& gtrade_cfg);
    ~DesktopGatewayGrpcService() override = default;

    // MyHandler interface implementation
    bool Init() override;
    bool Start() override;
    void Stop() override;

    // Authentication
    Status Login(ServerContext* context,
                const gtrade::desktop_gateway::LoginRequest* request,
                gtrade::desktop_gateway::LoginResponse* response) override;

    // Strategy management
    Status GetStrategies(ServerContext* context,
                        const gtrade::desktop_gateway::GetStrategiesRequest* request,
                        gtrade::desktop_gateway::GetStrategiesResponse* response) override;

    Status GetStrategy(ServerContext* context,
                      const gtrade::desktop_gateway::GetStrategyRequest* request,
                      gtrade::desktop_gateway::GetStrategyResponse* response) override;

    Status AddStrategy(ServerContext* context,
                      const gtrade::desktop_gateway::AddStrategyRequest* request,
                      gtrade::desktop_gateway::AddStrategyResponse* response) override;

    Status DeleteStrategy(ServerContext* context,
                         const gtrade::desktop_gateway::DeleteStrategyRequest* request,
                         gtrade::desktop_gateway::DeleteStrategyResponse* response) override;

    Status StartStrategy(ServerContext* context,
                        const gtrade::desktop_gateway::StartStrategyRequest* request,
                        gtrade::desktop_gateway::StartStrategyResponse* response) override;

    Status StopStrategy(ServerContext* context,
                       const gtrade::desktop_gateway::StopStrategyRequest* request,
                       gtrade::desktop_gateway::StopStrategyResponse* response) override;

    Status RestartStrategy(ServerContext* context,
                          const gtrade::desktop_gateway::RestartStrategyRequest* request,
                          gtrade::desktop_gateway::RestartStrategyResponse* response) override;

    Status BatchOperation(ServerContext* context,
                         const gtrade::desktop_gateway::BatchOperationRequest* request,
                         gtrade::desktop_gateway::BatchOperationResponse* response) override;

    // Order management
    Status GetOrders(ServerContext* context,
                    const gtrade::desktop_gateway::GetOrdersRequest* request,
                    gtrade::desktop_gateway::GetOrdersResponse* response) override;

    // Trade management
    Status GetTrades(ServerContext* context,
                    const gtrade::desktop_gateway::GetTradesRequest* request,
                    gtrade::desktop_gateway::GetTradesResponse* response) override;

    // Position management
    Status GetPositions(ServerContext* context,
                       const gtrade::desktop_gateway::GetPositionsRequest* request,
                       gtrade::desktop_gateway::GetPositionsResponse* response) override;

    // Template management
    Status GetTemplates(ServerContext* context,
                       const gtrade::desktop_gateway::GetTemplatesRequest* request,
                       gtrade::desktop_gateway::GetTemplatesResponse* response) override;

    Status GetTemplateConfig(ServerContext* context,
                            const gtrade::desktop_gateway::GetTemplateConfigRequest* request,
                            gtrade::desktop_gateway::GetTemplateConfigResponse* response) override;

    // Dictionary
    Status GetDictionary(ServerContext* context,
                        const gtrade::desktop_gateway::GetDictionaryRequest* request,
                        gtrade::desktop_gateway::GetDictionaryResponse* response) override;

private:
    // Authentication helpers
    bool ValidateToken(const std::string& token, std::string& username) const;
    std::string GenerateToken(const std::string& username) const;
    bool CheckAuth(ServerContext* context, gtrade::desktop_gateway::Status* status) const;

    // Response helpers
    void SetSuccess(gtrade::desktop_gateway::Status* status, const std::string& message = "") const;
    void SetError(gtrade::desktop_gateway::Status* status, int code, const std::string& message) const;

    // Service references
    ServiceMap& m_service_map;
    GTradeConfig m_gtrade_cfg;
    MyHandler* m_strategy_engine{};
    MyHandler* m_mysql_gateway{};

    // gRPC server
    std::unique_ptr<Server> m_server;
    std::string m_server_address;

    // mTLS configuration
    std::string m_ca_cert_path;
    std::string m_server_cert_path;
    std::string m_server_key_path;

    // JWT configuration
    std::string m_jwt_secret;
    int m_jwt_expiration{86400};  // 24 hours
};
