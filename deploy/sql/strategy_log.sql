-- 策略运行日志表
-- 对应C++结构体: StrategyLog

-- 策略运行日志表
CREATE TABLE IF NOT EXISTS `strategy_log` (
    `strat_id` VARCHAR(32) COMMENT '策略编号',
    `log_level` CHAR(1) COMMENT '日志等级: I/W/E',
    `log_time` BIGINT COMMENT '日志时间(纳秒)',
    `seq` SMALLINT UNSIGNED COMMENT '同纳秒内序号，防止主键碰撞',
    `content` VARCHAR(512) COMMENT '日志内容',
    PRIMARY KEY (`strat_id`, `log_time`, `seq`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='策略运行日志表';
