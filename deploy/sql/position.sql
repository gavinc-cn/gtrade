-- 持仓表
-- 对应C++结构体: Position

-- 持仓表
CREATE TABLE IF NOT EXISTS `position` (
    `market` VARCHAR(16) COMMENT '市场',
    `account_id` VARCHAR(32) COMMENT '账户ID',
    `inst_type` CHAR(1) COMMENT '标的类型',
    `instrument` VARCHAR(32) COMMENT '标的名称',
    `pos_side` CHAR(1) COMMENT '持仓方向[PosSide]',
    `portfolio` VARCHAR(32) COMMENT '组合',
    `margin_mode` CHAR(1) COMMENT '保证金模式',
    `avg_px` DOUBLE COMMENT '成交均价',
    `available` DOUBLE COMMENT '可用数量',
    `ex_time` BIGINT COMMENT '交易所时间',
    `local_time` BIGINT COMMENT '本地时间',
    `datetime` VARCHAR(32) COMMENT '格式化时间',
    `upl` DOUBLE COMMENT '未实现盈亏：交易所推送(账户持仓) / 本地计算(策略持仓)',
    `upl_ratio` DOUBLE COMMENT '未实现盈亏比率 - 来自交易所',
    `notional_usd` DOUBLE COMMENT '名义价值(USD) - 来自交易所',
    `total_cost` DOUBLE COMMENT '总成本（累计开仓价值 = Σ(数量 × 开仓价格)）',
    `realized_pnl` DOUBLE COMMENT '已实现盈亏（平仓产生的盈亏）',
    `fee_paid` DOUBLE COMMENT '已支付手续费',
    `pos_source` CHAR(1) COMMENT '持仓来源[PosSource]',
    PRIMARY KEY (`market`, `account_id`, `inst_type`, `instrument`, `pos_side`),
    KEY `idx_portfolio` (`portfolio`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='持仓表';
