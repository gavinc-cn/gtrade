//
// Desktop Gateway gRPC Service Implementation
//

#include "desktop_gateway_grpc_service.h"
#include "strategy_engine.h"
#include "mysql_gateway.h"
#include "i_strategy_engine.h"
#include "zrtools/zrt_misc.h"
#include "jwt-cpp/jwt.h"
#include <grpcpp/security/server_credentials.h>
#include <fstream>
#include <sstream>
#include <chrono>

using grpc::Status;
using grpc::StatusCode;

// Helper function to read file contents
static std::string ReadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

DesktopGatewayGrpcService::DesktopGatewayGrpcService(ServiceMap& service_map, const GTradeConfig& gtrade_cfg)
    : m_service_map(service_map), m_gtrade_cfg(gtrade_cfg) {
}

bool DesktopGatewayGrpcService::Init() {
    SPDLOG_INFO("{}", __PRETTY_FUNCTION__);

    // Get service references
    m_strategy_engine = m_service_map.at(k_StrategyEngine).get();
    m_mysql_gateway = m_service_map.at(k_MySqlGateway).get();

    // Load configuration from GTradeConfig
    m_server_address = m_gtrade_cfg.desktop_gateway_endpoint;  // e.g., "0.0.0.0:50051"
    m_jwt_secret = m_gtrade_cfg.desktop_gateway_jwt_secret;
    m_jwt_expiration = m_gtrade_cfg.desktop_gateway_jwt_expiration;
    m_ca_cert_path = m_gtrade_cfg.desktop_gateway_ca_cert;
    m_server_cert_path = m_gtrade_cfg.desktop_gateway_server_cert;
    m_server_key_path = m_gtrade_cfg.desktop_gateway_server_key;

    SPDLOG_INFO("Desktop Gateway gRPC Service initialized");
    SPDLOG_INFO("Server Address: {}", m_server_address);
    SPDLOG_INFO("CA Cert: {}", m_ca_cert_path);
    SPDLOG_INFO("Server Cert: {}", m_server_cert_path);
    SPDLOG_INFO("JWT Expiration: {} seconds", m_jwt_expiration);

    return true;
}

bool DesktopGatewayGrpcService::Start() {
    SPDLOG_INFO("Starting Desktop Gateway gRPC Server on {}", m_server_address);

    try {
        // Read mTLS certificates
        std::string ca_cert = ReadFile(m_ca_cert_path);
        std::string server_cert = ReadFile(m_server_cert_path);
        std::string server_key = ReadFile(m_server_key_path);

        // Configure mTLS
        grpc::SslServerCredentialsOptions::PemKeyCertPair key_cert_pair;
        key_cert_pair.private_key = server_key;
        key_cert_pair.cert_chain = server_cert;

        grpc::SslServerCredentialsOptions ssl_opts;
        ssl_opts.pem_root_certs = ca_cert;
        ssl_opts.pem_key_cert_pairs.push_back(key_cert_pair);
        // Require client certificate authentication
        ssl_opts.client_certificate_request = GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;

        auto server_creds = grpc::SslServerCredentials(ssl_opts);

        // Build and start server
        ServerBuilder builder;
        builder.AddListeningPort(m_server_address, server_creds);
        builder.RegisterService(this);

        m_server = builder.BuildAndStart();

        if (!m_server) {
            SPDLOG_ERROR("Failed to start gRPC server");
            return false;
        }

        SPDLOG_INFO("Desktop Gateway gRPC Server started successfully");
        return true;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to start gRPC server: {}", e.what());
        return false;
    }
}

void DesktopGatewayGrpcService::Stop() {
    if (m_server) {
        SPDLOG_INFO("Stopping Desktop Gateway gRPC Server");
        m_server->Shutdown();
        m_server->Wait();
        SPDLOG_INFO("Desktop Gateway gRPC Server stopped");
    }
}

// ===== Authentication =====

Status DesktopGatewayGrpcService::Login(
    ServerContext* context,
    const gtrade::desktop_gateway::LoginRequest* request,
    gtrade::desktop_gateway::LoginResponse* response) {

    SPDLOG_INFO("Login request for user: {}", request->username());

    // Validate credentials
    if (request->username() != m_gtrade_cfg.user || request->password() != m_gtrade_cfg.password) {
        SetError(response->mutable_status(), 401, "Invalid credentials");
        SPDLOG_WARN("Login failed for user: {}", request->username());
        return Status::OK;  // gRPC call succeeded, but application-level auth failed
    }

    // Generate JWT token
    std::string token = GenerateToken(request->username());
    auto now = std::chrono::system_clock::now();
    auto exp = now + std::chrono::seconds(m_jwt_expiration);
    auto exp_time = std::chrono::system_clock::to_time_t(exp);

    // Set response
    SetSuccess(response->mutable_status(), "Login successful");
    response->set_token(token);
    response->set_expires_at(exp_time);

    // Debug: Verify response fields
    SPDLOG_INFO("Login response set: code={}, success={}, message='{}'",
                response->status().code(),
                response->status().success(),
                response->status().message());
    SPDLOG_INFO("User '{}' logged in successfully", request->username());
    return Status::OK;
}

std::string DesktopGatewayGrpcService::GenerateToken(const std::string& username) const {
    auto now = std::chrono::system_clock::now();
    auto exp = now + std::chrono::seconds(m_jwt_expiration);

    auto token = jwt::create()
        .set_issuer("gtrade-desktop-gateway")
        .set_type("JWT")
        .set_issued_at(now)
        .set_expires_at(exp)
        .set_payload_claim("username", jwt::claim(username))
        .sign(jwt::algorithm::hs256{m_jwt_secret});

    return token;
}

bool DesktopGatewayGrpcService::ValidateToken(const std::string& token, std::string& username) const {
    try {
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{m_jwt_secret})
            .with_issuer("gtrade-desktop-gateway");

        auto decoded = jwt::decode(token);
        verifier.verify(decoded);

        // Extract username
        if (decoded.has_payload_claim("username")) {
            username = decoded.get_payload_claim("username").as_string();
            return true;
        }

        return false;
    } catch (const std::exception& e) {
        SPDLOG_WARN("Token validation failed: {}", e.what());
        return false;
    }
}

bool DesktopGatewayGrpcService::CheckAuth(ServerContext* context, gtrade::desktop_gateway::Status* status) const {
    // Extract metadata (headers)
    const auto& metadata = context->client_metadata();
    auto auth_header = metadata.find("authorization");

    if (auth_header == metadata.end()) {
        SetError(status, 401, "Missing authorization header");
        return false;
    }

    std::string auth_value(auth_header->second.data(), auth_header->second.size());

    // Extract token from "Bearer <token>"
    if (auth_value.find("Bearer ") != 0) {
        SetError(status, 401, "Invalid authorization header format");
        return false;
    }

    std::string token = auth_value.substr(7);  // Skip "Bearer "
    std::string username;

    if (!ValidateToken(token, username)) {
        SetError(status, 401, "Invalid or expired token");
        return false;
    }

    return true;
}

// ===== Helper Functions =====

void DesktopGatewayGrpcService::SetSuccess(gtrade::desktop_gateway::Status* status, const std::string& message) const {
    status->set_code(200);
    status->set_success(true);
    status->set_message(message.empty() ? "Success" : message);
}

void DesktopGatewayGrpcService::SetError(gtrade::desktop_gateway::Status* status, int code, const std::string& message) const {
    status->set_code(code);
    status->set_success(false);
    status->set_message(message);
}

// ===== Strategy Management =====

Status DesktopGatewayGrpcService::GetStrategies(
    ServerContext* context,
    const gtrade::desktop_gateway::GetStrategiesRequest* request,
    gtrade::desktop_gateway::GetStrategiesResponse* response) {

    if (!CheckAuth(context, response->mutable_status())) {
        return Status::OK;
    }

    // TODO: Query from MySQL via MySqlGateway
    // For now, return empty list
    SetSuccess(response->mutable_status());

    SPDLOG_DEBUG("GetStrategies: filter={}", request->template_name());
    return Status::OK;
}

Status DesktopGatewayGrpcService::GetStrategy(
    ServerContext* context,
    const gtrade::desktop_gateway::GetStrategyRequest* request,
    gtrade::desktop_gateway::GetStrategyResponse* response) {

    if (!CheckAuth(context, response->mutable_status())) {
        return Status::OK;
    }

    // TODO: Query from MySQL
    SetError(response->mutable_status(), 500, "Not implemented yet");

    SPDLOG_DEBUG("GetStrategy: strat_id={}", request->strat_id());
    return Status::OK;
}

Status DesktopGatewayGrpcService::AddStrategy(
    ServerContext* context,
    const gtrade::desktop_gateway::AddStrategyRequest* request,
    gtrade::desktop_gateway::AddStrategyResponse* response) {

    if (!CheckAuth(context, response->mutable_status())) {
        return Status::OK;
    }

    SPDLOG_INFO("AddStrategy request");

    // Parse config JSON to extract config_path
    // For now, assume config_json contains a "config_path" field
    // In production, you would parse the JSON and create the strategy config file

    // Create message buffer for StrategyEngine
    HttpAddStrategyReq http_req{};
    // TODO: Extract config_path from request->config_json()
    // For now, use a placeholder
    std::string config_path = "/tmp/new_strategy.yml";  // Placeholder
    zrt::fill_field(http_req.config_path, config_path);
    auto req_buf = std::make_shared<TBuffer>(http_req);

    // Synchronous call to StrategyEngine
    BufPtr rsp_buf{};
    m_strategy_engine->PostSyncMsg(kHttpAddStrategy, req_buf, rsp_buf);

    // Parse response
    const auto& rsp = *reinterpret_cast<const HttpStrategyOperationRsp*>(rsp_buf->Data());

    if (rsp.success) {
        SetSuccess(response->mutable_status(), "Strategy added successfully");
        response->set_strat_id("NEW_ID");  // TODO: Get actual strategy ID
        SPDLOG_INFO("Strategy added successfully");
    } else {
        SetError(response->mutable_status(), 500, "Failed to add strategy");
    }

    return Status::OK;
}

Status DesktopGatewayGrpcService::DeleteStrategy(
    ServerContext* context,
    const gtrade::desktop_gateway::DeleteStrategyRequest* request,
    gtrade::desktop_gateway::DeleteStrategyResponse* response) {

    if (!CheckAuth(context, response->mutable_status())) {
        return Status::OK;
    }

    SPDLOG_INFO("DeleteStrategy: strat_id={}", request->strat_id());

    // Convert strat_id string to int
    int id = std::stoi(request->strat_id());

    HttpDeleteStrategyReq http_req{};
    zrt::fill_field(http_req.strat_id, id);
    auto req_buf = std::make_shared<TBuffer>(http_req);

    BufPtr rsp_buf{};
    m_strategy_engine->PostSyncMsg(kHttpDeleteStrategy, req_buf, rsp_buf);

    const auto& rsp = *reinterpret_cast<const HttpStrategyOperationRsp*>(rsp_buf->Data());

    if (rsp.success) {
        SetSuccess(response->mutable_status(), "Strategy deleted successfully");
        SPDLOG_INFO("Strategy deleted: ID={}", id);
    } else {
        SetError(response->mutable_status(), 500, "Failed to delete strategy");
    }

    return Status::OK;
}

Status DesktopGatewayGrpcService::StartStrategy(
    ServerContext* context,
    const gtrade::desktop_gateway::StartStrategyRequest* request,
    gtrade::desktop_gateway::StartStrategyResponse* response) {

    if (!CheckAuth(context, response->mutable_status())) {
        return Status::OK;
    }

    SPDLOG_INFO("StartStrategy: strat_id={}", request->strat_id());

    int id = std::stoi(request->strat_id());

    HttpStartStrategyReq http_req{};
    zrt::fill_field(http_req.strat_id, id);
    auto req_buf = std::make_shared<TBuffer>(http_req);

    BufPtr rsp_buf{};
    m_strategy_engine->PostSyncMsg(kHttpStartStrategy, req_buf, rsp_buf);

    const auto& rsp = *reinterpret_cast<const HttpStrategyOperationRsp*>(rsp_buf->Data());

    if (rsp.success) {
        SetSuccess(response->mutable_status(), "Strategy started successfully");
        SPDLOG_INFO("Strategy started: ID={}", id);
    } else {
        SetError(response->mutable_status(), 500, "Failed to start strategy");
    }

    return Status::OK;
}

Status DesktopGatewayGrpcService::StopStrategy(
    ServerContext* context,
    const gtrade::desktop_gateway::StopStrategyRequest* request,
    gtrade::desktop_gateway::StopStrategyResponse* response) {

    if (!CheckAuth(context, response->mutable_status())) {
        return Status::OK;
    }

    SPDLOG_INFO("StopStrategy: strat_id={}", request->strat_id());

    int id = std::stoi(request->strat_id());

    HttpStopStrategyReq http_req{};
    zrt::fill_field(http_req.strat_id, id);
    auto req_buf = std::make_shared<TBuffer>(http_req);

    BufPtr rsp_buf{};
    m_strategy_engine->PostSyncMsg(kHttpStopStrategy, req_buf, rsp_buf);

    const auto& rsp = *reinterpret_cast<const HttpStrategyOperationRsp*>(rsp_buf->Data());

    if (rsp.success) {
        SetSuccess(response->mutable_status(), "Strategy stopped successfully");
        SPDLOG_INFO("Strategy stopped: ID={}", id);
    } else {
        SetError(response->mutable_status(), 500, "Failed to stop strategy");
    }

    return Status::OK;
}

Status DesktopGatewayGrpcService::RestartStrategy(
    ServerContext* context,
    const gtrade::desktop_gateway::RestartStrategyRequest* request,
    gtrade::desktop_gateway::RestartStrategyResponse* response) {

    if (!CheckAuth(context, response->mutable_status())) {
        return Status::OK;
    }

    SPDLOG_INFO("RestartStrategy: strat_id={}", request->strat_id());

    int id = std::stoi(request->strat_id());

    HttpRestartStrategyReq http_req{};
    zrt::fill_field(http_req.strat_id, id);
    auto req_buf = std::make_shared<TBuffer>(http_req);

    BufPtr rsp_buf{};
    m_strategy_engine->PostSyncMsg(kHttpRestartStrategy, req_buf, rsp_buf);

    const auto& rsp = *reinterpret_cast<const HttpStrategyOperationRsp*>(rsp_buf->Data());

    if (rsp.success) {
        SetSuccess(response->mutable_status(), "Strategy restarted successfully");
        SPDLOG_INFO("Strategy restarted: ID={}", id);
    } else {
        SetError(response->mutable_status(), 500, "Failed to restart strategy");
    }

    return Status::OK;
}

Status DesktopGatewayGrpcService::BatchOperation(
    ServerContext* context,
    const gtrade::desktop_gateway::BatchOperationRequest* request,
    gtrade::desktop_gateway::BatchOperationResponse* response) {

    if (!CheckAuth(context, response->mutable_status())) {
        return Status::OK;
    }

    SPDLOG_INFO("BatchOperation: operation={}, count={}", request->operation(), request->strat_ids_size());

    int success_count = 0;
    int failed_count = 0;

    for (const auto& strat_id : request->strat_ids()) {
        int id = std::stoi(strat_id);

        // Determine message type
        int msg_type;
        BufPtr req_buf;

        if (request->operation() == "start") {
            msg_type = kHttpStartStrategy;
            HttpStartStrategyReq http_req{};
            zrt::fill_field(http_req.strat_id, id);
            req_buf = std::make_shared<TBuffer>(http_req);
        } else if (request->operation() == "stop") {
            msg_type = kHttpStopStrategy;
            HttpStopStrategyReq http_req{};
            zrt::fill_field(http_req.strat_id, id);
            req_buf = std::make_shared<TBuffer>(http_req);
        } else if (request->operation() == "restart") {
            msg_type = kHttpRestartStrategy;
            HttpRestartStrategyReq http_req{};
            zrt::fill_field(http_req.strat_id, id);
            req_buf = std::make_shared<TBuffer>(http_req);
        } else if (request->operation() == "delete") {
            msg_type = kHttpDeleteStrategy;
            HttpDeleteStrategyReq http_req{};
            zrt::fill_field(http_req.strat_id, id);
            req_buf = std::make_shared<TBuffer>(http_req);
        } else {
            continue;  // Skip invalid operations
        }

        BufPtr rsp_buf{};
        m_strategy_engine->PostSyncMsg(msg_type, req_buf, rsp_buf);

        const auto& rsp = *reinterpret_cast<const HttpStrategyOperationRsp*>(rsp_buf->Data());

        if (rsp.success) {
            success_count++;
        } else {
            failed_count++;
            response->add_failed_ids(strat_id);
        }
    }

    SetSuccess(response->mutable_status());
    response->set_success_count(success_count);
    response->set_failed_count(failed_count);

    SPDLOG_INFO("Batch {} operation: {} success, {} failed",
                request->operation(), success_count, failed_count);

    return Status::OK;
}

// ===== Order Management =====

Status DesktopGatewayGrpcService::GetOrders(
    ServerContext* context,
    const gtrade::desktop_gateway::GetOrdersRequest* request,
    gtrade::desktop_gateway::GetOrdersResponse* response) {

    if (!CheckAuth(context, response->mutable_status())) {
        return Status::OK;
    }

    // TODO: Query from MySQL
    SetSuccess(response->mutable_status());
    response->set_total(0);
    response->set_page(request->page());
    response->set_page_size(request->page_size());

    SPDLOG_DEBUG("GetOrders: strat_id={}, instrument={}, page={}",
                 request->strat_id(), request->instrument(), request->page());

    return Status::OK;
}

// ===== Trade Management =====

Status DesktopGatewayGrpcService::GetTrades(
    ServerContext* context,
    const gtrade::desktop_gateway::GetTradesRequest* request,
    gtrade::desktop_gateway::GetTradesResponse* response) {

    if (!CheckAuth(context, response->mutable_status())) {
        return Status::OK;
    }

    // TODO: Query from MySQL
    SetSuccess(response->mutable_status());
    response->set_total(0);
    response->set_page(request->page());
    response->set_page_size(request->page_size());

    SPDLOG_DEBUG("GetTrades: strat_id={}, instrument={}, page={}",
                 request->strat_id(), request->instrument(), request->page());

    return Status::OK;
}

// ===== Position Management =====

Status DesktopGatewayGrpcService::GetPositions(
    ServerContext* context,
    const gtrade::desktop_gateway::GetPositionsRequest* request,
    gtrade::desktop_gateway::GetPositionsResponse* response) {

    if (!CheckAuth(context, response->mutable_status())) {
        return Status::OK;
    }

    // TODO: Query from MySQL
    SetSuccess(response->mutable_status());

    SPDLOG_DEBUG("GetPositions: strat_id={}, instrument={}",
                 request->strat_id(), request->instrument());

    return Status::OK;
}

// ===== Template Management =====

Status DesktopGatewayGrpcService::GetTemplates(
    ServerContext* context,
    const gtrade::desktop_gateway::GetTemplatesRequest* request,
    gtrade::desktop_gateway::GetTemplatesResponse* response) {

    if (!CheckAuth(context, response->mutable_status())) {
        return Status::OK;
    }

    // TODO: Scan strategy_config directory for template files
    SetSuccess(response->mutable_status());

    // Example templates (should be read from filesystem)
    response->add_template_names("demo");
    response->add_template_names("future_arbi");
    response->add_template_names("sma");

    SPDLOG_DEBUG("GetTemplates");

    return Status::OK;
}

Status DesktopGatewayGrpcService::GetTemplateConfig(
    ServerContext* context,
    const gtrade::desktop_gateway::GetTemplateConfigRequest* request,
    gtrade::desktop_gateway::GetTemplateConfigResponse* response) {

    if (!CheckAuth(context, response->mutable_status())) {
        return Status::OK;
    }

    // TODO: Read template config file and return as JSON
    SetError(response->mutable_status(), 500, "Not implemented yet");

    SPDLOG_DEBUG("GetTemplateConfig: template={}", request->template_name());

    return Status::OK;
}

// ===== Dictionary =====

Status DesktopGatewayGrpcService::GetDictionary(
    ServerContext* context,
    const gtrade::desktop_gateway::GetDictionaryRequest* request,
    gtrade::desktop_gateway::GetDictionaryResponse* response) {

    if (!CheckAuth(context, response->mutable_status())) {
        return Status::OK;
    }

    SetSuccess(response->mutable_status());

    // Add status dictionary
    auto* status_group = response->add_groups();
    status_group->set_name("status");

    auto* status_running = status_group->add_items();
    status_running->set_key("running");
    status_running->set_value("running");
    status_running->set_label("运行中");
    status_running->set_color("success");

    auto* status_stopped = status_group->add_items();
    status_stopped->set_key("stopped");
    status_stopped->set_value("stopped");
    status_stopped->set_label("已停止");
    status_stopped->set_color("info");

    auto* status_error = status_group->add_items();
    status_error->set_key("error");
    status_error->set_value("error");
    status_error->set_label("错误");
    status_error->set_color("danger");

    // Add side dictionary
    auto* side_group = response->add_groups();
    side_group->set_name("side");

    auto* buy = side_group->add_items();
    buy->set_key("buy");
    buy->set_value("buy");
    buy->set_label("买入");
    buy->set_color("success");

    auto* sell = side_group->add_items();
    sell->set_key("sell");
    sell->set_value("sell");
    sell->set_label("卖出");
    sell->set_color("danger");

    SPDLOG_DEBUG("GetDictionary");

    return Status::OK;
}
