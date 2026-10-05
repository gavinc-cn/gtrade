#include "ctp_trader.h"
#include "string_keys.h"         // k_ctp / k_td_front / k_broker_id / k_investor_id / k_password
#include "tbuffer.h"             // TBuffer
#include "str_utils.h"

#include <cstring>
#include <cstdio>

// =====================================================================
// CtpTrader 实现
// =====================================================================

CtpTrader::CtpTrader(const GTradeConfig& gtrade_cfg, const std::string& account_id, MyHandler* strat_engine)
: m_strategy_engine(strat_engine),
  m_gtrade_cfg(gtrade_cfg),
  m_account_id(account_id)
{
    // 交易凭证取自账户 extra
    m_broker_id = GetExtra(k_broker_id);
    m_user_id   = GetExtra(k_investor_id);   // CTP 登录 UserID = 投资者账号
    m_password  = GetExtra(k_password);
    m_td_front  = GetExtra(k_td_front);
    SetThread(zrt::EnginePool::GetInstance().GetSharedThread());
}

CtpTrader::~CtpTrader() {
    CtpTrader::Stop();
}

std::string CtpTrader::GetExtra(const std::string& key, const std::string& def) const {
    const auto acc_it = m_gtrade_cfg.account_map.find(m_account_id);
    if (acc_it == m_gtrade_cfg.account_map.end()) return def;
    const auto ex_it = acc_it->second.extra.find(key);
    return (ex_it == acc_it->second.extra.end()) ? def : ex_it->second;
}

bool CtpTrader::Init() {
    SPDLOG_INFO("[CtpTrader] Init account={} td_front={} broker={} investor={}",
                m_account_id, m_td_front, m_broker_id, m_user_id);
    // 注册下单/撤单请求处理器（MyHandler 线程执行）
    ZRT_ADD_HANDLER(kPlaceOrder,  CtpTrader::OnPlaceOrder);
    ZRT_ADD_HANDLER(kCancelOrder, CtpTrader::OnCancelOrder);
    return true;
}

bool CtpTrader::Start() {
    SPDLOG_INFO("[CtpTrader] Start");
    m_trader_api = CThostFtdcTraderApi::CreateFtdcTraderApi();
    if (m_trader_api == nullptr) {
        SPDLOG_ERROR("[CtpTrader] CreateFtdcTraderApi failed");
        return false;
    }
    m_spi = std::make_unique<CtpSpiTrader>(this);
    m_trader_api->RegisterSpi(m_spi.get());
    // 订阅私有/公共流：QUICK 模式（只收登录后的增量，不重传历史）
    m_trader_api->SubscribePrivateTopic(THOST_TERT_QUICK);
    m_trader_api->SubscribePublicTopic(THOST_TERT_QUICK);
    // 注册交易前置
    std::vector<char> front(m_td_front.begin(), m_td_front.end());
    front.push_back('\0');
    m_trader_api->RegisterFront(front.data());
    // 启动 CTP 内部线程 → 异步触发 OnFrontConnected
    m_trader_api->Init();
    return true;
}

void CtpTrader::Stop() {
    if (m_trader_api != nullptr) {
        SPDLOG_INFO("[CtpTrader] Stop, release TraderApi");
        m_trader_api->Release();
        m_trader_api = nullptr;
    }
    m_spi.reset();
}

// ===== 登录 / 结算单确认 / 查询 =====
void CtpTrader::OnFrontConnected() {
    DoLogin();
}

void CtpTrader::DoLogin() {
    if (m_trader_api == nullptr) return;
    CThostFtdcReqUserLoginField req {};
    std::snprintf(req.BrokerID, sizeof(req.BrokerID), "%s", m_broker_id.c_str());
    std::snprintf(req.UserID,   sizeof(req.UserID),   "%s", m_user_id.c_str());
    std::snprintf(req.Password, sizeof(req.Password), "%s", m_password.c_str());
    // 注：openctp 7x24 环境无需 AuthCode/AppID 认证；实盘需先 ReqUserAuthentication。
    const int ret = m_trader_api->ReqUserLogin(&req, ++m_request_id);
    if (ret != 0) SPDLOG_ERROR("[CtpTrader] ReqUserLogin ret={}", ret);
}

void CtpTrader::OnRspUserLogin(bool ok, int front_id, int session_id) {
    if (!ok) return;
    m_front_id   = front_id;     // 保存撤单所需的前置/会话编号
    m_session_id = session_id;
    DoSettlementConfirm();
}

void CtpTrader::DoSettlementConfirm() {
    if (m_trader_api == nullptr) return;
    // 结算单确认：BrokerID/InvestorID
    CThostFtdcSettlementInfoConfirmField req {};
    std::snprintf(req.BrokerID,   sizeof(req.BrokerID),   "%s", m_broker_id.c_str());
    std::snprintf(req.InvestorID, sizeof(req.InvestorID), "%s", m_user_id.c_str());
    const int ret = m_trader_api->ReqSettlementInfoConfirm(&req, ++m_request_id);
    if (ret != 0) SPDLOG_ERROR("[CtpTrader] ReqSettlementInfoConfirm ret={}", ret);
}

void CtpTrader::OnSettlementConfirmed() {
    m_logged_in = true;
    SPDLOG_INFO("[CtpTrader] ready to trade, account={}", m_account_id);
    // 登录确认完成：拉取一次资金/持仓快照（后续由 OnRtnOrder/OnRtnTrade 增量维护）
    DoQryAccount();
    DoQryPosition();
}

void CtpTrader::DoQryAccount() {
    if (m_trader_api == nullptr) return;
    CThostFtdcQryTradingAccountField req {};
    std::snprintf(req.BrokerID,   sizeof(req.BrokerID),   "%s", m_broker_id.c_str());
    std::snprintf(req.InvestorID, sizeof(req.InvestorID), "%s", m_user_id.c_str());
    const int ret = m_trader_api->ReqQryTradingAccount(&req, ++m_request_id);
    if (ret != 0) SPDLOG_ERROR("[CtpTrader] ReqQryTradingAccount ret={}", ret);
}

void CtpTrader::DoQryPosition() {
    if (m_trader_api == nullptr) return;
    CThostFtdcQryInvestorPositionField req {};
    std::snprintf(req.BrokerID,   sizeof(req.BrokerID),   "%s", m_broker_id.c_str());
    std::snprintf(req.InvestorID, sizeof(req.InvestorID), "%s", m_user_id.c_str());
    // InstrumentID 留空 → 查询全部持仓
    const int ret = m_trader_api->ReqQryInvestorPosition(&req, ++m_request_id);
    if (ret != 0) SPDLOG_ERROR("[CtpTrader] ReqQryInvestorPosition ret={}", ret);
}

// ===== MyHandler 线程：下单/撤单 =====
void CtpTrader::OnPlaceOrder(int msg_id, const BufPtr buffer) {
    (void)msg_id;
    if (m_trader_api == nullptr) return;
    const OrderReq& req = *reinterpret_cast<const OrderReq*>(buffer->Data());

    // 1) 生成本地 OrderRef，并在 ReqOrderInsert 前建立 ref↔entno 映射
    const int64_t seq = ++m_order_ref_seq;
    const std::string order_ref = CtpConverter::MakeOrderRef(seq);
    {
        std::lock_guard<std::mutex> lk(m_ref_mtx);
        m_ref_to_entno[order_ref] = req.entno;
        m_entno_to_ref[req.entno] = order_ref;
    }

    // 2) 取账户，转换为 CTP InputOrderField
    const auto acc_it = m_gtrade_cfg.account_map.find(m_account_id);
    if (acc_it == m_gtrade_cfg.account_map.end()) {
        SPDLOG_ERROR("[CtpTrader] account not found {}", m_account_id);
        return;
    }
    CThostFtdcInputOrderField input {};
    CtpConverter::ToInputOrder(req, input, acc_it->second, order_ref);

    // 3) 报单
    const int ret = m_trader_api->ReqOrderInsert(&input, ++m_request_id);
    SPDLOG_INFO("[CtpTrader] ReqOrderInsert entno={} ref={} inst={} bs={} oc={} price={} qty={} ret={}",
                req.entno, order_ref, req.inst_id, req.bs_side, req.oc_side, req.price, req.amount, ret);
}

void CtpTrader::OnCancelOrder(int msg_id, const BufPtr buffer) {
    (void)msg_id;
    if (m_trader_api == nullptr) return;
    const WithdrawReq& req = *reinterpret_cast<const WithdrawReq*>(buffer->Data());

    // 用 entno 反查 OrderRef（撤单定位）
    std::string order_ref;
    {
        std::lock_guard<std::mutex> lk(m_ref_mtx);
        const auto it = m_entno_to_ref.find(req.entno);
        if (it == m_entno_to_ref.end()) {
            SPDLOG_ERROR("[CtpTrader] cancel: entno {} not found in ref map", req.entno);
            return;
        }
        order_ref = it->second;
    }

    // 按本地引用撤单：FrontID+SessionID+OrderRef+ActionFlag=Delete
    CThostFtdcInputOrderActionField act {};
    std::snprintf(act.BrokerID,     sizeof(act.BrokerID),     "%s", m_broker_id.c_str());
    std::snprintf(act.InvestorID,   sizeof(act.InvestorID),   "%s", m_user_id.c_str());
    std::snprintf(act.InstrumentID, sizeof(act.InstrumentID), "%s", req.instrument);
    std::snprintf(act.OrderRef,     sizeof(act.OrderRef),     "%s", order_ref.c_str());
    act.FrontID    = m_front_id;
    act.SessionID  = m_session_id;
    act.ActionFlag = THOST_FTDC_AF_Delete;
    const int ret = m_trader_api->ReqOrderAction(&act, ++m_request_id);
    SPDLOG_INFO("[CtpTrader] ReqOrderAction entno={} ref={} ret={}", req.entno, order_ref, ret);
}

// ===== CTP 线程：回报投递 =====
void CtpTrader::OnRtnOrder(const CThostFtdcOrderField* pOrder) {
    if (m_strategy_engine == nullptr || pOrder == nullptr) return;
    Order order {};
    CtpConverter::ToOrder(*pOrder, order, m_market, m_account_id);
    // OrderRef 反查本地 entno（ToOrder 已把 OrderRef 填入 private_no）
    {
        std::lock_guard<std::mutex> lk(m_ref_mtx);
        const auto it = m_ref_to_entno.find(std::string(pOrder->OrderRef));
        if (it != m_ref_to_entno.end()) order.entno = it->second;
    }
    m_strategy_engine->PostMsg(kPlaceOrderConfirm, std::make_shared<TBuffer>(order));
}

void CtpTrader::OnRtnTrade(const CThostFtdcTradeField* pTrade) {
    if (m_strategy_engine == nullptr || pTrade == nullptr) return;
    Trade trade {};
    CtpConverter::ToTrade(*pTrade, trade, m_market, m_account_id);
    // OrderRef 反查本地 entno 填 ordno（ToTrade 已把 OrderRef 填入 private_no）：
    // 引擎侧要靠 ordno 找到本地委托，才能补 tdno / 策略号 / 组合等字段。
    {
        std::lock_guard<std::mutex> lk(m_ref_mtx);
        const auto it = m_ref_to_entno.find(std::string(pTrade->OrderRef));
        if (it != m_ref_to_entno.end()) zrt::fill_field(trade.ordno, it->second);
    }
    m_strategy_engine->PostMsg(kTradePush, std::make_shared<TBuffer>(trade));
}

void CtpTrader::OnPosition(const CThostFtdcInvestorPositionField* pPos) {
    if (m_strategy_engine == nullptr || pPos == nullptr) return;
    Position pos {};
    CtpConverter::ToPosition(*pPos, pos, m_account_id, m_market);
    m_strategy_engine->PostMsg(kPositionPush, std::make_shared<TBuffer>(pos));
}

void CtpTrader::OnAccount(const CThostFtdcTradingAccountField* pAcc) {
    if (m_strategy_engine == nullptr || pAcc == nullptr) return;
    Balance bal {};
    CtpConverter::ToBalance(*pAcc, bal, m_account_id, m_market);
    m_strategy_engine->PostMsg(kBalancePush, std::make_shared<TBuffer>(bal));
}
