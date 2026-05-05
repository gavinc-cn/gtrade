//
// Created by dell on 2025/3/2.
//

#pragma once

#include "zrtools/zrt_misc.h"

#define GTRADE_AUTO_CODE_DICT_BEGIN
// 委托状态
struct OrderStatus {
    enum Em {
        _0 = '0',	// 未报
        _1 = '1',	// 正报
        _2 = '2',	// 已报
        _3 = '3',	// 部成
        _4 = '4',	// 全部成交
        _5 = '5',	// 已报待撤
        _6 = '6',	// 场内撤单
        _7 = '7',	// 部成待撤
        _8 = '8',	// 部成部撤
        _9 = '9',	// 废单
        _I = 'I',	// 冻结
        _A = 'A',	// 预埋单
    };
};

// 买卖标记
struct TradeSide {
    enum Em {
        Buy = 'b',	// 买
        Sell = 's',	// 卖
    };
};

// 多空方向
struct PosSide {
    enum Em {
        Long = 'l',	// 多
        Short = 's',	// 空
        Net = 'n',	// 净
    };
};

// 开平方向
struct PosEffect {
    enum Em {
        Open = 'o',	// 开
        Close = 'c',	// 平
    };
};

// 价格类型
struct PriceType {
    enum Em {
        Limit = 'l',	// 限价
        Market = 'm',	// 市价
        MakerOnly = 'y',	// 仅Maker
        Fok = 'o',	// 全部成交或立即取消单
        Fak = 'a',	// 立即成交并取消剩余单(IOC)
    };
};

// 委托来源
struct OrderSource {
    enum Em {
        Local = 'l',	// 本地
        Foreign = 'f',	// 外系统
    };
};

// 交易模式
struct TradeMode {
    enum Em {
        Isolated = 'i',	// 保证金逐仓
        Cross = 'c',	// 保证金全仓
        Cash = 'h',	// 现金
        SpotIsolated = 's',	// 现货逐仓(仅适用于现货带单) ，现货带单时，tdMode 的值需要指定为spot_isolated
    };
};

// 标的类型
struct InstType {
    enum Em {
        Spot = 's',	// 币币
        Margin = 'm',	// 币币杠杆
        Swap = 'w',	// 永续合约
        Futures = 'f',	// 交割合约
        Option = 'o',	// 期权
    };
};

// K线尺度
struct KLineScale {
    enum Em {
        Year = 'Y',	// 年
        Mon = 'm',	// 月
        Day = 'd',	// 日
        Hour = 'H',	// 时
        Min = 'M',	// 分
        Sec = 'S',	// 秒
    };
};

// 持仓来源
struct PosSource {
    enum Em {
        Exchange = 'E',	// 交易所推送
        Strategy = 'S',	// 策略计算
    };
};

#define GTRADE_AUTO_CODE_DICT_END

struct BackTestFillMode {
    enum {
        Simulate = 's',
        Immediate = 'i',
    };
};