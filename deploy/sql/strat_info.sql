-- 策略信息表
CREATE TABLE IF NOT EXISTS `strat_info` (
    `id` int(11) NOT NULL AUTO_INCREMENT COMMENT '主键ID',
    `strat_name` varchar(100) NOT NULL COMMENT '策略名称',
    `strat_template` varchar(50) NOT NULL COMMENT '策略模板类型',
    `create_time` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `update_time` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    `param` json NOT NULL COMMENT '策略参数JSON',
    `indicator` json DEFAULT NULL COMMENT '策略指标JSON',
    `status` tinyint(1) NOT NULL DEFAULT 0 COMMENT '策略状态: 0-停止, 1-运行, 2-暂停',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_strat_name` (`strat_name`),
    KEY `idx_strat_template` (`strat_template`),
    KEY `idx_create_time` (`create_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='策略信息表';