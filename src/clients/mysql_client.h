#pragma once

#include "pch.h"
#include "BaseClient.h"
#include "httplib.h"
#include "type_define.h"
#include "i_client.h"
#include "sonic_helper.h"
#include <mysql/mysql.h>

class MysqlClient: public BaseClient {
public:
    MysqlClient(const DBConfig& db_cfg);
    MysqlClient() = default;
    ~MysqlClient();
    bool Connect();
    bool Execute(const std::string& sql);
    bool QryKLine(TBufferPtr& buf, const KLineQryReq& req);
    bool QryDepth(TBufferPtr& buf, const DepthQryReq& req);
    bool QryHisEntrusts(TBufferPtr& buf, const HisEntrustsQryReq& req);
    MYSQL* GetConnection() { return m_mysql_conn; }
private:
    DBConfig m_account {};
    MYSQL* m_mysql_conn = nullptr;
};