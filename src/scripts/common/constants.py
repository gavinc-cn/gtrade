RiskControlStatusNormal = 'Normal'
RiskControlStatusNotice = 'Notice'
RiskControlStatusWarning = 'Warning'
RiskControlStatusError = 'Error'

RiskControlValidValues = {
    RiskControlStatusNormal,
    RiskControlStatusNotice,
    RiskControlStatusWarning,
    RiskControlStatusError,
}

OrderStatusNone = ''
OrderStatusOpen = 'open'
OrderStatusClosed = 'closed'
OrderStatusCanceled = 'canceled'
OrderStatusSubmitted = 'submitted'
OPEN = 'open'
CLOSED = 'closed'
CANCELED = 'canceled'

OrderStatusValidValues = {
    OrderStatusNone,
    OrderStatusOpen,
    OrderStatusClosed,
    OrderStatusClosed,
    OrderStatusSubmitted,
}

OrderSideBuy = 'buy'
OrderSideSell = 'sell'
BUY = 'buy'
SELL = 'sell'
OPEN_LONG = 'open_long'
OPEN_SHORT = 'open_short'
CLOSE_LONG = 'close_long'
CLOSE_SHORT = 'close_short'

OrderSideValidValues = {
    OrderSideBuy,
    OrderSideSell,
}
EXTENDED_SIDE_VALUES = {
    OrderSideBuy,
    OrderSideSell,
    BUY,
    SELL,
    OPEN_LONG,
    OPEN_SHORT,
    CLOSE_LONG,
    CLOSE_SHORT
}

OrderTypeLimit = 'limit'
OrderTypeLimitMaker = 'limit-maker'
OrderTypeStopLimit = 'stop-limit'
LIMIT = 'limit'
LIMIT_MAKER = 'limit-maker'
STOP_LIMIT = 'stop_limit'

OrderTypeValidValues = {
    OrderTypeLimit,
    OrderTypeLimitMaker,
    OrderTypeStopLimit,
}

ExchangeTypeInverse = 'inverse'
ExchangeTypeDirect = 'direct'

ExchangeTypeValidValues = {
    ExchangeTypeDirect,
    ExchangeTypeInverse,
}

NotionalMethodBase = 'base'
NotionalMethodQuote = 'quote'

NotionalMethodValidValues = {
    NotionalMethodBase,
    NotionalMethodQuote,
}