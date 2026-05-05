//
// MessageServer - 消息服务器实现
//

#include <yaml-cpp/yaml.h>
#include "message_server.h"
#include "slack_client.h"
#include "type_define_dump.h"
#include "string_keys.h"
#include "msg_id_dump.h"


MessageServer::MessageServer(ServiceMap& pool, const GTradeConfig& gtrade_cfg):
m_gtrade_cfg(gtrade_cfg)
{
    SetThread(zrt::EnginePool::GetInstance().GetNamedThread(k_MessageServerThread));
}

bool MessageServer::Init() {
    SPDLOG_INFO("");
    SlackClient::GetInstance().Init(m_gtrade_cfg.slack_config_path);
    SPDLOG_INFO("SlackClient initialized from config: {}", m_gtrade_cfg.slack_config_path);

    InstallDefaultHandler([](int msg_id, const BufPtr buffer) {
        SPDLOG_ERROR("Unhandled msg_id={} buf_sz={}", GetEmName_MsgId(msg_id), buffer->GetSize());
    });

    ZRT_ADD_HANDLER(kNotifyMessage, MessageServer::OnSlackMessage);
    return true;
}

bool MessageServer::Start() {
    SPDLOG_INFO("");
    return true;
}

void MessageServer::OnSlackMessage(int msg_id, const BufPtr buffer) {
    const auto& [channel, subject, content, is_async] = buffer->Data<NotifyMessageReq>();

    SPDLOG_INFO("Received Slack message: channel={} subject={} content={} async={}",
                channel, subject, content, is_async);

    if (is_async) {
        // 异步发送
        SlackClient::GetInstance().SendTextAsync(channel, subject, content);
        SPDLOG_INFO("Slack message sent asynchronously to channel: {}", channel);
    } else {
        // 同步发送
        SlackClient::GetInstance().SendText(channel, subject, content);
        SPDLOG_INFO("Slack message sent successfully to channel: {}", channel);
    }
}
