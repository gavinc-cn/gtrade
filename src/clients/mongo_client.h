#pragma once

#include "pch.h"
#include "BaseClient.h"
#include "httplib.h"
#include "type_define.h"
#include "i_client.h"
#include "sonic_helper.h"
#include <mongocxx/client.hpp>
#include <mongocxx/database.hpp>
#include <mongocxx/collection.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

class MongoClient: public BaseClient {
public:
    MongoClient(const Account& account);
    bool Connect();
    bool QryKLine(TBufferPtr& buf, const KLineQryReq& req);
private:
    std::string m_base_url = "https://www.okx.com";
    Account m_account {};
    std::unique_ptr<mongocxx::client> m_client {};
    mongocxx::database m_database;
};