-- K线数据表
-- 对应C++结构体: KLine

-- K线数据表
CREATE TABLE IF NOT EXISTS `kline` (
    `ex_time` BIGINT COMMENT '交易所时间戳(纳秒)',
    `local_time` BIGINT COMMENT '本地接收时间戳(纳秒)',
    `datetime` VARCHAR(32) COMMENT '格式化时间字符串',
    `market` VARCHAR(16) COMMENT '市场',
    `instrument` VARCHAR(32) COMMENT '标的',
    `coefficient` INT COMMENT '时间周期系数',
    `scale` CHAR(1) COMMENT '时间周期单位(d/H/M/S)',
    `open` DOUBLE COMMENT '开盘价',
    `high` DOUBLE COMMENT '最高价',
    `low` DOUBLE COMMENT '最低价',
    `close` DOUBLE COMMENT '收盘价',
    `volume` DOUBLE COMMENT '成交量',
    PRIMARY KEY (`market`, `instrument`, `ex_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='K线数据表';
