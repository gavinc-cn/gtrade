#include "pch.h"
#include "type_define.h"
#include "i_client.h"
#include "tbuffer.h"
#include "zrtools/zrt_fill.h"
#include <string>

#include "define.h"
#include "logger_config.h"
#include "service_map.h"
#include "string_keys.h"
#include "notify_msg_helper.h"


void SendNotifyMsg(const std::string& channel, const std::string& subject, const std::string& message) {
    if constexpr (GlobalConst::IsRealTrading) {
        NotifyMessageReq slack_req {};
        zrt::fill_field(slack_req.channel, channel);
        zrt::fill_field(slack_req.subject, subject);
        zrt::fill_field(slack_req.content, message);
        zrt::fill_field(slack_req.is_async, false);
        ServiceMap::GetInstance().at(k_MessageServer)->PostMsg(kNotifyMessage, std::make_shared<TBuffer>(slack_req));
    }
    else {
        SPDLOG_INFO(LOG_PREFIX_SLACK "[{}|{}] {}", "error", subject, message);
    }
}
