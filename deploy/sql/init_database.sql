-- 数据库初始化脚本
-- 自动生成于: 2025-07-13
-- 说明: 创建数据库并设置默认字符集和存储引擎

-- 创建数据库（如果不存在）
CREATE DATABASE IF NOT EXISTS `gtrade` 
CHARACTER SET utf8mb4 
COLLATE utf8mb4_unicode_ci;

-- 使用数据库
USE `gtrade`;

-- 设置会话级别的默认存储引擎
SET SESSION default_storage_engine = InnoDB;

-- 显示数据库配置信息
SELECT 
    SCHEMA_NAME as '数据库名',
    DEFAULT_CHARACTER_SET_NAME as '默认字符集',
    DEFAULT_COLLATION_NAME as '默认校对规则'
FROM information_schema.SCHEMATA 
WHERE SCHEMA_NAME = 'gtrade';

-- 显示当前存储引擎设置
SHOW VARIABLES LIKE 'default_storage_engine';

-- 推荐的my.cnf配置
-- [mysqld]
-- default-storage-engine=InnoDB
-- character-set-server=utf8mb4
-- collation-server=utf8mb4_unicode_ci
-- 
-- [mysql]
-- default-character-set=utf8mb4
-- 
-- [client]
-- default-character-set=utf8mb4