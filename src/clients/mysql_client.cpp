#include <cryptopp/hmac.h>
#include <cryptopp/sha.h>
#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include "type_define.h"
#include "mysql_client.h"
#include "dict.h"
#include "zrtools/zrt_time-inl.h"
#include "zrtools/zrt_compare.h"
#include "sonic_helper.h"
#include "i_client.h"
#include "http_utils.h"
#include "dict_mapping.h"
#include "my_utc.h"


MysqlClient::MysqlClient(const DBConfig& db_cfg):
m_account(db_cfg)
{
    Connect();
}

MysqlClient::~MysqlClient() {
    if (m_mysql_conn) {
        mysql_close(m_mysql_conn);
        m_mysql_conn = nullptr;
    }
}

bool MysqlClient::Connect() {
    if (m_mysql_conn) return true;
    
    m_mysql_conn = mysql_init(nullptr);
    if (!m_mysql_conn) {
        SPDLOG_ERROR("mysql_init failed");
        return false;
    }
    
    // 设置连接超时
    constexpr unsigned int timeout = 10;
    mysql_options(m_mysql_conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(m_mysql_conn, MYSQL_OPT_READ_TIMEOUT, &timeout);
    mysql_options(m_mysql_conn, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
    
    // 设置字符集
    mysql_options(m_mysql_conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    
    // 连接数据库
    SPDLOG_INFO("connect {}@{}:{}:{}", m_account.user, m_account.host, m_account.port, m_account.db);
    if (!mysql_real_connect(m_mysql_conn,
        m_account.host.c_str(), // host
        m_account.user.c_str(), // user
        m_account.password.c_str(), // passwd
        m_account.db.c_str(), // passwd
        m_account.port, // port
        nullptr, 0)) {
        SPDLOG_ERROR("mysql_real_connect failed: {}", mysql_error(m_mysql_conn));
        mysql_close(m_mysql_conn);
        m_mysql_conn = nullptr;
        return false;
    }
    
    SPDLOG_INFO("MySQL connected successfully");
    return true;
}

bool MysqlClient::Execute(const std::string& sql) {
    SPDLOG_TRACE("sql={}", sql);

    // 确保数据库连接有效
    if (!Connect()) {
        SPDLOG_ERROR("MySQL connection failed");
        return false;
    }

    // 检查连接是否仍然有效，如果断开则重新连接
    if (mysql_ping(m_mysql_conn) != 0) {
        SPDLOG_WARN("MySQL connection lost, reconnecting...");
        mysql_close(m_mysql_conn);
        m_mysql_conn = nullptr;
        if (!Connect()) {
            SPDLOG_ERROR("MySQL reconnection failed");
            return false;
        }
    }

    if (mysql_query(m_mysql_conn, sql.c_str())) {
        SPDLOG_ERROR("mysql_query failed: {}", mysql_error(m_mysql_conn));
        return false;
    }

    return true;
}

bool MysqlClient::QryKLine(TBufferPtr& buf, const KLineQryReq& req) {
    if (!Connect()) {
        SPDLOG_ERROR("connect mysql failed");
        return false;
    }
    
    try {
        // 构建SQL查询语句
        const std::string sql = fmt::format(
            "SELECT market, instrument, ex_time, ex_time_h, open, high, low, close, volume "
            "FROM `kline_{}{}_{}_{}` "
            "WHERE market = '{}' AND instrument = '{}' AND ex_time >= {} AND ex_time <= {} "
            "ORDER BY ex_time",
            req.coefficient, req.scale, req.market, req.instrument,
            req.market, req.instrument, req.start_time, req.end_time
        );
        
        SPDLOG_DEBUG("Executing SQL: {}", sql);
        
        // 执行查询
        if (mysql_query(m_mysql_conn, sql.c_str())) {
            SPDLOG_ERROR("mysql_query failed: {}", mysql_error(m_mysql_conn));
            return false;
        }
        
        // 获取结果集
        MYSQL_RES* result = mysql_store_result(m_mysql_conn);
        if (!result) {
            SPDLOG_ERROR("mysql_store_result failed: {}", mysql_error(m_mysql_conn));
            return false;
        }
        
        std::map<int64_t, KLine> kline_map{};
        MYSQL_ROW row {};
        
        // 处理查询结果
        while ((row = mysql_fetch_row(result))) {
            KLine kline{};
            
            // 填充KLine数据
            zrt::fill_field(kline.market, row[0] ? row[0] : "");
            zrt::fill_field(kline.instrument, row[1] ? row[1] : "");
            zrt::fill_field(kline.ex_time, row[2] ? std::stoll(row[2]) : 0);
            zrt::fill_field(kline.datetime, row[3] ? row[3] : "");
            zrt::fill_field(kline.open, row[4] ? std::stod(row[4]) : 0.0);
            zrt::fill_field(kline.high, row[5] ? std::stod(row[5]) : 0.0);
            zrt::fill_field(kline.low, row[6] ? std::stod(row[6]) : 0.0);
            zrt::fill_field(kline.close, row[7] ? std::stod(row[7]) : 0.0);
            zrt::fill_field(kline.volume, row[8] ? std::stod(row[8]) : 0.0);
            zrt::fill_field(kline.coefficient, req.coefficient);
            zrt::fill_field(kline.scale, req.scale);
            
            kline_map[kline.ex_time] = std::move(kline);
        }
        
        mysql_free_result(result);
        
        // 构建响应数据
        KLineRange kline_range{};
        zrt::fill_field(kline_range.req_id, req.req_id);
        zrt::fill_field(kline_range.market, req.market);
        zrt::fill_field(kline_range.instrument, req.instrument);
        zrt::fill_field(kline_range.coefficient, req.coefficient);
        zrt::fill_field(kline_range.scale, req.scale);
        zrt::fill_field(kline_range.start_time, req.start_time);
        zrt::fill_field(kline_range.end_time, req.end_time);
        zrt::fill_field(kline_range.count, kline_map.size());
        
        buf->Append(kline_range);
        for (const auto& [ex_time, kline] : kline_map) {
            buf->Append(kline);
        }
        
        SPDLOG_INFO("got {} {}{} klines for {} in {}", 
                   kline_range.count, kline_range.coefficient, kline_range.scale, 
                   kline_range.instrument, kline_range.market);
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("process data failed: {}", e.what());
        return false;
    }
    
    return true;
}

bool MysqlClient::QryDepth(TBufferPtr& buf, const DepthQryReq& req) {
    if (!Connect()) {
        SPDLOG_ERROR("connect mysql failed");
        return false;
    }

    std::map<int64_t, Depth> depth_map {};
    try {
        // 构建SQL查询语句
        const std::string sql = fmt::format(
            "SELECT ex_time, local_time, ex_time_iso, market, instrument, bid1_px, bid1_vol, ask1_px, ask1_vol "
            "FROM `depth1_{}_{}` "
            "WHERE market = '{}' AND instrument = '{}' AND ex_time >= {} AND ex_time <= {} "
            "ORDER BY ex_time",
            req.market, req.instrument,
            req.market, req.instrument, req.start_time, req.end_time
        );
        
        SPDLOG_DEBUG("Executing SQL: {}", sql);
        
        // 执行查询
        if (mysql_query(m_mysql_conn, sql.c_str())) {
            SPDLOG_ERROR("mysql_query failed: {}", mysql_error(m_mysql_conn));
            return false;
        }
        
        // 获取结果集
        MYSQL_RES* result = mysql_store_result(m_mysql_conn);
        if (!result) {
            SPDLOG_ERROR("mysql_store_result failed: {}", mysql_error(m_mysql_conn));
            return false;
        }
        
        MYSQL_ROW row {};
        // 处理查询结果
        while ((row = mysql_fetch_row(result))) {
            Depth depth {};
            // 填充Depth数据
            zrt::fill_field(depth.ex_time, row[0] ? std::stoll(row[0]) : 0);
            zrt::fill_field(depth.local_time, row[1] ? std::stoll(row[1]) : 0);
            zrt::fill_field(depth.datetime, row[2] ? row[2] : "");
            zrt::fill_field(depth.market, row[3] ? row[3] : "");
            zrt::fill_field(depth.symbol, row[4] ? row[4] : "");
            
            // 从depth1表只能获取一档数据，设置bid和ask
            depth.bid_cnt = 1;
            depth.ask_cnt = 1;
            depth.bid_price[0] = row[5] ? std::stod(row[5]) : 0.0;
            depth.bid_amount[0] = row[6] ? std::stod(row[6]) : 0.0;
            depth.ask_price[0] = row[7] ? std::stod(row[7]) : 0.0;
            depth.ask_amount[0] = row[8] ? std::stod(row[8]) : 0.0;
            
            depth.seq_id = 0;
            depth.monotonic = 0;
            
            depth_map[depth.ex_time] = std::move(depth);
        }
        
        mysql_free_result(result);
    }
    catch (const std::exception& e) {
        SPDLOG_ERROR("process depth data failed: {}", e.what());
        return false;
    }
        
    // // 构建响应数据
    // DepthRange depth_range{};
    // zrt::fill_field(depth_range.req_id, req.req_id);
    // zrt::fill_field(depth_range.strat_id, req.strat_id);
    // zrt::fill_field(depth_range.market, req.market);
    // zrt::fill_field(depth_range.instrument, req.instrument);
    // zrt::fill_field(depth_range.level, req.level);
    // zrt::fill_field(depth_range.start_time, req.start_time);
    // zrt::fill_field(depth_range.end_time, req.end_time);
    // zrt::fill_field(depth_range.count, depth_map.size());
    //
    // buf->Append(depth_range);
    for (const auto& [ex_time, depth] : depth_map) {
        buf->Append(depth);
    }

    SPDLOG_INFO("got {} depth records for {} in {}", depth_map.size(), req.instrument, req.market);
    return true;
}

bool MysqlClient::QryHisEntrusts(TBufferPtr& buf, const HisEntrustsQryReq& req) {
    if (!Connect()) {
        SPDLOG_ERROR("connect mysql failed");
        return false;
    }

    try {
        // 构造查询条件
        std::string sql = "SELECT market, account_id, inst_type, inst_id, inst_id_code, policy_no, private_no, "
                         "bs_side, pos_side, oc_side, trade_mode, price_type, price, amount, "
                         "ent_time, expire_time, entno, ex_entno, status, filled_px, filled, remain, "
                         "confirm_time, filled_time, update_time, source, err_code, err_msg, "
                         "drawno, draw_amt, withdraw_time FROM `order` WHERE 1=1";

        // 添加可选过滤条件
        if (!zrt::is_empty(req.inst_type)) {
            sql += fmt::format(" AND inst_type='{}'", req.inst_type);
        }
        if (strlen(req.instrument) > 0) {
            sql += fmt::format(" AND inst_id='{}'", req.instrument);
        }
        if (req.price_type != '\0') {
            sql += fmt::format(" AND price_type='{}'", req.price_type);
        }
        if (req.status != '\0') {
            sql += fmt::format(" AND status='{}'", req.status);
        }
        if (req.start_time > 0) {
            sql += fmt::format(" AND ent_time >= {}", req.start_time);
        }
        if (req.end_time > 0) {
            sql += fmt::format(" AND end_time <= {}", req.end_time);
        }

        sql += " ORDER BY ent_time DESC";

        SPDLOG_DEBUG("Executing SQL: {}", sql);

        // 执行查询
        if (mysql_query(m_mysql_conn, sql.c_str())) {
            SPDLOG_ERROR("mysql_query failed: {}", mysql_error(m_mysql_conn));
            return false;
        }

        // 获取查询结果
        MYSQL_RES* result = mysql_store_result(m_mysql_conn);
        if (!result) {
            SPDLOG_ERROR("mysql_store_result failed: {}", mysql_error(m_mysql_conn));
            return false;
        }

        MYSQL_ROW row {};
        int count = 0;

        // 处理查询结果
        while ((row = mysql_fetch_row(result))) {
            Order entrust {};
            int col = 0;

            // 填充Entrust数据
            zrt::fill_field(entrust.market, row[col++] ? row[col - 1] : "");
            zrt::fill_field(entrust.account_id, row[col++] ? row[col - 1] : "");
            zrt::fill_field(entrust.inst_type, row[col++] ? row[col - 1][0] : '\0');
            zrt::fill_field(entrust.inst_id, row[col++] ? row[col - 1] : "");
            entrust.inst_id_code = row[col++] ? std::stoll(row[col - 1]) : 0;
            zrt::fill_field(entrust.policy_no, row[col++] ? row[col - 1] : "");
            zrt::fill_field(entrust.private_no, row[col++] ? row[col - 1] : "");
            zrt::fill_field(entrust.bs_side, row[col++] ? row[col - 1][0] : '\0');
            zrt::fill_field(entrust.pos_side, row[col++] ? row[col - 1][0] : '\0');
            zrt::fill_field(entrust.oc_side, row[col++] ? row[col - 1][0] : '\0');
            zrt::fill_field(entrust.trade_mode, row[col++] ? row[col - 1][0] : '\0');
            zrt::fill_field(entrust.price_type, row[col++] ? row[col - 1][0] : '\0');
            entrust.price = row[col++] ? std::stod(row[col - 1]) : 0.0;
            entrust.amount = row[col++] ? std::stod(row[col - 1]) : 0.0;
            entrust.ent_time = row[col++] ? std::stoll(row[col - 1]) : 0;
            entrust.expire_time = row[col++] ? std::stoll(row[col - 1]) : 0;
            entrust.entno = row[col++] ? std::stoll(row[col - 1]) : 0;
            entrust.ex_entno = row[col++] ? std::stoll(row[col - 1]) : 0;
            zrt::fill_field(entrust.status, row[col++] ? row[col - 1][0] : '\0');
            entrust.filled_px = row[col++] ? std::stod(row[col - 1]) : 0.0;
            entrust.filled = row[col++] ? std::stod(row[col - 1]) : 0.0;
            entrust.remain = row[col++] ? std::stod(row[col - 1]) : 0.0;
            entrust.confirm_time = row[col++] ? std::stoll(row[col - 1]) : 0;
            entrust.filled_time = row[col++] ? std::stoll(row[col - 1]) : 0;
            entrust.update_time = row[col++] ? std::stoll(row[col - 1]) : 0;
            zrt::fill_field(entrust.source, row[col++] ? row[col - 1][0] : '\0');
            entrust.err_code = row[col++] ? std::stoi(row[col - 1]) : 0;
            zrt::fill_field(entrust.err_msg, row[col++] ? row[col - 1] : "");
            entrust.drawno = row[col++] ? std::stoll(row[col - 1]) : 0;
            entrust.draw_amt = row[col++] ? std::stod(row[col - 1]) : 0.0;
            entrust.withdraw_time = row[col++] ? std::stoll(row[col - 1]) : 0;

            buf->Append(entrust);
            ++count;
        }

        mysql_free_result(result);
        SPDLOG_INFO("Loaded {} entrusts from database", count);

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in QryHisEntrusts: {}", e.what());
        return false;
    }

    return true;
}
