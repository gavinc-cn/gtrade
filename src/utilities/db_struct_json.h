//
// DB 结构体 → JSON（推送帧 / HTTP 补查共用一份序列化口径）
//
// 为什么放在 utilities：`sonic_helper.h` 依赖 `interface/str_types.h`，
// 把 JSON 序列化放进 interface/ 会形成 interface → utilities → interface 的反向依赖；
// utilities 已在 interface 之上，被 http_server / strategy_engine 共同依赖，放这里无环。
//
// 字段名与 DB 列名（`/api/orders`、`/api/trades` 的 SELECT 结果）保持一致，前端两条路径
// 可以同构合并；19 位大整数（>2^53）一律输出**字符串**，避免浏览器 JSON number 精度丢失
// ——与 `web_server/strategy_service` 的字符串化口径一致。
//
#pragma once

#include <string>

#include "db_structures.h"
#include "sonic_helper.h"

namespace zrt {

// 委托 → JSON 对象
inline void ToJson(JsonObj& obj, const Order& o) {
    obj.AddMember("entno", std::to_string(o.entno));
    obj.AddMember("status", o.status);
    obj.AddMember("fmt_time", std::to_string(o.fmt_time));
    obj.AddMember("market", std::string(o.market));
    obj.AddMember("account_id", std::string(o.account_id));
    obj.AddMember("portfolio", std::string(o.portfolio));
    obj.AddMember("inst_type", o.inst_type);
    obj.AddMember("inst_id", std::string(o.inst_id));
    obj.AddMember("inst_id_code", o.inst_id_code);
    obj.AddMember("policy_no", std::string(o.policy_no));
    obj.AddMember("private_no", std::string(o.private_no));
    obj.AddMember("bs_side", o.bs_side);
    obj.AddMember("pos_side", o.pos_side);
    obj.AddMember("oc_side", o.oc_side);
    obj.AddMember("trade_mode", o.trade_mode);
    obj.AddMember("price_type", o.price_type);
    obj.AddMember("price", o.price);
    obj.AddMember("amount", o.amount);
    obj.AddMember("ent_time", std::to_string(o.ent_time));
    obj.AddMember("expire_time", std::to_string(o.expire_time));
    obj.AddMember("ex_entno", std::to_string(o.ex_entno));
    obj.AddMember("filled_px", o.filled_px);
    obj.AddMember("filled", o.filled);
    obj.AddMember("remain", o.remain);
    obj.AddMember("confirm_time", std::to_string(o.confirm_time));
    obj.AddMember("filled_time", std::to_string(o.filled_time));
    obj.AddMember("update_time", std::to_string(o.update_time));
    obj.AddMember("source", o.source);
    obj.AddMember("err_code", o.err_code);
    obj.AddMember("err_msg", std::string(o.err_msg));
    obj.AddMember("drawno", std::to_string(o.drawno));
    obj.AddMember("draw_amt", o.draw_amt);
    obj.AddMember("withdraw_time", std::to_string(o.withdraw_time));
    obj.AddMember("status_id", o.status_id);
    obj.AddMember("status_gid", std::to_string(o.status_gid));
    obj.AddMember("trd_px", o.trd_px);
    obj.AddMember("trd_qty", o.trd_qty);
}

// 成交 → JSON 对象
inline void ToJson(JsonObj& obj, const Trade& t) {
    obj.AddMember("tdno", std::to_string(t.tdno));
    obj.AddMember("market", std::string(t.market));
    obj.AddMember("account_id", std::string(t.account_id));
    obj.AddMember("portfolio", std::string(t.portfolio));
    obj.AddMember("instrument", std::string(t.instrument));
    obj.AddMember("strat_id", std::string(t.strat_id));
    obj.AddMember("private_no", std::string(t.private_no));
    obj.AddMember("ordno", std::to_string(t.ordno));
    obj.AddMember("td_side", t.td_side);
    obj.AddMember("pos_side", t.pos_side);
    obj.AddMember("px_type", t.px_type);
    obj.AddMember("td_px", t.td_px);
    obj.AddMember("td_qty", t.td_qty);
    obj.AddMember("td_val", t.td_val);
    obj.AddMember("filled_time", std::to_string(t.filled_time));
    obj.AddMember("ord_status_id", t.ord_status_id);
    obj.AddMember("margin_mode", t.margin_mode);
}

// 持仓 → JSON 对象
inline void ToJson(JsonObj& obj, const Position& p) {
    obj.AddMember("market", std::string(p.market));
    obj.AddMember("account_id", std::string(p.account_id));
    obj.AddMember("inst_type", p.inst_type);
    obj.AddMember("instrument", std::string(p.instrument));
    obj.AddMember("pos_side", p.pos_side);
    obj.AddMember("portfolio", std::string(p.portfolio));
    obj.AddMember("margin_mode", p.margin_mode);
    obj.AddMember("avg_px", p.avg_px);
    obj.AddMember("available", p.available);
    obj.AddMember("ex_time", std::to_string(p.ex_time));
    obj.AddMember("local_time", std::to_string(p.local_time));
    obj.AddMember("upl", p.upl);
    obj.AddMember("upl_ratio", p.upl_ratio);
    obj.AddMember("notional_usd", p.notional_usd);
    obj.AddMember("total_cost", p.total_cost);
    obj.AddMember("realized_pnl", p.realized_pnl);
    obj.AddMember("fee_paid", p.fee_paid);
    obj.AddMember("pos_source", p.pos_source);
}

// 资金 → JSON 对象
inline void ToJson(JsonObj& obj, const Balance& b) {
    obj.AddMember("market", std::string(b.market));
    obj.AddMember("account_id", std::string(b.account_id));
    obj.AddMember("currency", std::string(b.currency));
    obj.AddMember("available", b.available);
    obj.AddMember("frozen", b.frozen);
    obj.AddMember("total", b.total);
    obj.AddMember("ex_time", std::to_string(b.ex_time));
    obj.AddMember("local_time", std::to_string(b.local_time));
}

}  // namespace zrt
