//
// Created by dell on 2025/2/19.
//

#include <string>
#include "i_client_dump.h"
#include "strategy_engine.h"
#include "tbuffer.h"
#include "i_strategy_engine_dump.h"
#include "type_define_dump.h"
#include "mysql_client.h"
#include "mysql_gateway.h"
#include "db_structures_dump.h"
#include "logger_config.h"
#include "message_server.h"
#include "notify_msg_helper.h"
#include "sql_builder.h"
#include "sql_builder_traits.h"

/**
 * 对字符串进行 SQL 转义，防止 SQL 语法错误和注入攻击
 *
 * 为什么使用 mysql_real_escape_string 而非简单字符替换：
 *   1. 字符集感知：根据连接的 charset 进行转义，避免多字节字符被破坏
 *      例如 GBK 中 '縗'(0xbf5c) 包含 0x5c (反斜杠)，简单替换会破坏字符
 *   2. SQL 模式感知：NO_BACKSLASH_ESCAPES 模式下转义规则不同
 *   3. 安全性：由 MySQL 官方库保证正确性
 *
 * 转义规则:
 *   '  -> \'   单引号，防止截断 SQL 字符串
 *   "  -> \"   双引号
 *   \  -> \\   反斜杠，防止转义序列混乱
 *   \n -> \\n  换行符
 *   \r -> \\r  回车符
 *   \0 -> \\0  空字符
 *
 * @param str 需要转义的原始字符串
 * @return 转义后的字符串，可安全用于 SQL 拼接
 */
std::string MySqlGateway::EscapeString(const std::string& str) const {
    if (str.empty()) {
        return str;
    }

    // 使用 mysql_real_escape_string 进行转义
    MYSQL* conn = m_mysql_client->GetConnection();
    if (!conn) {
        // 如果连接不可用，使用简单的替换方法
        std::string escaped = str;
        size_t pos = 0;
        while ((pos = escaped.find('\'', pos)) != std::string::npos) {
            escaped.replace(pos, 1, "\\'");
            pos += 2;
        }
        return escaped;
    }

    // 分配足够的空间：最坏情况下每个字符都需要转义
    std::vector<char> buffer(str.size() * 2 + 1);
    const unsigned long escaped_len = mysql_real_escape_string(conn, buffer.data(), str.c_str(), str.size());
    return std::string(buffer.data(), escaped_len);
}

MySqlGateway::MySqlGateway(ServiceMap& pool, const GTradeConfig& gtrade_cfg):
m_gtrade_cfg(gtrade_cfg)
{
    SetThread(zrt::EnginePool::GetInstance().GetNamedThread(k_MySqlGatewayThread));

    // 检查共享内存是否有效
    if (!m_order_shm.IsValid()) {
        SPDLOG_ERROR("Failed to map entrust shared memory");
    } else if (!m_order_shm->IsValid()) {
        SPDLOG_ERROR("Entrust shared memory content invalid (bad magic)");
    } else {
        SPDLOG_INFO("Entrust shared memory mapped successfully");
    }

    if (!m_trade_shm.IsValid()) {
        SPDLOG_ERROR("Failed to map done shared memory");
    } else if (!m_trade_shm->IsValid()) {
        SPDLOG_ERROR("Done shared memory content invalid (bad magic)");
    } else {
        SPDLOG_INFO("Done shared memory mapped successfully");
    }

    if (!m_position_shm.IsValid()) {
        SPDLOG_ERROR("Failed to map position shared memory");
    } else if (!m_position_shm->IsValid()) {
        SPDLOG_ERROR("Position shared memory content invalid (bad magic)");
    } else {
        SPDLOG_INFO("Position shared memory mapped successfully");
    }

    if (!m_portfolio_position_shm.IsValid()) {
        SPDLOG_ERROR("Failed to map portfolio position shared memory");
    } else if (!m_portfolio_position_shm->IsValid()) {
        SPDLOG_ERROR("Portfolio position shared memory content invalid (bad magic)");
    } else {
        SPDLOG_INFO("Portfolio position shared memory mapped successfully");
    }

    if (!m_balance_shm.IsValid()) {
        SPDLOG_ERROR("Failed to map balance shared memory");
    } else if (!m_balance_shm->IsValid()) {
        SPDLOG_ERROR("Balance shared memory content invalid (bad magic)");
    } else {
        SPDLOG_INFO("Balance shared memory mapped successfully");
    }
}

bool MySqlGateway::Init() {
    SPDLOG_INFO("{}", __PRETTY_FUNCTION__ );

    m_strategy_engine = ServiceMap::GetInstance().at(k_StrategyEngine).get();
    m_message_server = ServiceMap::GetInstance().at(k_MessageServer).get();

    // 在 Init() 创建 MySQL 客户端：所有服务的 Init() 都先于任何 Start()（gtrade.cpp 启动序列），
    // 保证 StrategyEngine::Start() 中的同步读（标的范围启动恢复）不依赖 Start() 的遍历顺序
    m_mysql_client = std::make_unique<MysqlClient>(m_gtrade_cfg.db_config);

    InstallDefaultHandler([](int msg_id, const BufPtr buffer) {
        SPDLOG_ERROR("msg_id={} buf_sz={}", GetEmName_MsgId(msg_id), buffer->GetSize());
    });

    ZRT_ADD_HANDLER(kDbSetStrategyInfo, MySqlGateway::OnDbSetStrategyInfo);
    ZRT_ADD_HANDLER(kDbDelStrategyInfo, MySqlGateway::OnDbDelStrategyInfo);
    ZRT_ADD_HANDLER(kDbDelAllStrategyInfo, MySqlGateway::OnDbDelAllStrategyInfo);
    ZRT_ADD_HANDLER(kDbDelKLine, MySqlGateway::OnDbDelKLine);
    ZRT_ADD_HANDLER(kDbSetKLine, MySqlGateway::OnDbSetKLine);
    ZRT_ADD_HANDLER(kDbDelOrder, MySqlGateway::OnDbDelOrder);
    ZRT_ADD_HANDLER(kDbDelTrade, MySqlGateway::OnDbDelTrade);
    // ZRT_ADD_HANDLER(kDbSetDone, MySqlGateway::OnDbSetTrade);
    ZRT_ADD_HANDLER(kDbSetOrder, MySqlGateway::OnDbSetOrder);
    ZRT_ADD_HANDLER(kDbSetTrade, MySqlGateway::OnDbSetTrade);
    ZRT_ADD_HANDLER(kDbSetPosition, MySqlGateway::OnDbSetPosition);
    ZRT_ADD_HANDLER(kDbSetPortfolioPosition, MySqlGateway::OnDbSetPortfolioPosition);
    ZRT_ADD_HANDLER(kDbSetStrategyLog, MySqlGateway::OnDbSetStrategyLog);
    ZRT_ADD_SYNC_HANDLER(kDbQueryHisOrdersReq, MySqlGateway::OnDbQueryHisOrdersReq);
    ZRT_ADD_HANDLER(kDbSetInstrumentScope, MySqlGateway::OnDbSetInstrumentScope);
    ZRT_ADD_SYNC_HANDLER(kDbQueryInstrumentScopeReq, MySqlGateway::OnDbQueryInstrumentScopeReq);
    ZRT_ADD_SYNC_HANDLER(kDbQueryMaxIdsReq, MySqlGateway::OnDbQueryMaxIdsReq);

    return true;
}

bool MySqlGateway::Start() {
    SPDLOG_INFO("{}", __PRETTY_FUNCTION__ );
    // m_mysql_client 已在 Init() 创建（早于所有服务的 Start()）

    // 启动共享内存轮询定时器（仅实盘模式）
    if constexpr (GlobalConst::IsRealTrading) {
        m_poll_timer = std::make_unique<boost::asio::steady_timer>(*RefIoService());
        m_poll_timer->expires_after(std::chrono::milliseconds(POLL_INTERVAL_MS));
        m_poll_timer->async_wait([this](const boost::system::error_code& ec) {
            if (!ec) {
                PollSharedMemory();
            }
        });
        SPDLOG_INFO("Shared memory polling started (interval={}ms)", POLL_INTERVAL_MS);
    }

    return true;
}


void MySqlGateway::OnDbSetStrategyInfo(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const StrategyInfo*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    // 判断是完整更新还是部分更新
    // bool is_param_update = strlen(recv_data.strat_template) > 0;  // 有template说明是param更新
    // bool is_indicator_update = recv_data.status == -1;  // status为-1说明只更新indicator

    // 转义 JSON 字符串，防止 SQL 注入和语法错误
    const std::string escaped_indicator = EscapeString(recv_data.indicator);
    const std::string escaped_param = EscapeString(recv_data.param);
    const std::string escaped_strat_name = EscapeString(recv_data.strat_name);
    const std::string escaped_strat_template = EscapeString(recv_data.strat_template);

    std::string sql;

    // if (is_indicator_update) {
    //     // 只更新indicator字段
    //     sql = fmt::format(
    //         "INSERT INTO strat_info (strat_name, strat_template, status, param, indicator) "
    //         "VALUES ('{}', '', 0, '{{}}', '{}') "
    //         "ON DUPLICATE KEY UPDATE "
    //         "indicator='{}', update_time=CURRENT_TIMESTAMP",
    //         escaped_strat_name, escaped_indicator,
    //         escaped_indicator
    //     );
    // } else if (is_param_update) {
    //     // 更新param、template、status字段
    //     sql = fmt::format(
    //         "INSERT INTO strat_info (strat_name, strat_template, status, param, indicator) "
    //         "VALUES ('{}', '{}', {}, '{}', '{{}}') "
    //         "ON DUPLICATE KEY UPDATE "
    //         "strat_template='{}', status={}, param='{}', update_time=CURRENT_TIMESTAMP",
    //         escaped_strat_name, escaped_strat_template, recv_data.status, escaped_param,
    //         escaped_strat_template, recv_data.status, escaped_param
    //     );
    // } else {
        // 完整更新（向后兼容）
        sql = fmt::format(
            "INSERT INTO strat_info (strat_name, strat_template, status, param, indicator) "
            "VALUES ('{}', '{}', {}, '{}', '{}') "
            "ON DUPLICATE KEY UPDATE "
            "strat_template='{}', status={}, param='{}', indicator='{}', update_time=CURRENT_TIMESTAMP",
            escaped_strat_name, escaped_strat_template, recv_data.status,
            escaped_param, escaped_indicator,
            escaped_strat_template, recv_data.status,
            escaped_param, escaped_indicator
        );
    // }

    SPDLOG_DEBUG("SQL: {}", sql);
    m_mysql_client->Execute(sql);
}

void MySqlGateway::OnDbDelStrategyInfo(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const StrategyInfo*>(buffer->Data());
    SPDLOG_INFO("recv_data={}", zrt::to_str(recv_data));

    // 删除指定策略
    const std::string sql = fmt::format(
        "DELETE FROM strat_info WHERE strat_name='{}'",
        EscapeString(recv_data.strat_name)
    );

    SPDLOG_INFO("Executing delete SQL: {}", sql);
    if (m_mysql_client->Execute(sql)) {
        SPDLOG_INFO("Strategy '{}' deleted from database successfully", recv_data.strat_name);
    } else {
        SPDLOG_ERROR("Failed to delete strategy '{}' from database", recv_data.strat_name);
    }
}

void MySqlGateway::OnDbDelAllStrategyInfo(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const StrategyInfo*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    // 删除所有策略
    std::string sql = "DELETE FROM strat_info";
    m_mysql_client->Execute(sql);
}

// 插入K线数据 - 表名动态生成: kline_{coefficient}{scale}_{market}_{instrument}
void MySqlGateway::OnDbSetKLine(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const KLine*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    // 转义字符串字段
    const std::string escaped_datetime = EscapeString(recv_data.datetime);
    const std::string escaped_market = EscapeString(recv_data.market);
    const std::string escaped_instrument = EscapeString(recv_data.instrument);

    // 动态生成表名: kline_{coefficient}{scale}_{market}_{instrument}
    // 注意：表名使用反引号包裹，不需要转义单引号，但需要转义反引号（实际上market/instrument不应包含特殊字符）
    std::string table_name = fmt::format("`kline_{}{}_{}_{}`",
        recv_data.coefficient, recv_data.scale, recv_data.market, recv_data.instrument);

    // 构建INSERT ON DUPLICATE KEY UPDATE语句
    std::string sql = fmt::format(
        "INSERT INTO {} (`ex_time`, `local_time`, `datetime`, `market`, `instrument`, `coefficient`, `scale`, `open`, `high`, `low`, `close`, `volume`) "
        "VALUES ({}, {}, '{}', '{}', '{}', {}, '{}', {}, {}, {}, {}, {}) "
        "ON DUPLICATE KEY UPDATE "
        "local_time={}, "
        "datetime='{}', "
        "coefficient={}, "
        "scale='{}', "
        "open={}, "
        "high={}, "
        "low={}, "
        "close={}, "
        "volume={}, update_time=CURRENT_TIMESTAMP",
        table_name,
        recv_data.ex_time, recv_data.local_time, escaped_datetime, escaped_market, escaped_instrument, recv_data.coefficient, recv_data.scale, recv_data.open, recv_data.high, recv_data.low, recv_data.close, recv_data.volume,
        recv_data.local_time, escaped_datetime, recv_data.coefficient, recv_data.scale, recv_data.open, recv_data.high, recv_data.low, recv_data.close, recv_data.volume
    );

    m_mysql_client->Execute(sql);
}

// 删除K线数据 - 表名动态生成: kline_{coefficient}{scale}_{market}_{instrument}
void MySqlGateway::OnDbDelKLine(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const KLine*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    // 转义字符串字段
    const std::string escaped_market = EscapeString(recv_data.market);
    const std::string escaped_instrument = EscapeString(recv_data.instrument);

    // 动态生成表名: kline_{coefficient}{scale}_{market}_{instrument}
    std::string table_name = fmt::format("`kline_{}{}_{}_{}`",
        recv_data.coefficient, recv_data.scale, recv_data.market, recv_data.instrument);

    // 删除指定K线数据
    std::string sql = fmt::format(
        "DELETE FROM {} WHERE market='{}' AND instrument='{}' AND ex_time={}",
        table_name, escaped_market, escaped_instrument, recv_data.ex_time
    );

    m_mysql_client->Execute(sql);
}

// 写入委托数据（使用模板 SQL Builder）
void MySqlGateway::OnDbSetOrder(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const Order*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    // 使用模板 SQL Builder 自动生成 INSERT...ON DUPLICATE KEY UPDATE 语句
    // 字符串转义由 FieldTraits 自动处理，无需手动转义
    // 字段列表和值列表完全由 XML 定义生成（35个字段），避免手动维护错误
    std::string sql = gtrade::SqlBuilder<Order>::BuildInsertOrUpdateSql(recv_data);

    m_mysql_client->Execute(sql);
}

// 写入成交数据（使用模板 SQL Builder）
void MySqlGateway::OnDbSetTrade(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const Trade*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    // 使用模板 SQL Builder 自动生成 INSERT 语句
    // 字段列表和值列表完全由 XML 定义生成，无需手动维护
    std::string sql = gtrade::SqlBuilder<Trade>::BuildInsertSql(recv_data);

    m_mysql_client->Execute(sql);
}

// 删除委托数据（使用模板 SQL Builder）
void MySqlGateway::OnDbDelOrder(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const Order*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    // 使用模板 SQL Builder 自动生成 DELETE 语句（基于主键）
    std::string sql = gtrade::SqlBuilder<Order>::BuildDeleteSql(recv_data);

    m_mysql_client->Execute(sql);
}

// 删除成交数据（使用模板 SQL Builder）
void MySqlGateway::OnDbDelTrade(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const Trade*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    // 使用模板 SQL Builder 自动生成 DELETE 语句（基于主键）
    std::string sql = gtrade::SqlBuilder<Trade>::BuildDeleteSql(recv_data);

    m_mysql_client->Execute(sql);
}

// 写入持仓数据（仅用于非实盘场景，如回测、手动补录等）
// 实盘场景应在持仓更新源头直接写入共享内存（参考OrderManager）
void MySqlGateway::OnDbSetPosition(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const Position*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    // 直接写入数据库
    // 注意：实盘模式下，持仓应在更新源头（如OnPosPush）直接写入共享内存
    //      MySqlGateway只负责从共享内存读取并持久化
    std::string sql = gtrade::SqlBuilder<Position>::BuildInsertOrUpdateSql(recv_data, "position");
    m_mysql_client->Execute(sql);
}

// 写入组合持仓数据（仅用于非实盘场景，如回测、手动补录等）
// 实盘场景应在持仓更新源头直接写入共享内存（参考OrderManager）
void MySqlGateway::OnDbSetPortfolioPosition(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const Position*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    // 直接写入数据库
    // 注意：实盘模式下，组合持仓应在更新源头直接写入共享内存
    //      MySqlGateway只负责从共享内存读取并持久化
    std::string sql = gtrade::SqlBuilder<Position>::BuildInsertOrUpdateSql(recv_data, "portfolio_position");
    m_mysql_client->Execute(sql);
}

// 写入策略运行日志（仅追加，联合主键: strat_id + log_time + seq）
void MySqlGateway::OnDbSetStrategyLog(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const StrategyLog*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    const std::string escaped_strat_id = EscapeString(recv_data.strat_id);
    const std::string escaped_content = EscapeString(recv_data.content);
    const char log_level = recv_data.log_level.get();

    const std::string sql = fmt::format(
        "INSERT INTO strategy_log (strat_id, log_level, log_time, seq, content) "
        "VALUES ('{}', '{}', {}, {}, '{}')",
        escaped_strat_id,
        log_level != '\0' ? log_level : 'I',
        recv_data.log_time,
        recv_data.seq,
        escaped_content
    );

    SPDLOG_DEBUG("SQL: {}", sql);
    m_mysql_client->Execute(sql);
}

// 查询历史委托
BufPtr MySqlGateway::OnDbQueryHisOrdersReq(int msg_id, const BufPtr buffer) {
    const auto& recv_data = *reinterpret_cast<const HisEntrustsQryReq*>(buffer->Data());
    SPDLOG_INFO("{}", zrt::to_str(recv_data));

    BufPtr rsp_buf = std::make_shared<TBuffer>();

    // 确保连接可用：连接失败时返回空响应
    // 注意：绝不能把空连接句柄传给 mysql_query（libmysqlclient 解引用 NULL 会段错误）
    if (!m_mysql_client->Connect()) {
        SPDLOG_ERROR("MySQL connection unavailable, return empty response");
        return rsp_buf;
    }
    MYSQL* conn = m_mysql_client->GetConnection();

    try {
        // 构造查询条件
        std::string sql = "SELECT market, account_id, inst_type, inst_id, policy_no, private_no, "
                         "bs_side, pos_side, oc_side, trade_mode, price_type, price, amount, "
                         "ent_time, expire_time, entno, ex_entno, status, filled_px, filled, remain, "
                         "confirm_time, filled_time, update_time, source, err_code, err_msg, "
                         "drawno, draw_amt, withdraw_time FROM `order` WHERE 1=1";

        // 添加可选过滤条件（注意：单字符字段理论上不需要转义，但为安全起见统一处理）
        if (!zrt::is_empty(recv_data.inst_type)) {
            sql += fmt::format(" AND inst_type='{}'", EscapeString(std::string(1, recv_data.inst_type)));
        }
        if (strlen(recv_data.instrument) > 0) {
            sql += fmt::format(" AND inst_id='{}'", EscapeString(recv_data.instrument));
        }
        if (recv_data.price_type != '\0') {
            sql += fmt::format(" AND price_type='{}'", EscapeString(std::string(1, recv_data.price_type)));
        }
        if (recv_data.status != '\0') {
            sql += fmt::format(" AND status='{}'", EscapeString(std::string(1, recv_data.status)));
        }
        if (recv_data.start_time > 0) {
            sql += fmt::format(" AND ent_time >= {}", recv_data.start_time);
        }
        if (recv_data.end_time > 0) {
            sql += fmt::format(" AND ent_time <= {}", recv_data.end_time);
        }

        sql += " ORDER BY ent_time DESC";

        SPDLOG_DEBUG("Executing SQL: {}", sql);

        // 执行查询
        if (mysql_query(conn, sql.c_str())) {
            SPDLOG_ERROR("mysql_query failed: {}", mysql_error(conn));
            return rsp_buf;
        }

        // 获取查询结果
        MYSQL_RES* result = mysql_store_result(conn);
        if (!result) {
            SPDLOG_ERROR("mysql_store_result failed: {}", mysql_error(conn));
            return rsp_buf;
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

            rsp_buf->Append(entrust);
            ++count;
        }

        mysql_free_result(result);
        SPDLOG_INFO("Loaded {} entrusts from database", count);

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Exception in OnDbQueryHisEntrustsReq: {}", e.what());
    }

    return rsp_buf;
}


// 全量替换写：事务内 DELETE + 批量 INSERT（低频操作；失败记日志 + Slack 告警，不回滚内存状态）
void MySqlGateway::OnDbSetInstrumentScope(int msg_id, const BufPtr buffer) {
    // 保存属低频操作，失败必须可见：发 Slack 告警（不做节流，区别于高频订单写的 m_db_failure_alert）
    const auto notify_fail = [](const std::string& detail) {
        SendNotifyMsg(k_error, "标的范围落库失败", fmt::format("标的范围保存未生效（已跳过写入）：{}", detail));
    };
    if (m_mysql_client == nullptr) {
        SPDLOG_ERROR("instrument_scope write skipped: mysql gateway not started");
        notify_fail("MySQL 网关未初始化");
        return;
    }
    std::vector<InstrumentScopeItem> items {};
    buffer->ForEach<InstrumentScopeItem>([&items](const InstrumentScopeItem& item) { items.push_back(item); });
    // 连接存活预检：Execute 内部含 Connect + mysql_ping + 断线重连，
    // 避免裸连接被 wait_timeout 回收后事务写静默失败
    if (!m_mysql_client->Execute("SELECT 1")) {
        SPDLOG_ERROR("instrument_scope write skipped: mysql connection unavailable");
        notify_fail("数据库连接不可用");
        return;
    }
    MYSQL* conn = m_mysql_client->GetConnection();
    // 预检 SELECT 1 的结果集必须取走，否则下一条命令报 CR_COMMANDS_OUT_OF_SYNC(2014)
    if (MYSQL_RES* probe_result = mysql_store_result(conn)) {
        mysql_free_result(probe_result);
    }
    if (mysql_query(conn, "START TRANSACTION") || mysql_query(conn, "DELETE FROM instrument_scope")) {
        const std::string err = mysql_error(conn);
        SPDLOG_ERROR("instrument_scope delete failed: {}", err);
        mysql_query(conn, "ROLLBACK");
        notify_fail(fmt::format("全量清理失败（已回滚）：{}", err));
        return;
    }
    for (size_t i = 0; i < items.size(); ++i) {
        // inst_type 是标的唯一性的一部分：OKX 的币币杠杆复用现货的 instId，只有加列才能与表主键
        // (market, inst_id, inst_type) 对齐。空串（加列前的老行/未带类型的客户端）由引擎归一化后再落库。
        const std::string sql = fmt::format("INSERT INTO instrument_scope (market, inst_id, inst_type) "
                                            "VALUES ('{}', '{}', '{}')",
                                            EscapeString(std::string(items[i].market)),
                                            EscapeString(std::string(items[i].inst_id)),
                                            EscapeString(std::string(items[i].inst_type)));
        if (mysql_query(conn, sql.c_str())) {
            const std::string err = mysql_error(conn);
            SPDLOG_ERROR("instrument_scope insert failed: {}", err);
            mysql_query(conn, "ROLLBACK");
            notify_fail(fmt::format("写入 market={} inst_id={} inst_type={} 失败（已回滚）：{}",
                                    std::string(items[i].market), std::string(items[i].inst_id),
                                    std::string(items[i].inst_type), err));
            return;
        }
    }
    if (mysql_query(conn, "COMMIT")) {
        const std::string err = mysql_error(conn);
        SPDLOG_ERROR("instrument_scope commit failed: {}", err);
        notify_fail(fmt::format("提交失败（事务可能已中断）：{}", err));
        return;
    }
    SPDLOG_INFO("instrument_scope saved: {} rows", items.size());
}

// 同步读（启动加载）：返回逐条 Append 的 InstrumentScopeItem；失败记日志 + Slack 告警并返回空 buffer
BufPtr MySqlGateway::OnDbQueryInstrumentScopeReq(int msg_id, const BufPtr buffer) {
    // 启动读失败必须可见：发 Slack 告警（启动恢复不会失败重试，只会按空范围继续）
    const auto notify_fail = [](const std::string& detail) {
        SendNotifyMsg(k_error, "标的范围启动读失败", fmt::format("标的范围启动恢复将按空范围处理：{}", detail));
    };
    BufPtr rsp_buf = std::make_shared<TBuffer>();
    if (m_mysql_client == nullptr) {
        SPDLOG_ERROR("instrument_scope read skipped: mysql gateway not started");
        notify_fail("MySQL 网关未初始化");
        return rsp_buf;
    }
    if (!m_mysql_client->Connect()) {
        SPDLOG_ERROR("MySQL connection unavailable, return empty instrument_scope response");
        notify_fail("数据库连接不可用");
        return rsp_buf;
    }
    MYSQL* conn = m_mysql_client->GetConnection();
    if (mysql_query(conn, "SELECT market, inst_id, inst_type FROM instrument_scope")) {
        const std::string err = mysql_error(conn);
        SPDLOG_ERROR("mysql_query failed: {}", err);
        notify_fail(fmt::format("查询失败：{}", err));
        return rsp_buf;
    }
    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) {
        const std::string err = mysql_error(conn);
        SPDLOG_ERROR("mysql_store_result failed: {}", err);
        notify_fail(fmt::format("读取结果集失败：{}", err));
        return rsp_buf;
    }
    MYSQL_ROW row {};
    int count = 0;
    while ((row = mysql_fetch_row(result))) {
        InstrumentScopeItem item {};
        zrt::fill_field(item.market, std::string(row[0] ? row[0] : ""));
        zrt::fill_field(item.inst_id, std::string(row[1] ? row[1] : ""));
        // inst_type 可能为空（加列前写入的老行）：不在这里补，由引擎 ApplyInstrumentScope 统一归一化
        zrt::fill_field(item.inst_type, std::string(row[2] ? row[2] : ""));
        rsp_buf->Append(item);
        ++count;
    }
    mysql_free_result(result);
    SPDLOG_INFO("instrument_scope loaded: {} rows", count);
    return rsp_buf;
}

BufPtr MySqlGateway::OnDbQueryMaxIdsReq(int msg_id, const BufPtr buffer) {
    // 启动高水位：查 order / trade 的最大号，供引擎设置号段基数，
    // 保证跨重启（含"同一秒内重启"）ID 单调不重叠（方案 rev4 §8）。
    // 两张表主键即 entno / tdno，MAX() 走主键末行，代价可忽略。
    // 数据库不可用时返回空响应（调用方回退时间基数并记日志），不阻塞启动。
    BufPtr rsp_buf = std::make_shared<TBuffer>();
    if (m_mysql_client == nullptr || !m_mysql_client->Connect()) {
        SPDLOG_ERROR("max ids query skipped: MySQL connection unavailable");
        return rsp_buf;
    }
    MYSQL* conn = m_mysql_client->GetConnection();

    // 单条 SQL 取两个最大值；表空时 MAX() 为 NULL，用 IFNULL 归 0
    const char* sql =
        "SELECT IFNULL((SELECT MAX(entno) FROM `order`), 0), IFNULL((SELECT MAX(tdno) FROM trade), 0)";
    if (mysql_query(conn, sql)) {
        SPDLOG_ERROR("mysql_query failed: {}", mysql_error(conn));
        return rsp_buf;
    }
    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) {
        SPDLOG_ERROR("mysql_store_result failed: {}", mysql_error(conn));
        return rsp_buf;
    }
    if (MYSQL_ROW row = mysql_fetch_row(result)) {
        MaxIdsQryRsp rsp {};
        rsp.max_entno = row[0] ? std::atoll(row[0]) : 0;
        rsp.max_tdno  = row[1] ? std::atoll(row[1]) : 0;
        rsp_buf->Append(rsp);
        SPDLOG_INFO("max ids from db: max_entno={}, max_tdno={}", rsp.max_entno, rsp.max_tdno);
    }
    mysql_free_result(result);
    return rsp_buf;
}


// ============ 共享内存轮询持久化实现 ============

void MySqlGateway::PollSharedMemory() {
    // 批量读取委托
    // SPDLOG_INFO("write order to db");
    if (m_order_shm.IsValid() && m_order_shm->IsValid()) {
        size_t batch_count = 0;
        Order entrust {};

        while (batch_count < BATCH_SIZE && m_order_shm->Read(entrust)) {
            // 带重试的写入数据库
            if (WriteOrderWithRetry(entrust)) {
                batch_count++;
            }
        }

        // 批量确认已写入（直接操作共享内存）
        if (batch_count > 0) {
            uint64_t old_confirmed = m_order_shm->GetConfirmedSeq();
            m_order_shm->ConfirmBatch(batch_count);
            uint64_t new_confirmed = m_order_shm->GetConfirmedSeq();
            SPDLOG_INFO("Batch entrust persistence confirmed: {} records, seq {} -> {}, unconfirmed={}",
                       batch_count, old_confirmed, new_confirmed, m_order_shm->GetUnconfirmedCount());
        }
    }

    // 批量读取成交
    // SPDLOG_INFO("write trade to db");
    if (m_trade_shm.IsValid() && m_trade_shm->IsValid()) {
        size_t batch_count = 0;
        Trade done {};

        while (batch_count < BATCH_SIZE && m_trade_shm->Read(done)) {
            // 带重试的写入数据库
            if (WriteTradeWithRetry(done)) {
                batch_count++;
            }
        }

        // 批量确认已写入（直接操作共享内存）
        if (batch_count > 0) {
            uint64_t old_confirmed = m_trade_shm->GetConfirmedSeq();
            m_trade_shm->ConfirmBatch(batch_count);
            uint64_t new_confirmed = m_trade_shm->GetConfirmedSeq();
            SPDLOG_INFO("Batch done persistence confirmed: {} records, seq {} -> {}, unconfirmed={}",
                       batch_count, old_confirmed, new_confirmed, m_trade_shm->GetUnconfirmedCount());
        }
    }

    // 批量读取持仓
    // SPDLOG_INFO("write position to db");
    if (m_position_shm.IsValid() && m_position_shm->IsValid()) {
        size_t batch_count = 0;
        Position position {};

        while (batch_count < BATCH_SIZE && m_position_shm->Read(position)) {
            // 带重试的写入数据库
            if (WritePositionWithRetry(position, "position")) {
                batch_count++;
            }
        }

        // 批量确认已写入（直接操作共享内存）
        if (batch_count > 0) {
            uint64_t old_confirmed = m_position_shm->GetConfirmedSeq();
            m_position_shm->ConfirmBatch(batch_count);
            uint64_t new_confirmed = m_position_shm->GetConfirmedSeq();
            SPDLOG_INFO("Batch position persistence confirmed: {} records, seq {} -> {}, unconfirmed={}",
                       batch_count, old_confirmed, new_confirmed, m_position_shm->GetUnconfirmedCount());
        }
    }

    // 批量读取组合持仓
    // SPDLOG_INFO("write portfolio position to db");
    if (m_portfolio_position_shm.IsValid() && m_portfolio_position_shm->IsValid()) {
        size_t batch_count = 0;
        Position portfolio_position {};

        while (batch_count < BATCH_SIZE && m_portfolio_position_shm->Read(portfolio_position)) {
            // 带重试的写入数据库
            if (WritePortfolioPositionWithRetry(portfolio_position)) {
                batch_count++;
            }
        }

        // 批量确认已写入（直接操作共享内存）
        if (batch_count > 0) {
            uint64_t old_confirmed = m_portfolio_position_shm->GetConfirmedSeq();
            m_portfolio_position_shm->ConfirmBatch(batch_count);
            uint64_t new_confirmed = m_portfolio_position_shm->GetConfirmedSeq();
            SPDLOG_INFO("Batch portfolio position persistence confirmed: {} records, seq {} -> {}, unconfirmed={}",
                       batch_count, old_confirmed, new_confirmed, m_portfolio_position_shm->GetUnconfirmedCount());
        }
    }

    // 批量读取资金
    // SPDLOG_INFO("write balance to db");
    if (m_balance_shm.IsValid() && m_balance_shm->IsValid()) {
        size_t batch_count = 0;
        Balance balance {};

        while (batch_count < BATCH_SIZE && m_balance_shm->Read(balance)) {
            // 带重试的写入数据库
            if (WriteBalanceWithRetry(balance)) {
                batch_count++;
            }
        }

        // 批量确认已写入（直接操作共享内存）
        if (batch_count > 0) {
            uint64_t old_confirmed = m_balance_shm->GetConfirmedSeq();
            m_balance_shm->ConfirmBatch(batch_count);
            uint64_t new_confirmed = m_balance_shm->GetConfirmedSeq();
            SPDLOG_INFO("Batch balance persistence confirmed: {} records, seq {} -> {}, unconfirmed={}",
                       batch_count, old_confirmed, new_confirmed, m_balance_shm->GetUnconfirmedCount());
        }
    }

    // 重新设置定时器
    if (m_poll_timer) {
        m_poll_timer->expires_after(std::chrono::milliseconds(POLL_INTERVAL_MS));
        m_poll_timer->async_wait([this](const boost::system::error_code& ec) {
            if (!ec) {
                PollSharedMemory();
            }
        });
    }
}

bool MySqlGateway::WriteOrderWithRetry(const Order& order, int retry_count) {
    try {
        // 使用模板 SQL Builder 自动生成 REPLACE INTO 语句
        std::string sql = gtrade::SqlBuilder<Order>::BuildReplaceSql(order);

        m_mysql_client->Execute(sql);
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to write entrust entno={}, retry={}/{}, error: {}",
                    order.entno, retry_count, MAX_RETRY_COUNT, e.what());

        // 检查是否需要重试
        if (retry_count < MAX_RETRY_COUNT) {
            // 计算指数退避延迟
            int64_t delay_ms = INITIAL_RETRY_DELAY_MS * (1 << retry_count);  // 1s, 2s, 4s, 8s, 16s
            if (delay_ms > MAX_RETRY_DELAY_MS) {
                delay_ms = MAX_RETRY_DELAY_MS;
            }

            SPDLOG_WARN("Retrying entrust entno={} after {}ms (attempt {}/{})",
                       order.entno, delay_ms, retry_count + 1, MAX_RETRY_COUNT);

            // 等待后重试
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            return WriteOrderWithRetry(order, retry_count + 1);
        } else {
            // 重试次数用尽，发送告警
            auto now = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_db_failure_alert.last_alert_time).count();

            if (elapsed_ms > AlertThrottle::ALERT_INTERVAL_MS || m_db_failure_alert.alert_count == 0) {
                SendNotifyMsg(k_error, "CRITICAL: 数据库写入失败",
                              fmt::format("委托entno={} 写入失败，已重试{}次", order.entno, MAX_RETRY_COUNT));
                m_db_failure_alert.last_alert_time = now;
                m_db_failure_alert.alert_count++;
            }

            return false;
        }
    }
}

bool MySqlGateway::WriteTradeWithRetry(const Trade& trade, int retry_count) {
    try {
        // 使用模板 SQL Builder 自动生成 REPLACE INTO 语句
        std::string sql = gtrade::SqlBuilder<Trade>::BuildReplaceSql(trade);

        m_mysql_client->Execute(sql);
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to write done trade_no={}, retry={}/{}, error: {}",
                    trade.tdno, retry_count, MAX_RETRY_COUNT, e.what());

        // 检查是否需要重试
        if (retry_count < MAX_RETRY_COUNT) {
            // 计算指数退避延迟
            int64_t delay_ms = INITIAL_RETRY_DELAY_MS * (1 << retry_count);
            if (delay_ms > MAX_RETRY_DELAY_MS) {
                delay_ms = MAX_RETRY_DELAY_MS;
            }

            SPDLOG_WARN("Retrying done trade_no={} after {}ms (attempt {}/{})",
                       trade.tdno, delay_ms, retry_count + 1, MAX_RETRY_COUNT);

            // 等待后重试
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            return WriteTradeWithRetry(trade, retry_count + 1);
        } else {
            // 重试次数用尽，发送告警
            auto now = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_db_failure_alert.last_alert_time).count();

            if (elapsed_ms > AlertThrottle::ALERT_INTERVAL_MS || m_db_failure_alert.alert_count == 0) {
                SendNotifyMsg(k_error, "CRITICAL: 数据库写入失败",
                              fmt::format("成交trade_no={} 写入失败，已重试{}次", trade.tdno, MAX_RETRY_COUNT));
                m_db_failure_alert.last_alert_time = now;
                m_db_failure_alert.alert_count++;
            }

            return false;
        }
    }
}

bool MySqlGateway::WritePositionWithRetry(const Position& position, const std::string& table_name, int retry_count) {
    try {
        // 使用模板 SQL Builder 自动生成 REPLACE INTO 语句
        std::string sql = gtrade::SqlBuilder<Position>::BuildReplaceSql(position, table_name);

        m_mysql_client->Execute(sql);
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to write position instrument={}, table={}, retry={}/{}, error: {}",
                    position.instrument, table_name, retry_count, MAX_RETRY_COUNT, e.what());

        // 检查是否需要重试
        if (retry_count < MAX_RETRY_COUNT) {
            // 计算指数退避延迟
            int64_t delay_ms = INITIAL_RETRY_DELAY_MS * (1 << retry_count);
            if (delay_ms > MAX_RETRY_DELAY_MS) {
                delay_ms = MAX_RETRY_DELAY_MS;
            }

            SPDLOG_WARN("Retrying position instrument={}, table={} after {}ms (attempt {}/{})",
                       position.instrument, table_name, delay_ms, retry_count + 1, MAX_RETRY_COUNT);

            // 等待后重试
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            return WritePositionWithRetry(position, table_name, retry_count + 1);
        } else {
            // 重试次数用尽，发送告警
            auto now = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_db_failure_alert.last_alert_time).count();

            if (elapsed_ms > AlertThrottle::ALERT_INTERVAL_MS || m_db_failure_alert.alert_count == 0) {
                SendNotifyMsg(k_error, "CRITICAL: 数据库写入失败",
                              fmt::format("持仓instrument={}, table={} 写入失败，已重试{}次",
                                        position.instrument, table_name, MAX_RETRY_COUNT));
                m_db_failure_alert.last_alert_time = now;
                m_db_failure_alert.alert_count++;
            }

            return false;
        }
    }
}

bool MySqlGateway::WritePortfolioPositionWithRetry(const Position& position, int retry_count) {
    return WritePositionWithRetry(position, "portfolio_position", retry_count);
}

bool MySqlGateway::WriteBalanceWithRetry(const Balance& balance, int retry_count) {
    try {
        // 使用模板 SQL Builder 自动生成 REPLACE INTO 语句
        std::string sql = gtrade::SqlBuilder<Balance>::BuildReplaceSql(balance);

        if (sql.empty()) {
            SPDLOG_ERROR("WriteBalanceWithRetry: generated empty SQL for account={}, currency={}",
                        balance.account_id, balance.currency);
            return false;
        }

        m_mysql_client->Execute(sql);
        return true;
    }
    catch (const std::exception& e) {
        if (retry_count < MAX_RETRY_COUNT) {
            int64_t delay_ms = INITIAL_RETRY_DELAY_MS * (1LL << retry_count);
            if (delay_ms > MAX_RETRY_DELAY_MS) {
                delay_ms = MAX_RETRY_DELAY_MS;
            }
            SPDLOG_WARN("WriteBalanceWithRetry failed (attempt {}/{}): account={}, currency={}, err={}, retry in {}ms",
                       retry_count + 1, MAX_RETRY_COUNT,
                       balance.account_id, balance.currency, e.what(), delay_ms);

            // 等待后重试
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            return WriteBalanceWithRetry(balance, retry_count + 1);
        } else {
            // 重试次数用尽，发送告警
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_db_failure_alert.last_alert_time).count();

            if (elapsed > m_db_failure_alert.ALERT_INTERVAL_MS || m_db_failure_alert.alert_count == 0) {
                SendNotifyMsg(k_error, "资金写入数据库失败",
                              fmt::format("账户: {}, 币种: {}, 重试{}次后失败: {}",
                                        balance.account_id, balance.currency,
                                        MAX_RETRY_COUNT, e.what()));
                m_db_failure_alert.last_alert_time = now;
                m_db_failure_alert.alert_count++;
            }

            SPDLOG_ERROR("WriteBalanceWithRetry exhausted all retries: account={}, currency={}, err={}",
                        balance.account_id, balance.currency, e.what());
            return false;
        }
    }
}
