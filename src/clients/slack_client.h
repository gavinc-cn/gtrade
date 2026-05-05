//
// SlackClient - C++ implementation of Slack notification service
//

#pragma once

#include "pch.h"
#include "httplib.h"
#include <string>
#include <unordered_map>

struct SlackMessage {
    std::string channel;
    std::string subject;
    std::string content;
};

class SlackClient {
    ZRT_DECLARE_SINGLETON(SlackClient);
public:
    // Initialize from YAML configuration file
    void Init(const std::string& yaml_file_path);

    // Send text message synchronously
    bool SendText(const std::string& channel,
                 const std::string& subject = "",
                 const std::string& content = "");

    // Send text message to multiple channels synchronously
    bool SendText(const std::vector<std::string>& channels,
                 const std::string& subject = "",
                 const std::string& content = "");

    // Send text message asynchronously
    void SendTextAsync(const std::string& channel,
                      const std::string& subject = "",
                      const std::string& content = "");

    // Send text message to multiple channels asynchronously
    void SendTextAsync(const std::vector<std::string>& channels,
                      const std::string& subject = "",
                      const std::string& content = "");

private:
    // Format message text
    std::string FormatMessage(const std::string& subject, const std::string& content) const;
    // Send HTTP POST request to Slack webhook
    bool SendToWebhook(const std::string& url, const std::string& text);
    // Get webhook URL for channel
    std::string GetWebhookUrl(const std::string& channel) const;

    std::unordered_map<std::string, std::string> m_channels;
    std::string m_default_channel;
};
