#include "slack_client.h"
#include "sonic_helper.h"
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include <yaml-cpp/yaml.h>
#include <fstream>

SlackClient::SlackClient() = default;
SlackClient::~SlackClient() = default;

void SlackClient::Init(const std::string& yaml_file_path) {
    YAML::Node config = YAML::LoadFile(yaml_file_path);

    for (const auto& channel : config["channels"]) {
        m_channels[channel.first.as<std::string>()] = channel.second.as<std::string>();
    }
    SPDLOG_INFO("SlackClient initialized: ", zrt::to_str(m_channels));

    m_default_channel = config["default_channel"].as<std::string>();
    SPDLOG_DEBUG("Loaded default Slack channel: {}", m_default_channel);
}

std::string SlackClient::FormatMessage(const std::string& subject, const std::string& content) const {
    std::string text {};
    if (const size_t total_len = subject.length() + content.length();
        total_len > 2048) {
        // Long message format
        if (!subject.empty()) {
            text = fmt::format("{}\n{}\n\n{}",
                             std::string(50, '-'), subject, content);
        } else {
            text = content;
        }
    } else if (!subject.empty() && !content.empty()) {
        // Normal format with both subject and content
        text = fmt::format("```{}\n\n{}\n```", subject, content);
    } else {
        // Only subject or content
        text = fmt::format("```{}\n```", subject.empty() ? content : subject);
    }
    return text;
}

bool SlackClient::SendToWebhook(const std::string& url, const std::string& text) {
    try {
        // Parse URL to get host and path
        std::string host {};
        std::string path {};
        size_t protocol_pos = url.find("://");
        if (protocol_pos == std::string::npos) {
            SPDLOG_ERROR("Invalid webhook URL: {}", url);
            return false;
        }

        std::string url_without_protocol = url.substr(protocol_pos + 3);

        if (size_t path_pos = url_without_protocol.find('/');
            path_pos == std::string::npos) {
            host = url_without_protocol;
            path = "/";
        } else {
            host = url_without_protocol.substr(0, path_pos);
            path = url_without_protocol.substr(path_pos);
        }

        // Create HTTP client
        bool use_ssl = url.find("https://") == 0;
        httplib::Client client(fmt::format("{}://{}", use_ssl ? "https" : "http", host));
        client.set_connection_timeout(5);
        client.set_read_timeout(5);
        client.set_write_timeout(5);

        // Prepare JSON payload
        sonic_json::Document d {};
        d.SetObject();
        auto& alloc = d.GetAllocator();
        AddMember(d, alloc, "text", text);

        std::string body = d.Dump();

        // Send POST request
        httplib::Headers headers {
            {"Content-Type", "application/json"}
        };

        SPDLOG_INFO("[SLACK_REQ] url={} body={}", url, body);

        if (auto res = client.Post(path, headers, body, "application/json")) {
            if (res->status == 200) {
                SPDLOG_INFO("[SLACK_RSP] status={} body={}", res->status, res->body);
                return true;
            } else {
                SPDLOG_ERROR("[SLACK_RSP] status={} reason={} body={}",
                           res->status, res->reason, res->body);
                return false;
            }
        } else {
            SPDLOG_ERROR("Slack HTTP POST failed: error code {}", static_cast<int>(res.error()));
            return false;
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in SendToWebhook: {}", e.what());
        return false;
    }
}

std::string SlackClient::GetWebhookUrl(const std::string& channel) const {
    if (const auto it = m_channels.find(channel);
        it != m_channels.end()) {
        return it->second;
    }
    return m_default_channel;
}

bool SlackClient::SendText(const std::string& channel, const std::string& subject, const std::string& content) {
    const std::string text = FormatMessage(subject, content);
    const std::string url = GetWebhookUrl(channel);
    if (url.empty()) {
        SPDLOG_ERROR("No webhook URL configured for channel: {}", channel);
        return false;
    }
    return SendToWebhook(url, text);
}

bool SlackClient::SendText(const std::vector<std::string>& channels,
                          const std::string& subject,
                          const std::string& content) {
    bool all_success = true;
    for (const auto& channel : channels) {
        if (!SendText(channel, subject, content)) {
            all_success = false;
        }
    }
    return all_success;
}

void SlackClient::SendTextAsync(const std::string& channel,
                               const std::string& subject,
                               const std::string& content) {
    std::thread([this, channel, subject, content]() {
        SendText(channel, subject, content);
    }).detach();
}

void SlackClient::SendTextAsync(const std::vector<std::string>& channels,
                               const std::string& subject,
                               const std::string& content) {
    std::thread([this, channels, subject, content]() {
        SendText(channels, subject, content);
    }).detach();
}
