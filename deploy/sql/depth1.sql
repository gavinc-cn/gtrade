-- depth 表结构定义
-- 深度数据表

-- 市场深度数据表
CREATE TABLE `depth1` (
    `ex_time` BIGINT NOT NULL COMMENT '交易所时间戳(纳秒)',
    `local_time` BIGINT NOT NULL COMMENT '本地接收时间戳(纳秒)',
    `datetime` VARCHAR(32) NOT NULL COMMENT '格式化时间字符串',
    `market` VARCHAR(16) NOT NULL COMMENT '市场',
    `instrument` VARCHAR(32) NOT NULL COMMENT '标的',
    `bid1_px` DOUBLE COMMENT '最优买价',
    `bid1_vol` DOUBLE COMMENT '最优买量',
    `ask1_px` DOUBLE COMMENT '最优卖价',
    `ask1_vol` DOUBLE COMMENT '最优卖量',
    PRIMARY KEY (`market`, `instrument`, `ex_time`)
) COMMENT='市场深度数据表';