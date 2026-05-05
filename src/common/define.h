
#pragma once

struct GlobalConst {
#if GTRADE_IN_BACKTEST_MODE
    static constexpr bool IsRealTrading = false;
    static constexpr bool IsBackTest = true;
#else
    static constexpr bool IsRealTrading = true;
    static constexpr bool IsBackTest = false;
#endif
};

