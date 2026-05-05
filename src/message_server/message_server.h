//
// MessageServer - 消息服务器，处理 Slack 消息发送
//

#pragma once

#include "pch.h"
#include "type_define.h"
#include "i_client.h"
#include "define.h"
#include "service_map.h"

class MessageServer: public MyHandler {
public:
    explicit MessageServer(ServiceMap& pool, const GTradeConfig& gtrade_cfg);
    ~MessageServer() override = default;

    bool Init() override;
    bool Start() override;

    // 处理 Slack 消息请求
    void OnSlackMessage(int msg_id, const BufPtr buffer);

private:
    GTradeConfig m_gtrade_cfg {};
};
