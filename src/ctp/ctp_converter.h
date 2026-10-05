#pragma once

#include "pch.h"
#include "ThostFtdcUserApiStruct.h"    // CTP 数据结构 (各 *Field)
#include "ThostFtdcUserApiDataType.h"  // CTP 类型 / 枚举值
#include "db_structures.h"             // GTrade Order / Trade / Position / Balance
#include "i_exchange_data.h"           // GTrade Depth
#include "i_strategy_engine.h"         // GTrade OrderReq / WithdrawReq
#include "i_client.h"                  // GTrade Account

#include <string>

// =====================================================================
// CtpConverter —— CTP 字段 ↔ GTrade 内部结构 的唯一转换点
// ---------------------------------------------------------------------
// 设计目的：把所有 CTP 私有协议特定逻辑（字段名、枚举字符值、时间格式、
//           订单关联键 OrderRef）隔离在此处。上层（CtpMd / CtpTrader /
//           StrategyEngine）只处理统一的 GTrade 内部结构，完全不感知 CTP。
//
// 一致性：结构体内存布局由 ThostFtdcUserApiStruct.h (6.7.x) 决定，必须与
//         链接的 .so 库版本匹配（都使用 6.7.x）。
//
// 枚举映射依据（CTP DataType.h 实测值，见 src/3rd/ctp/README.md）：
//   Direction     : D_Buy='0'  D_Sell='1'
//   OffsetFlag    : OF_Open='0' OF_Close='1' OF_CloseToday='3' OF_CloseYesterday='4'
//   HedgeFlag     : HF_Speculation='1'
//   OrderPriceType: OPT_AnyPrice='1'(市价) OPT_LimitPrice='2'(限价)
//   TimeCondition : TC_IOC='1' TC_GFD='3'
//   OrderStatus   : OST_AllTraded='0' OST_PartTradedQueueing='1' OST_NoTradeQueueing='3'
//                   OST_Canceled='5' OST_Unknown='a'
//   PosiDirection : PD_Long='2' PD_Short='3' PD_Net='1'
// =====================================================================
class CtpConverter {
public:
    // ===== 行情：CTP 深度行情 → GTrade Depth（按需求只填 1 档）=====
    // market: 写入 Depth.market（如 "ctp"）
    static void ToDepth(const CThostFtdcDepthMarketDataField& md, Depth& out,
                        const std::string& market);

    // ===== 下单：GTrade OrderReq → CTP InputOrderField =====
    // order_ref: CTP 报单引用（本地 entno 转字符串，用于回报关联回本地订单）
    static void ToInputOrder(const OrderReq& req, CThostFtdcInputOrderField& out,
                             const Account& account, const std::string& order_ref);

    // ===== 委托回报：CTP Order → GTrade Order =====
    static void ToOrder(const CThostFtdcOrderField& src, Order& out,
                        const std::string& market, const std::string& account_id);

    // ===== 成交回报：CTP Trade → GTrade Trade =====
    static void ToTrade(const CThostFtdcTradeField& src, Trade& out,
                        const std::string& market, const std::string& account_id);

    // ===== 持仓查询：CTP InvestorPosition → GTrade Position =====
    static void ToPosition(const CThostFtdcInvestorPositionField& src, Position& out,
                           const std::string& account_id, const std::string& market);

    // ===== 资金查询：CTP TradingAccount → GTrade Balance =====
    static void ToBalance(const CThostFtdcTradingAccountField& src, Balance& out,
                          const std::string& account_id, const std::string& market);

    // ===== 订单关联键：本地 entno → CTP OrderRef（char[13] 字符串）=====
    static std::string MakeOrderRef(int64_t entno);

    // ===== CTP 时间 → epoch19 纳秒 =====
    // trading_day: "YYYYMMDD"；update_time: "HH:MM:SS"；millisec: 毫秒
    // 注：CTP 行情时间为北京时间(CST, UTC+8)，这里换算为 UTC epoch19。
    static int64_t CtpTimeToEpoch(const char* trading_day,
                                  const char* update_time, int millisec);

private:
    // 枚举映射（GTrade 内部 ↔ CTP）
    static char BsSideToCtpDirection(char bs_side);                   // 'b'/'s' -> '0'/'1'
    static char OcSideToCtpOffset(char oc_side);                      // 'o'/'c' -> '0'/'1'
    static char PriceTypeToCtp(char price_type, char& time_cond);     // 'l'/'m' -> OPT + 输出 TimeCondition
    static char CtpOrderStatusToGTrade(char ctp_status);              // CTP OST -> GTrade OrderStatus(dict.h)
    static char CtpPosiDirToGTradePosSide(char posi_dir);             // '2'/'3'/'1' -> 'l'/'s'/'n'
    static char CtpOffsetToGTradeOcSide(char offset_flag);            // '0'/'1'/'3'/'4' -> 'o'/'c'
    static char CtpTradePosSide(char direction, char offset_flag);    // 买开/卖平->'l'，卖开/买平->'s'

    // CTP 无效价（DBL_MAX ≈ 1.797693e+308，表示无报价）置 0
    static double CtpPrice(double p);
};
