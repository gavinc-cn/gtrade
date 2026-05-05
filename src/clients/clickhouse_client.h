
#pragma once

#include "pch.h"
#include "BaseClient.h"
#include "httplib.h"
#include "type_define.h"
#include "i_client.h"
#include "sonic_helper.h"
#include "clickhouse/client.h"

class ClickhouseClient: public BaseClient {
public:
    ClickhouseClient(const Account& account);
    bool Connect();
    bool QryKLine(TBufferPtr& buf, const KLineQryReq& req);
private:
    std::string m_base_url = "https://www.okx.com";
    Account m_account {};
    std::unique_ptr<clickhouse::Client> m_client {};
};


