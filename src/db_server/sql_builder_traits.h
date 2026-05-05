//
// SQL Builder Field Traits - 自动生成
// 警告: 此文件由脚本自动生成，请勿手动修改！
//

#pragma once

#include "sql_builder.h"
#include "db_structures.h"
#include <string>
#include <cmath>
#include <spdlog/fmt/fmt.h>

namespace gtrade {

// 简单字符串转义函数（用于 FieldTraits）
inline std::string SimpleEscapeString(const std::string& str) {
    std::string escaped = str;
    size_t pos = 0;
    while ((pos = escaped.find('\'', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "\\'");
        pos += 2;
    }
    return escaped;
}

// 安全的 double 转 SQL 字符串（处理 NaN 和 Inf）
inline std::string DoubleToSqlString(double value) {
    if (std::isnan(value) || std::isinf(value)) {
        return "NULL";  // NaN 和 Inf 转换为 SQL 的 NULL
    }
    return std::to_string(value);
}

// FieldTraits 特化 - 资金表
template<>
struct FieldTraits<Balance> {
    static constexpr std::string_view kTableName = "balance";

    // 字段元数据 (9个字段)
    static constexpr std::array<FieldMetadata, 9> kFields = {{
        {"ex_time", SqlFieldType::kInt64, false},  // 交易所时间(纳秒)
        {"local_time", SqlFieldType::kInt64, false},  // 本地时间(纳秒)
        {"datetime", SqlFieldType::kCharArray, true},  // 格式化时间
        {"market", SqlFieldType::kCharArray, true},  // 市场
        {"account_id", SqlFieldType::kCharArray, true},  // 账户ID
        {"currency", SqlFieldType::kString, false},  // 币种
        {"available", SqlFieldType::kDouble, false},  // 可用余额
        {"frozen", SqlFieldType::kDouble, false},  // 冻结余额
        {"total", SqlFieldType::kDouble, false},  // 总余额
    }};

    // 主键 (3个)
    static constexpr std::array<std::string_view, 3> kPrimaryKeys = {"market", "account_id", "currency"};

    // 获取字段值
    static std::string GetFieldValue(const Balance& data, size_t field_index) {
        switch (field_index) {
            case 0:  // ex_time (int64_t)
                return std::to_string(data.ex_time);
            case 1:  // local_time (int64_t)
                return std::to_string(data.local_time);
            case 2:  // datetime (DateTimeCs)
                return fmt::format("'{}'", SimpleEscapeString(data.datetime));
            case 3:  // market (MarketCs)
                return fmt::format("'{}'", SimpleEscapeString(data.market));
            case 4:  // account_id (AccountIdCs)
                return fmt::format("'{}'", SimpleEscapeString(data.account_id));
            case 5:  // currency (CurrencyCs)
                return fmt::format("'{}'", data.currency);
            case 6:  // available (double)
                return DoubleToSqlString(data.available);
            case 7:  // frozen (double)
                return DoubleToSqlString(data.frozen);
            case 8:  // total (double)
                return DoubleToSqlString(data.total);
            default:
                return "NULL";
        }
    }
};

// FieldTraits 特化 - K线数据表
template<>
struct FieldTraits<KLine> {
    static constexpr std::string_view kTableName = "kline";

    // 字段元数据 (12个字段)
    static constexpr std::array<FieldMetadata, 12> kFields = {{
        {"ex_time", SqlFieldType::kInt64, false},  // 交易所时间戳(纳秒)
        {"local_time", SqlFieldType::kInt64, false},  // 本地接收时间戳(纳秒)
        {"datetime", SqlFieldType::kCharArray, true},  // 格式化时间字符串
        {"market", SqlFieldType::kCharArray, true},  // 市场
        {"instrument", SqlFieldType::kCharArray, true},  // 标的
        {"coefficient", SqlFieldType::kInt, false},  // 时间周期系数
        {"scale", SqlFieldType::kChar, true},  // 时间周期单位(d/H/M/S)
        {"open", SqlFieldType::kDouble, false},  // 开盘价
        {"high", SqlFieldType::kDouble, false},  // 最高价
        {"low", SqlFieldType::kDouble, false},  // 最低价
        {"close", SqlFieldType::kDouble, false},  // 收盘价
        {"volume", SqlFieldType::kDouble, false},  // 成交量
    }};

    // 主键 (3个)
    static constexpr std::array<std::string_view, 3> kPrimaryKeys = {"market", "instrument", "ex_time"};

    // 获取字段值
    static std::string GetFieldValue(const KLine& data, size_t field_index) {
        switch (field_index) {
            case 0:  // ex_time (int64_t)
                return std::to_string(data.ex_time);
            case 1:  // local_time (int64_t)
                return std::to_string(data.local_time);
            case 2:  // datetime (DateTimeCs)
                return fmt::format("'{}'", SimpleEscapeString(data.datetime));
            case 3:  // market (MarketCs)
                return fmt::format("'{}'", SimpleEscapeString(data.market));
            case 4:  // instrument (InstrumentCs)
                return fmt::format("'{}'", SimpleEscapeString(data.instrument));
            case 5:  // coefficient (int)
                return std::to_string(data.coefficient);
            case 6:  // scale (CharCs)
                return fmt::format("'{}'", data.scale);
            case 7:  // open (double)
                return DoubleToSqlString(data.open);
            case 8:  // high (double)
                return DoubleToSqlString(data.high);
            case 9:  // low (double)
                return DoubleToSqlString(data.low);
            case 10:  // close (double)
                return DoubleToSqlString(data.close);
            case 11:  // volume (double)
                return DoubleToSqlString(data.volume);
            default:
                return "NULL";
        }
    }
};

// FieldTraits 特化 - 委托表
template<>
struct FieldTraits<Order> {
    static constexpr std::string_view kTableName = "order";

    // 字段元数据 (37个字段)
    static constexpr std::array<FieldMetadata, 37> kFields = {{
        {"entno", SqlFieldType::kInt64, false},  // 委托号
        {"status", SqlFieldType::kChar, true},  // 委托状态
        {"fmt_time", SqlFieldType::kInt64, false},  // 格式化时间
        {"market", SqlFieldType::kCharArray, true},  // 市场
        {"account_id", SqlFieldType::kCharArray, true},  // 账户ID
        {"portfolio", SqlFieldType::kCharArray, true},  // 组合
        {"inst_type", SqlFieldType::kChar, true},  // 标的类型
        {"inst_id", SqlFieldType::kCharArray, true},  // 标的名称
        {"inst_id_code", SqlFieldType::kInt64, false},  // 标的唯一标识代码
        {"policy_no", SqlFieldType::kCharArray, true},  // 策略编号
        {"private_no", SqlFieldType::kCharArray, true},  // 私有号
        {"bs_side", SqlFieldType::kChar, true},  // 买卖方向(B/S)
        {"pos_side", SqlFieldType::kChar, true},  // 持仓方向
        {"oc_side", SqlFieldType::kChar, true},  // 开平方向
        {"trade_mode", SqlFieldType::kChar, true},  // 交易模式
        {"price_type", SqlFieldType::kChar, true},  // 价格类型
        {"price", SqlFieldType::kDouble, false},  // 价格
        {"amount", SqlFieldType::kDouble, false},  // 数量
        {"ent_time", SqlFieldType::kInt64, false},  // 委托时间(纳秒)
        {"expire_time", SqlFieldType::kInt64, false},  // 委托超时时间(纳秒)
        {"ex_entno", SqlFieldType::kInt64, false},  // 交易所委托号
        {"filled_px", SqlFieldType::kDouble, false},  // 成交均价
        {"filled", SqlFieldType::kDouble, false},  // 成交数量
        {"remain", SqlFieldType::kDouble, false},  // 剩余数量
        {"confirm_time", SqlFieldType::kInt64, false},  // 委托确认时间(纳秒)
        {"filled_time", SqlFieldType::kInt64, false},  // 成交时间(纳秒)
        {"update_time", SqlFieldType::kInt64, false},  // 更新时间(纳秒)
        {"source", SqlFieldType::kChar, true},  // 委托来源
        {"err_code", SqlFieldType::kInt, false},  // 错误码
        {"err_msg", SqlFieldType::kCharArray, true},  // 错误消息
        {"drawno", SqlFieldType::kInt64, false},  // 撤单号
        {"draw_amt", SqlFieldType::kDouble, false},  // 撤单数量
        {"withdraw_time", SqlFieldType::kInt64, false},  // 撤单时间(纳秒)
        {"status_id", SqlFieldType::kInt, false},  // 订单状态id
        {"status_gid", SqlFieldType::kInt64, false},  // 订单状态全局id
        {"trd_px", SqlFieldType::kDouble, false},  // 最近一笔成交价
        {"trd_qty", SqlFieldType::kDouble, false},  // 最近一笔成交量
    }};

    // 主键 (1个)
    static constexpr std::array<std::string_view, 1> kPrimaryKeys = {"entno"};

    // 获取字段值
    static std::string GetFieldValue(const Order& data, size_t field_index) {
        switch (field_index) {
            case 0:  // entno (int64_t)
                return std::to_string(data.entno);
            case 1:  // status (CharCs)
                return fmt::format("'{}'", data.status);
            case 2:  // fmt_time (int64_t)
                return std::to_string(data.fmt_time);
            case 3:  // market (MarketCs)
                return fmt::format("'{}'", SimpleEscapeString(data.market));
            case 4:  // account_id (AccountIdCs)
                return fmt::format("'{}'", SimpleEscapeString(data.account_id));
            case 5:  // portfolio (PortfolioCs)
                return fmt::format("'{}'", SimpleEscapeString(data.portfolio));
            case 6:  // inst_type (CharCs)
                return fmt::format("'{}'", data.inst_type);
            case 7:  // inst_id (InstrumentCs)
                return fmt::format("'{}'", SimpleEscapeString(data.inst_id));
            case 8:  // inst_id_code (int64_t)
                return std::to_string(data.inst_id_code);
            case 9:  // policy_no (PolicyNoCs)
                return fmt::format("'{}'", SimpleEscapeString(data.policy_no));
            case 10:  // private_no (PrivateNoCs)
                return fmt::format("'{}'", SimpleEscapeString(data.private_no));
            case 11:  // bs_side (CharCs)
                return fmt::format("'{}'", data.bs_side);
            case 12:  // pos_side (CharCs)
                return fmt::format("'{}'", data.pos_side);
            case 13:  // oc_side (CharCs)
                return fmt::format("'{}'", data.oc_side);
            case 14:  // trade_mode (CharCs)
                return fmt::format("'{}'", data.trade_mode);
            case 15:  // price_type (CharCs)
                return fmt::format("'{}'", data.price_type);
            case 16:  // price (double)
                return DoubleToSqlString(data.price);
            case 17:  // amount (double)
                return DoubleToSqlString(data.amount);
            case 18:  // ent_time (int64_t)
                return std::to_string(data.ent_time);
            case 19:  // expire_time (int64_t)
                return std::to_string(data.expire_time);
            case 20:  // ex_entno (int64_t)
                return std::to_string(data.ex_entno);
            case 21:  // filled_px (double)
                return DoubleToSqlString(data.filled_px);
            case 22:  // filled (double)
                return DoubleToSqlString(data.filled);
            case 23:  // remain (double)
                return DoubleToSqlString(data.remain);
            case 24:  // confirm_time (int64_t)
                return std::to_string(data.confirm_time);
            case 25:  // filled_time (int64_t)
                return std::to_string(data.filled_time);
            case 26:  // update_time (int64_t)
                return std::to_string(data.update_time);
            case 27:  // source (CharCs)
                return fmt::format("'{}'", data.source);
            case 28:  // err_code (int)
                return std::to_string(data.err_code);
            case 29:  // err_msg (ErrMsgCs)
                return fmt::format("'{}'", SimpleEscapeString(data.err_msg));
            case 30:  // drawno (int64_t)
                return std::to_string(data.drawno);
            case 31:  // draw_amt (double)
                return DoubleToSqlString(data.draw_amt);
            case 32:  // withdraw_time (int64_t)
                return std::to_string(data.withdraw_time);
            case 33:  // status_id (int)
                return std::to_string(data.status_id);
            case 34:  // status_gid (int64_t)
                return std::to_string(data.status_gid);
            case 35:  // trd_px (double)
                return DoubleToSqlString(data.trd_px);
            case 36:  // trd_qty (double)
                return DoubleToSqlString(data.trd_qty);
            default:
                return "NULL";
        }
    }
};

// FieldTraits 特化 - 持仓
template<>
struct FieldTraits<Position> {
    static constexpr std::string_view kTableName = "position";

    // 字段元数据 (19个字段)
    static constexpr std::array<FieldMetadata, 19> kFields = {{
        {"market", SqlFieldType::kCharArray, true},  // 市场
        {"account_id", SqlFieldType::kCharArray, true},  // 账户ID
        {"inst_type", SqlFieldType::kChar, true},  // 标的类型
        {"instrument", SqlFieldType::kCharArray, true},  // 标的名称
        {"pos_side", SqlFieldType::kChar, true},  // 持仓方向[PosSide]
        {"portfolio", SqlFieldType::kCharArray, true},  // 组合
        {"margin_mode", SqlFieldType::kChar, true},  // 保证金模式
        {"avg_px", SqlFieldType::kDouble, false},  // 成交均价
        {"available", SqlFieldType::kDouble, false},  // 可用数量
        {"ex_time", SqlFieldType::kInt64, false},  // 交易所时间
        {"local_time", SqlFieldType::kInt64, false},  // 本地时间
        {"datetime", SqlFieldType::kCharArray, true},  // 格式化时间
        {"upl", SqlFieldType::kDouble, false},  // 未实现盈亏：交易所推送(账户持仓) / 本地计算(策略持仓)
        {"upl_ratio", SqlFieldType::kDouble, false},  // 未实现盈亏比率 - 来自交易所
        {"notional_usd", SqlFieldType::kDouble, false},  // 名义价值(USD) - 来自交易所
        {"total_cost", SqlFieldType::kDouble, false},  // 总成本（累计开仓价值 = Σ(数量 × 开仓价格)）
        {"realized_pnl", SqlFieldType::kDouble, false},  // 已实现盈亏（平仓产生的盈亏）
        {"fee_paid", SqlFieldType::kDouble, false},  // 已支付手续费
        {"pos_source", SqlFieldType::kChar, true},  // 持仓来源[PosSource]
    }};

    // 主键 (5个)
    static constexpr std::array<std::string_view, 5> kPrimaryKeys = {"market", "account_id", "inst_type", "instrument", "pos_side"};

    // 获取字段值
    static std::string GetFieldValue(const Position& data, size_t field_index) {
        switch (field_index) {
            case 0:  // market (MarketCs)
                return fmt::format("'{}'", SimpleEscapeString(data.market));
            case 1:  // account_id (AccountIdCs)
                return fmt::format("'{}'", SimpleEscapeString(data.account_id));
            case 2:  // inst_type (CharCs)
                return fmt::format("'{}'", data.inst_type);
            case 3:  // instrument (InstrumentCs)
                return fmt::format("'{}'", SimpleEscapeString(data.instrument));
            case 4:  // pos_side (CharCs)
                return fmt::format("'{}'", data.pos_side);
            case 5:  // portfolio (PortfolioCs)
                return fmt::format("'{}'", SimpleEscapeString(data.portfolio));
            case 6:  // margin_mode (CharCs)
                return fmt::format("'{}'", data.margin_mode);
            case 7:  // avg_px (double)
                return DoubleToSqlString(data.avg_px);
            case 8:  // available (double)
                return DoubleToSqlString(data.available);
            case 9:  // ex_time (int64_t)
                return std::to_string(data.ex_time);
            case 10:  // local_time (int64_t)
                return std::to_string(data.local_time);
            case 11:  // datetime (DateTimeCs)
                return fmt::format("'{}'", SimpleEscapeString(data.datetime));
            case 12:  // upl (double)
                return DoubleToSqlString(data.upl);
            case 13:  // upl_ratio (double)
                return DoubleToSqlString(data.upl_ratio);
            case 14:  // notional_usd (double)
                return DoubleToSqlString(data.notional_usd);
            case 15:  // total_cost (double)
                return DoubleToSqlString(data.total_cost);
            case 16:  // realized_pnl (double)
                return DoubleToSqlString(data.realized_pnl);
            case 17:  // fee_paid (double)
                return DoubleToSqlString(data.fee_paid);
            case 18:  // pos_source (CharCs)
                return fmt::format("'{}'", data.pos_source);
            default:
                return "NULL";
        }
    }
};

// FieldTraits 特化 - 策略运行日志表
template<>
struct FieldTraits<StrategyLog> {
    static constexpr std::string_view kTableName = "strategy_log";

    // 字段元数据 (5个字段)
    static constexpr std::array<FieldMetadata, 5> kFields = {{
        {"strat_id", SqlFieldType::kCharArray, true},  // 策略编号
        {"log_level", SqlFieldType::kChar, true},  // 日志等级: I/W/E
        {"log_time", SqlFieldType::kInt64, false},  // 日志时间(纳秒)
        {"seq", SqlFieldType::kUInt16, false},  // 同纳秒内序号，防止主键碰撞
        {"content", SqlFieldType::kString, false},  // 日志内容
    }};

    // 主键 (3个)
    static constexpr std::array<std::string_view, 3> kPrimaryKeys = {"strat_id", "log_time", "seq"};

    // 获取字段值
    static std::string GetFieldValue(const StrategyLog& data, size_t field_index) {
        switch (field_index) {
            case 0:  // strat_id (PolicyNoCs)
                return fmt::format("'{}'", SimpleEscapeString(data.strat_id));
            case 1:  // log_level (CharCs)
                return fmt::format("'{}'", data.log_level);
            case 2:  // log_time (int64_t)
                return std::to_string(data.log_time);
            case 3:  // seq (uint16_t)
                return std::to_string(data.seq);
            case 4:  // content (LogContentCs)
                return fmt::format("'{}'", data.content);
            default:
                return "NULL";
        }
    }
};

// FieldTraits 特化 - 成交表
template<>
struct FieldTraits<Trade> {
    static constexpr std::string_view kTableName = "trade";

    // 字段元数据 (17个字段)
    static constexpr std::array<FieldMetadata, 17> kFields = {{
        {"tdno", SqlFieldType::kInt64, false},  // 成交号
        {"market", SqlFieldType::kCharArray, true},  // 市场
        {"account_id", SqlFieldType::kCharArray, true},  // 账户ID
        {"portfolio", SqlFieldType::kCharArray, true},  // 组合
        {"instrument", SqlFieldType::kCharArray, true},  // 标的名称
        {"strat_id", SqlFieldType::kCharArray, true},  // 策略编号
        {"private_no", SqlFieldType::kCharArray, true},  // 私有号
        {"ordno", SqlFieldType::kInt64, false},  // 委托号
        {"td_side", SqlFieldType::kChar, true},  // 买卖方向(B/S)
        {"pos_side", SqlFieldType::kChar, true},  // 持仓方向
        {"px_type", SqlFieldType::kChar, true},  // 价格类型
        {"td_px", SqlFieldType::kDouble, false},  // 成交价格
        {"td_qty", SqlFieldType::kDouble, false},  // 成交数量
        {"td_val", SqlFieldType::kDouble, false},  // 成交金额
        {"filled_time", SqlFieldType::kInt64, false},  // 成交时间(纳秒)
        {"ord_status_id", SqlFieldType::kInt64, false},  // 委托状态id
        {"margin_mode", SqlFieldType::kChar, true},  // 保证金模式
    }};

    // 主键 (1个)
    static constexpr std::array<std::string_view, 1> kPrimaryKeys = {"tdno"};

    // 获取字段值
    static std::string GetFieldValue(const Trade& data, size_t field_index) {
        switch (field_index) {
            case 0:  // tdno (int64_t)
                return std::to_string(data.tdno);
            case 1:  // market (MarketCs)
                return fmt::format("'{}'", SimpleEscapeString(data.market));
            case 2:  // account_id (AccountIdCs)
                return fmt::format("'{}'", SimpleEscapeString(data.account_id));
            case 3:  // portfolio (PortfolioCs)
                return fmt::format("'{}'", SimpleEscapeString(data.portfolio));
            case 4:  // instrument (InstrumentCs)
                return fmt::format("'{}'", SimpleEscapeString(data.instrument));
            case 5:  // strat_id (PolicyNoCs)
                return fmt::format("'{}'", SimpleEscapeString(data.strat_id));
            case 6:  // private_no (PrivateNoCs)
                return fmt::format("'{}'", SimpleEscapeString(data.private_no));
            case 7:  // ordno (int64_t)
                return std::to_string(data.ordno);
            case 8:  // td_side (CharCs)
                return fmt::format("'{}'", data.td_side);
            case 9:  // pos_side (CharCs)
                return fmt::format("'{}'", data.pos_side);
            case 10:  // px_type (CharCs)
                return fmt::format("'{}'", data.px_type);
            case 11:  // td_px (double)
                return DoubleToSqlString(data.td_px);
            case 12:  // td_qty (double)
                return DoubleToSqlString(data.td_qty);
            case 13:  // td_val (double)
                return DoubleToSqlString(data.td_val);
            case 14:  // filled_time (int64_t)
                return std::to_string(data.filled_time);
            case 15:  // ord_status_id (int64_t)
                return std::to_string(data.ord_status_id);
            case 16:  // margin_mode (CharCs)
                return fmt::format("'{}'", data.margin_mode);
            default:
                return "NULL";
        }
    }
};

} // namespace gtrade
