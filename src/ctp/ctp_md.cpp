#include "ctp_md.h"
#include "ctp_converter.h"     // CtpConverter::ToDepth
#include "string_keys.h"       // k_ctp / k_md_front / k_broker_id / k_investor_id / k_password
#include "str_utils.h"         // zrt::to_str
#include "tbuffer.h"           // TBuffer
#include "zrtools/zrt_time.h"  // zrt::get_monotonic19

#include <cstring>
#include <cstdio>

// =====================================================================
// CtpMd 实现
// =====================================================================

CtpMd::CtpMd(const ServiceMap& pool, const GTradeConfig& gtrade_cfg, const std::string& md_account_id)
: m_gtrade_cfg(gtrade_cfg),
  m_strategy_engine(pool.at(k_StrategyEngine).get()),
  m_md_account_id(md_account_id)
{
    // 行情连接共享一条，凭证取自指定账户的 extra
    m_broker_id = GetExtra(k_broker_id);
    m_user_id   = GetExtra(k_investor_id);   // CTP 行情登录用户 = 投资者账号
    m_password  = GetExtra(k_password);
    m_md_front  = GetExtra(k_md_front);
    // 绑定到共享线程（与 OkxWs 一致：行情服务跑在 EnginePool 共享线程）
    SetThread(zrt::EnginePool::GetInstance().GetSharedThread());
}

CtpMd::~CtpMd() {
    CtpMd::Stop();
}

std::string CtpMd::GetExtra(const std::string& key, const std::string& def) const {
    const auto acc_it = m_gtrade_cfg.account_map.find(m_md_account_id);
    if (acc_it == m_gtrade_cfg.account_map.end()) return def;
    const auto ex_it = acc_it->second.extra.find(key);
    return (ex_it == acc_it->second.extra.end()) ? def : ex_it->second;
}

bool CtpMd::Init() {
    SPDLOG_INFO("[CtpMd] Init md_account={} md_front={} broker={}", m_md_account_id, m_md_front, m_broker_id);
    // 注册订阅请求处理器（MyHandler 线程执行）
    ZRT_ADD_HANDLER(kStratSubscribeQuote, CtpMd::OnSubscribeQuote);
    return true;
}

bool CtpMd::Start() {
    SPDLOG_INFO("[CtpMd] Start");
    // 创建 MdApi：流文件目录留空（当前目录），TCP 模式（非 UDP/组播）
    m_md_api = CThostFtdcMdApi::CreateFtdcMdApi();
    if (m_md_api == nullptr) {
        SPDLOG_ERROR("[CtpMd] CreateFtdcMdApi failed");
        return false;
    }
    m_spi = std::make_unique<CtpSpiMd>(this);
    m_md_api->RegisterSpi(m_spi.get());
    // 注册行情前置（需 char*，复制到本地缓冲避免生命周期问题）
    std::vector<char> front(m_md_front.begin(), m_md_front.end());
    front.push_back('\0');
    m_md_api->RegisterFront(front.data());
    // 启动 CTP 内部线程 → 异步触发 OnFrontConnected
    m_md_api->Init();
    return true;
}

void CtpMd::Stop() {
    if (m_md_api != nullptr) {
        SPDLOG_INFO("[CtpMd] Stop, release MdApi");
        m_md_api->Release();   // 释放后内部线程退出，不可再用
        m_md_api = nullptr;
    }
    m_spi.reset();
}

// ===== MyHandler 线程：策略引擎发来的订阅请求 =====
void CtpMd::OnSubscribeQuote(int msg_id, const BufPtr buffer) {
    (void)msg_id;
    const QuoteSub quote_sub = *reinterpret_cast<const QuoteSub*>(buffer->Data());
    SPDLOG_INFO("[CtpMd] subscribe market={} inst={}", quote_sub.market, quote_sub.inst_id);
    SubscribeInstrument(std::string(quote_sub.inst_id));
}

// ===== CTP 线程 =====
void CtpMd::OnFrontConnected() {
    m_connected = true;
    DoLogin();
}

void CtpMd::DoLogin() {
    if (m_md_api == nullptr) return;
    CThostFtdcReqUserLoginField req {};
    std::snprintf(req.BrokerID, sizeof(req.BrokerID), "%s", m_broker_id.c_str());
    std::snprintf(req.UserID,   sizeof(req.UserID),   "%s", m_user_id.c_str());
    std::snprintf(req.Password, sizeof(req.Password), "%s", m_password.c_str());
    // openctp 7x24 环境上述字段可为空；实盘需正确 broker_id/user/password
    const int ret = m_md_api->ReqUserLogin(&req, ++m_request_id);
    if (ret != 0) {
        SPDLOG_ERROR("[CtpMd] ReqUserLogin ret={}", ret);
    }
}

void CtpMd::OnRspUserLogin(bool ok) {
    m_logged_in = ok;
    if (!ok) return;
    // 登录成功：补订阅登录前暂存的合约
    std::unordered_set<std::string> pending_copy {};
    {
        std::lock_guard<std::mutex> lk(m_sub_mtx);
        pending_copy.swap(m_pending);
    }
    for (const auto& inst : pending_copy) {
        // 复用 SubscribeInstrument：此时已登录会即时下发
        SubscribeInstrument(inst);
    }
}

void CtpMd::SubscribeInstrument(const std::string& inst_id) {
    if (inst_id.empty()) return;
    {
        std::lock_guard<std::mutex> lk(m_sub_mtx);
        if (m_subscribed.count(inst_id)) return;   // 已订阅，去重
        if (!m_logged_in) {
            m_pending.emplace(inst_id);             // 未登录，暂存
            SPDLOG_INFO("[CtpMd] pending subscribe inst={} (not logged in)", inst_id);
            return;
        }
        m_subscribed.emplace(inst_id);
    }
    // 已登录且首次订阅：即时下发 SubscribeMarketData
    if (m_md_api == nullptr) return;
    char buf[32] = {0};
    std::snprintf(buf, sizeof(buf), "%s", inst_id.c_str());
    char* ptrs[1] = {buf};
    const int ret = m_md_api->SubscribeMarketData(ptrs, 1);
    SPDLOG_INFO("[CtpMd] SubscribeMarketData inst={} ret={}", inst_id, ret);
}

void CtpMd::OnDepth(const CThostFtdcDepthMarketDataField* md) {
    if (m_strategy_engine == nullptr || md == nullptr) return;

    Depth depth {};
    // CTP 字段 → GTrade Depth（market/symbol/ask1/bid1/ex_time 由转换器填充）
    CtpConverter::ToDepth(*md, depth, std::string(k_ctp));

    // 补充转换器无法得知的本地侧字段（与 OkxWs::OnBboTbt 对齐）
    depth.local_time = MyUTC().Epoch19();                       // 本地接收时间(纳秒)
    depth.monotonic  = zrt::get_monotonic19();                  // 单调时钟(纳秒)
    depth.seq_id     = ++m_depth_seq;                           // 本地自增序号
    if (depth.ex_time > 0) {
        zrt::fill_field(depth.datetime, MyUTC(depth.ex_time, 19).ToFormat());
    }

    // 投递到策略引擎（PostMsg 线程安全，可在 CTP 线程直接调用）
    m_strategy_engine->PostMsg(kDepth1, std::make_shared<TBuffer>(depth));
}
