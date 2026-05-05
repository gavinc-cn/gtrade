-- 成交表
-- 对应C++结构体: Trade

-- 成交表
CREATE TABLE IF NOT EXISTS `trade` (
    `tdno` BIGINT COMMENT '成交号',
    `market` VARCHAR(16) COMMENT '市场',
    `account_id` VARCHAR(32) COMMENT '账户ID',
    `portfolio` VARCHAR(32) COMMENT '组合',
    `instrument` VARCHAR(32) COMMENT '标的名称',
    `strat_id` VARCHAR(32) COMMENT '策略编号',
    `private_no` VARCHAR(64) COMMENT '私有号',
    `ordno` BIGINT COMMENT '委托号',
    `td_side` CHAR(1) COMMENT '买卖方向(B/S)',
    `pos_side` CHAR(1) COMMENT '持仓方向',
    `px_type` CHAR(1) COMMENT '价格类型',
    `td_px` DOUBLE COMMENT '成交价格',
    `td_qty` DOUBLE COMMENT '成交数量',
    `td_val` DOUBLE COMMENT '成交金额',
    `filled_time` BIGINT COMMENT '成交时间(纳秒)',
    `ord_status_id` BIGINT COMMENT '委托状态id',
    `margin_mode` CHAR(1) COMMENT '保证金模式',
    PRIMARY KEY (`tdno`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='成交表';
