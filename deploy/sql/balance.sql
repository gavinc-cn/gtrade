-- 资金表
-- 对应C++结构体: Balance

-- 资金表
CREATE TABLE IF NOT EXISTS `balance` (
    `ex_time` BIGINT COMMENT '交易所时间(纳秒)',
    `local_time` BIGINT COMMENT '本地时间(纳秒)',
    `datetime` VARCHAR(32) COMMENT '格式化时间',
    `market` VARCHAR(16) COMMENT '市场',
    `account_id` VARCHAR(32) COMMENT '账户ID',
    `currency` VARCHAR(8) COMMENT '币种',
    `available` DOUBLE COMMENT '可用余额',
    `frozen` DOUBLE COMMENT '冻结余额',
    `total` DOUBLE COMMENT '总余额',
    PRIMARY KEY (`market`, `account_id`, `currency`),
    KEY `idx_account` (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='资金表';
