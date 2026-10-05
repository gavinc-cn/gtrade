#include "ctp_converter.h"
#include "dict.h"      // GTrade 枚举 (TradeSide/PosEffect/PriceType/OrderStatus/PosSide)
#include <ctime>
#include <cstdio>
#include <cstring>
#include <cmath>

// CTP 无效价阈值：CTP 用 DBL_MAX(≈1.797693e+308) 表示无报价，正常价格远小于此
static constexpr double kCtpInvalidPrice = 1e18;

// ---------------------------------------------------------------------
// 私有：枚举与数值映射
// ---------------------------------------------------------------------

double CtpConverter::CtpPrice(double p) {
    if (!std::isfinite(p) || p >= kCtpInvalidPrice || p < 0) return 0.0;
    return p;
}

char CtpConverter::BsSideToCtpDirection(char bs_side) {
    // GTrade TradeSide::Buy='b' -> CTP D_Buy='0'; Sell='s' -> D_Sell='1'
    return (bs_side == TradeSide::Buy) ? THOST_FTDC_D_Buy : THOST_FTDC_D_Sell;
}

char CtpConverter::OcSideToCtpOffset(char oc_side) {
    // GTrade PosEffect::Open='o' -> OF_Open='0'; Close='c' -> OF_Close='1'
    // 注：上交所(SHFE/INE)平仓需区分平今(3)/平昨(4)，此处默认 '1'(平)，
    //     精确的平今/平昨由 CtpTrader 依据合约交易所覆盖 OrderField。
    return (oc_side == PosEffect::Open) ? THOST_FTDC_OF_Open : THOST_FTDC_OF_Close;
}

char CtpConverter::PriceTypeToCtp(char price_type, char& time_cond) {
    // 返回 OrderPriceType，并通过输出参数返回 TimeCondition
    switch (price_type) {
        case PriceType::Market:  // 市价 -> 任意价 + GFD
            time_cond = THOST_FTDC_TC_GFD;
            return THOST_FTDC_OPT_AnyPrice;
        case PriceType::Fak:     // IOC -> 限价 + IOC
            time_cond = THOST_FTDC_TC_IOC;
            return THOST_FTDC_OPT_LimitPrice;
        case PriceType::Fok:     // FOK -> 限价 + IOC（完整FOK需VolumeCondition，此处简化）
            time_cond = THOST_FTDC_TC_IOC;
            return THOST_FTDC_OPT_LimitPrice;
        case PriceType::Limit:   // 限价
        default:
            time_cond = THOST_FTDC_TC_GFD;
            return THOST_FTDC_OPT_LimitPrice;
    }
}

char CtpConverter::CtpOrderStatusToGTrade(char ctp_status) {
    // CTP OrderStatusType -> GTrade OrderStatus (dict.h)
    switch (ctp_status) {
        case THOST_FTDC_OST_AllTraded:             return OrderStatus::_4; // 全部成交
        case THOST_FTDC_OST_PartTradedQueueing:    return OrderStatus::_3; // 部成
        case THOST_FTDC_OST_PartTradedNotQueueing: return OrderStatus::_8; // 部成部撤
        case THOST_FTDC_OST_NoTradeQueueing:       return OrderStatus::_2; // 已报(待成交)
        case THOST_FTDC_OST_NoTradeNotQueueing:    return OrderStatus::_6; // 场内撤单
        case THOST_FTDC_OST_Canceled:              return OrderStatus::_6; // 撤单
        case THOST_FTDC_OST_Unknown:               return OrderStatus::_1; // 正报
        case THOST_FTDC_OST_NotTouched:            return OrderStatus::_1; // 未触发
        case THOST_FTDC_OST_Touched:               return OrderStatus::_1; // 触发
        default:                                   return OrderStatus::_1;
    }
}

char CtpConverter::CtpPosiDirToGTradePosSide(char posi_dir) {
    switch (posi_dir) {
        case THOST_FTDC_PD_Long:  return PosSide::Long;
        case THOST_FTDC_PD_Short: return PosSide::Short;
        default:                  return PosSide::Net; // '1' Net
    }
}

char CtpConverter::CtpOffsetToGTradeOcSide(char offset_flag) {
    // Open '0' -> 'o'；任何 Close 类(1/3/4) -> 'c'
    return (offset_flag == THOST_FTDC_OF_Open) ? PosEffect::Open : PosEffect::Close;
}

char CtpConverter::CtpTradePosSide(char direction, char offset_flag) {
    // 国内期货的持仓方向由"买卖方向 + 开平标志"共同决定：
    //   买开(Buy+Open) / 卖平(Sell+Close*) → 影响多头持仓
    //   卖开(Sell+Open) / 买平(Buy+Close*) → 影响空头持仓
    // 平今/平昨(OF_CloseToday/OF_CloseYesterday) 与平仓(OF_Close) 对持仓方向的影响一致。
    const bool is_open = (offset_flag == THOST_FTDC_OF_Open);
    const bool is_buy  = (direction == THOST_FTDC_D_Buy);
    return (is_buy == is_open) ? PosSide::Long : PosSide::Short;
}

// ---------------------------------------------------------------------
// 订单关联键 / 时间
// ---------------------------------------------------------------------

std::string CtpConverter::MakeOrderRef(int64_t seq) {
    // CTP OrderRef 为 char[13]，格式化为 12 位左补 0。
    // 注意：GTrade 的 entno 为 epoch 级大数(19位)，远超 OrderRef 容量。
    //       调用方(CtpTrader)应维护独立的递增序号 seq(1,2,3...) 并建立 seq->entno 映射，
    //       回报通过 OrderRef 反查回本地 entno。
    char buf[16] = {0};
    std::snprintf(buf, sizeof(buf), "%012lld", static_cast<long long>(seq));
    return buf;
}

int64_t CtpConverter::CtpTimeToEpoch(const char* trading_day, const char* update_time, int millisec) {
    if (!trading_day || !update_time) return 0;
    int y = 0, mon = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(trading_day, "%4d%2d%2d", &y, &mon, &d) != 3) return 0;
    std::sscanf(update_time, "%2d:%2d:%2d", &h, &mi, &s); // 容错：失败取 0
    std::tm t{};
    t.tm_year = y - 1900;
    t.tm_mon  = mon - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min  = mi;
    t.tm_sec  = s;
    std::time_t epoch = ::timegm(&t);  // 先按 UTC 解析该时间字符串
    epoch -= 8 * 3600;                  // CTP 为北京时间(CST,UTC+8)，换算为真实 UTC
    return static_cast<int64_t>(epoch) * 1000000000LL
         + static_cast<int64_t>(millisec) * 1000000LL;
}

// ---------------------------------------------------------------------
// 行情：CTP DepthMarketData -> GTrade Depth (1 档)
// ---------------------------------------------------------------------
void CtpConverter::ToDepth(const CThostFtdcDepthMarketDataField& md, Depth& out,
                           const std::string& market) {
    std::memset(&out, 0, sizeof(out));
    std::snprintf(out.market, sizeof(out.market), "%s", market.c_str());
    zrt::fill_field(out.symbol, md.InstrumentID);

    // 仅 1 档（CTP 普通行情只有买卖 1 档；Level-2 才有 2~5 档）
    const double ask = CtpPrice(md.AskPrice1);
    const double bid = CtpPrice(md.BidPrice1);
    out.ask_price[0]  = ask;
    out.ask_amount[0] = static_cast<double>(md.AskVolume1);
    out.bid_price[0]  = bid;
    out.bid_amount[0] = static_cast<double>(md.BidVolume1);
    out.ask_cnt = (ask > 0) ? 1 : 0;
    out.bid_cnt = (bid > 0) ? 1 : 0;

    // 交易所时间(纳秒)。local_time / datetime / seq_id / monotonic 由调用方 CtpMd 填充。
    out.ex_time = CtpTimeToEpoch(md.TradingDay, md.UpdateTime, md.UpdateMillisec);
}

// ---------------------------------------------------------------------
// 下单：GTrade OrderReq -> CTP InputOrderField
// ---------------------------------------------------------------------
void CtpConverter::ToInputOrder(const OrderReq& req, CThostFtdcInputOrderField& out,
                                const Account& account, const std::string& order_ref) {
    std::memset(&out, 0, sizeof(out));

    // 账户与合约。BrokerID 从 account.extra["broker_id"] 取（openctp 7x24 可空）。
    // InvestorID 用账户的投资者账号（优先 extra["investor_id"]，缺失则用 account_id）。
    const auto ex_end = account.extra.end();
    const auto broker_it = account.extra.find("broker_id");
    if (broker_it != ex_end) {
        std::snprintf(out.BrokerID, sizeof(out.BrokerID), "%s", broker_it->second.c_str());
    }
    const auto inv_it = account.extra.find("investor_id");
    const std::string& investor = (inv_it != ex_end) ? inv_it->second : std::string(req.account_id);
    std::snprintf(out.InvestorID,   sizeof(out.InvestorID),   "%s", investor.c_str());
    std::snprintf(out.InstrumentID, sizeof(out.InstrumentID), "%s", req.inst_id);
    std::snprintf(out.OrderRef,     sizeof(out.OrderRef),     "%s", order_ref.c_str());

    // 方向 / 开平 / 投机标志
    out.Direction        = BsSideToCtpDirection(req.bs_side);
    out.CombOffsetFlag[0] = OcSideToCtpOffset(req.oc_side);
    out.CombOffsetFlag[1] = '\0';
    out.CombHedgeFlag[0]  = THOST_FTDC_HF_Speculation; // 投机
    out.CombHedgeFlag[1]  = '\0';

    // 价格类型 / 时间条件 / 价格 / 数量
    char time_cond = THOST_FTDC_TC_GFD;
    out.OrderPriceType      = PriceTypeToCtp(req.price_type, time_cond);
    out.TimeCondition       = time_cond;
    out.LimitPrice          = CtpPrice(req.price);
    out.VolumeTotalOriginal = static_cast<int>(req.amount);
    out.VolumeCondition     = THOST_FTDC_VC_AV;          // AnyVolume
    out.MinVolume           = 1;
    out.ContingentCondition = THOST_FTDC_CC_Immediately; // 立即触发
    out.ForceCloseReason    = THOST_FTDC_FCC_NotForceClose;
    out.IsAutoSuspend       = 0;
    out.UserForceClose      = 0;
}

// ---------------------------------------------------------------------
// 委托回报：CTP Order -> GTrade Order
// ---------------------------------------------------------------------
void CtpConverter::ToOrder(const CThostFtdcOrderField& src, Order& out,
                           const std::string& market, const std::string& account_id) {
    std::memset(&out, 0, sizeof(out));
    std::snprintf(out.market,     sizeof(out.market),     "%s", market.c_str());
    std::snprintf(out.account_id, sizeof(out.account_id), "%s", account_id.c_str());
    zrt::fill_field(out.inst_id, src.InstrumentID);  // CTP char[81] → inst_id char[32] 安全截断
    // OrderRef 关联本地订单：CtpTrader 通过 OrderRef 反查 entno 后回填 out.entno
    std::snprintf(out.private_no, sizeof(out.private_no), "%s", src.OrderRef);

    // bs_side/oc_side 为 CharCs（单字符包装），需用 zrt::fill_field 赋值（无 operator=(char)）
    zrt::fill_field(out.bs_side, (src.Direction == THOST_FTDC_D_Buy) ? TradeSide::Buy : TradeSide::Sell);
    zrt::fill_field(out.oc_side, CtpOffsetToGTradeOcSide(src.CombOffsetFlag[0]));
    out.price     = CtpPrice(src.LimitPrice);
    out.amount    = static_cast<double>(src.VolumeTotalOriginal);
    out.filled    = static_cast<double>(src.VolumeTraded);
    out.remain    = static_cast<double>(src.VolumeTotal - src.VolumeTraded);
    // status 为 CharCs，需用 zrt::fill_field 赋值
    zrt::fill_field(out.status, CtpOrderStatusToGTrade(src.OrderStatus));
    // ex_entno: 交易所系统订单号(OrderSysID)
    out.ex_entno  = std::atoll(src.OrderSysID);
    out.ent_time  = CtpTimeToEpoch(src.TradingDay, src.InsertTime, 0);
    out.update_time = CtpTimeToEpoch(src.TradingDay, src.UpdateTime, 0);
}

// ---------------------------------------------------------------------
// 成交回报：CTP Trade -> GTrade Trade
// ---------------------------------------------------------------------
void CtpConverter::ToTrade(const CThostFtdcTradeField& src, Trade& out,
                           const std::string& market, const std::string& account_id) {
    std::memset(&out, 0, sizeof(out));
    std::snprintf(out.market,     sizeof(out.market),     "%s", market.c_str());
    std::snprintf(out.account_id, sizeof(out.account_id), "%s", account_id.c_str());
    zrt::fill_field(out.instrument, src.InstrumentID);  // CTP char[81] → instrument char[32] 安全截断
    std::snprintf(out.private_no, sizeof(out.private_no), "%s", src.OrderRef);

    // td_side / pos_side 为 CharCs，需用 zrt::fill_field 赋值
    zrt::fill_field(out.td_side, (src.Direction == THOST_FTDC_D_Buy) ? TradeSide::Buy : TradeSide::Sell);
    // pos_side 必须填：Trade 流出到 OrderManager::UpdateStrategyPosition 时用它区分开多/开空，
    // 缺省(0)会被当成"非空头"从而把卖出开仓记成买入开仓（详见工单）。
    zrt::fill_field(out.pos_side, CtpTradePosSide(src.Direction, src.OffsetFlag));
    out.td_px   = CtpPrice(src.Price);
    out.td_qty  = static_cast<double>(src.Volume);
    // 名义价量积（不含合约乘数，期货真实成交额还需 ×ContractMultiplier）——上层未用到该字段
    out.td_val  = CtpPrice(src.Price) * static_cast<double>(src.Volume);
    out.filled_time = CtpTimeToEpoch(src.TradingDay, src.TradeTime, 0);

    // 以下字段本转换层无法得知，由上层补齐（见 CtpTrader::OnRtnTrade / StrategyEngine::OnTradePush）：
    //   tdno（本地成交号，OrderManager::CreateTradeId 生成）
    //   ordno（本地委托号，CtpTrader 用 OrderRef 反查）
    //   strat_id / portfolio / px_type / margin_mode / ord_status_id（取自本地委托）
}

// ---------------------------------------------------------------------
// 持仓查询：CTP InvestorPosition -> GTrade Position
// ---------------------------------------------------------------------
void CtpConverter::ToPosition(const CThostFtdcInvestorPositionField& src, Position& out,
                              const std::string& account_id, const std::string& market) {
    std::memset(&out, 0, sizeof(out));
    std::snprintf(out.market,     sizeof(out.market),     "%s", market.c_str());
    std::snprintf(out.account_id, sizeof(out.account_id), "%s", account_id.c_str());
    zrt::fill_field(out.instrument, src.InstrumentID);  // CTP char[81] → instrument char[32] 安全截断

    // pos_side 为 CharCs，需用 zrt::fill_field 赋值
    zrt::fill_field(out.pos_side, CtpPosiDirToGTradePosSide(src.PosiDirection));
    out.available = static_cast<double>(src.Position);      // 总持仓
    out.avg_px    = 0;                                       // 均价需由 PositionCost/Position 计算，此处简化
    out.upl       = src.PositionProfit;
    out.realized_pnl = src.CloseProfit;
    // 注：CTP 持仓分今日/昨日(PositionDate)，CtpTrader 汇总时需合并；此处为单条转换。
}

// ---------------------------------------------------------------------
// 资金查询：CTP TradingAccount -> GTrade Balance
// ---------------------------------------------------------------------
void CtpConverter::ToBalance(const CThostFtdcTradingAccountField& src, Balance& out,
                             const std::string& account_id, const std::string& market) {
    std::memset(&out, 0, sizeof(out));
    std::snprintf(out.market,     sizeof(out.market),     "%s", market.c_str());
    std::snprintf(out.account_id, sizeof(out.account_id), "%s", account_id.c_str());
    std::snprintf(out.currency,   sizeof(out.currency),   "%s", src.CurrencyID);

    out.available = src.Available;
    out.frozen    = src.FrozenMargin + src.FrozenCash + src.FrozenCommission;
    out.total     = src.Balance;
}
