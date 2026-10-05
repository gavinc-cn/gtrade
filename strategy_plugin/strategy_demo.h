//
// Created by dell on 2025/2/20.
//

#pragma once

#include <bitset>
#include "strategy_base.h"
#include "logger_config.h"
#include "zrtools/zrt_file.h"
#include "zrtools/bollinger.h"
#include "zrtools/zrt_shm_direct.h"
#include "i_exchange_data.h"
#include "dict.h"
#include "zrtools/zrt_math.h"
#include "sonic_helper.h"


class StratDemo: public StrategyBase {
    struct StratConfig {
        // 账户
        std::string account_id {};
        // 市场
        std::string market {};
        // 合约1
        std::string instrument {};
        // 最大下单量
        double max_entamt {};
        // 最小下单量
        double min_entamt {};
        // 1H K线窗口大小, 一天即是24
        size_t kline_window_size {};
        // 手续费
        double fee {};
    };

    struct OrderInfo: public OrderReq {
        CharCs status;
        double filled;
        int64_t update_time;
    };

    struct Indicator {
        double cur_pos {};
        int strat_status {};
        char status_str [32] {};
        OrderInfo buy_ent_info {};
        OrderInfo sell_ent_info {};

        operator std::string() {return ToStr();}

        // 序列化为JSON字符串
        std::string ToJson() const {
            JsonObj obj {};
            obj.AddMember("cur_pos", cur_pos);
            obj.AddMember("strat_status", strat_status);
            obj.AddMember("status_str", status_str);
            return obj;
        }

        std::string ToStr() const {
            return fmt::format("{},buy_ent_info:{},sell_ent_info:{}", ToJson(),
                zrt::to_str(buy_ent_info), zrt::to_str(sell_ent_info));
        }
    };

    struct StratStatus {
        enum {
            Initializing,
            Ready2Order,
            Wait4Fill,
            Cancelling,
            CancelSuccess,
            RoundDone,
        };
        static std::string Dump(const int em) {
            const static std::unordered_map<int, std::string> em_map = {
                {Initializing, "初始化中"},
                {Ready2Order, "准备挂单"},
                {Wait4Fill, "等待成交"},
                {Cancelling, "正撤"},
                {CancelSuccess, "撤成"},
                {RoundDone, "本轮结束"},
            };
            const auto iter = em_map.find(em);
            return iter != em_map.end() ? iter->second : "";
        }
    };

    struct DataStatus {
        enum {
            KlineReady,
            Count,
        };
    };

    struct TimerId {
        enum {
            SaveIndicator = 1,  // 保存指标到MySQL的定时器
        };
    };

public:
    StratDemo(const GTradeConfig& gtrade_cfg, MyHandler* strat_engine, const std::string& strat_id):
    StrategyBase(gtrade_cfg, strat_engine, strat_id)
    {
    }

private:
    // 策略控制
    bool OnInit(const YAML::Node& strat_yml) override;
    bool OnStart() override;
    bool OnStop() override;
    bool OnPause() override;
    bool OnResume() override;
    // 消息回调
    void OnQryKLineRsp(int msg_id, const BufPtr buffer);
    void OnQryOrderRsp(int msg_id, const BufPtr buffer);
    void OnIndicatorKLineClosePush(int msg_id, const BufPtr buffer);
    void OnDepth1(int msg_id, const BufPtr buffer);
    void OnPlaceOrderRsp(int msg_id, const BufPtr buffer);
    void OnPlaceOrderConfirm(int msg_id, const BufPtr buffer);
    void OnTradePush(int msg_id, const BufPtr buffer);
    void OnPosPush(int msg_id, const BufPtr buffer);
    void OnPortfolioPosPush(int msg_id, const BufPtr buffer);
    void OnBalancePush(int msg_id, const BufPtr buffer);
    void OnTimerEvent(int msg_id, const BufPtr buffer);

    // 工具函数
    void QryKLine(const std::string& inst_id);
    // void QryEntrust(int64_t entno, const std::string& market, const std::string& account_id);
    // void QryEntrust(const std::string& private_no);
    void ProcessKLine(const KLine& kline);
    void HandleOrderUpdate(OrderInfo& ent_info, const Order& order);
    void PlaceOrder(OrderInfo& ent_info, char bs_side, double price, double amount) const;
    void CancelOrder(OrderInfo& ent_info);
    bool BuySpread();
    bool SellSpread();
    void AdvancePos(const char bs_side, const double amount);
    void RevertPos(const char bs_side, const double amount);
    bool BothClosed() const {
        return zrt::equal_any_of(m_buy_ord_info->status, OrderStatus::_4, OrderStatus::_6, OrderStatus::_8, OrderStatus::_9) ||
            zrt::equal_any_of(m_sell_ord_info->status, OrderStatus::_4, OrderStatus::_6, OrderStatus::_8, OrderStatus::_9);
    }

    std::string GetOrderStr(const Order& order) const {
        return fmt::format(
            "[{}] {} {} {} {}\npx={:.8g} amt={:.8g}", zrt::equal(order.bs_side, TradeSide::Buy) ? "buy" : "sell",
            order.entno, order.market, order.inst_id, order.status,
            order.price, order.amount);
    }

    // 检查点辅助方法
    void CommitCheckpoint();
    void RestoreFromCheckpoint();

    StratConfig m_config {};
    zrt::DirectShm<Indicator> m_indicator {GetStratId()};
    int m_buy_cnt {};
    int m_sell_cnt {};
    Depth m_quote {};
    double m_target_pos {};
    OrderInfo* m_buy_ord_info {};
    OrderInfo* m_sell_ord_info {};
    std::bitset<DataStatus::Count> m_data_status {};
    // 检查点（策略自定义数据结构）
    StrategyCheckpointHelper<Indicator> m_checkpoint;
};
