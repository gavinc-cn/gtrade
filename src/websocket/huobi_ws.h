//
// Created by dell on 2025/2/15.
//

#pragma once

#include "pch.h"
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "websocket_base.h"
#include "i_exchange_data_dump.h"
#include "define.h"
#include "misc.h"
#include "str_utils.h"
#include "json_helper.h"

class HuobiWs: public WebSocketBase {
public:
    HuobiWs(IOPool<MyService>& pool, const std::string& uri):
    WebSocketBase(uri),
    m_service(pool.GetSharedThread())
    {

    }

    void subscribe();
    void on_message(websocketpp::connection_hdl, client::message_ptr msg);

private:
    std::shared_ptr<MyService> m_service;
};
