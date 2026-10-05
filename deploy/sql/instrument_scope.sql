-- 标的范围订阅（web 设置页维护；引擎启动时加载）
--
-- 主键是三元组 (market, inst_id, inst_type)：instType 是标的唯一性的一部分 —— OKX 的币币杠杆
-- （MARGIN）复用现货的 instId，且 instIdCode 与全部规格字段都相同，二元主键无法区分两者。
-- inst_type 用 OKX 风格字符串（SPOT/SWAP/FUTURES/OPTION/MARGIN），与 HTTP/前端同型。
-- 本系统不做杠杆交易，出厂清单不含 MARGIN，但主键形状保持不变。
CREATE TABLE IF NOT EXISTS `instrument_scope` (
    `market` varchar(32) NOT NULL COMMENT '市场（okx/okx_dummy/ctp）',
    `inst_id` varchar(32) NOT NULL COMMENT '标的',
    `inst_type` varchar(16) NOT NULL DEFAULT '' COMMENT '标的类型（SPOT/SWAP/FUTURES/…）；空串=待引擎按行情缓存归一化',
    `update_time` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`market`, `inst_id`, `inst_type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='标的范围订阅';

-- ─────────────────────────────────────────────────────────────────────────────
-- 已建库升级（2026-10-03：二元主键 → 三元主键）。新库执行上面的 CREATE 即可，无需这一段。
--
--   ALTER TABLE `instrument_scope`
--       ADD COLUMN `inst_type` varchar(16) NOT NULL DEFAULT '' COMMENT '标的类型（SPOT/SWAP/FUTURES/…）' AFTER `inst_id`,
--       DROP PRIMARY KEY,
--       ADD PRIMARY KEY (`market`, `inst_id`, `inst_type`);
--
-- 老行的 inst_type 会是空串：**不必手工回填**——引擎启动恢复时按行情缓存推断补全（该 instId
-- 只有一种类型就补上），随后任一"保存"都会把整表按真实类型重写（保存是全量替换）。
-- 想让库里立刻可读时可选用下面的启发式回填（OKX 的 instId 自带类型线索：`-SWAP` 后缀=永续，
-- 带 `-YYMMDD`/`-YYMMDD` 形式到期日后缀=交割，其余=现货；ctp 标的不适用，保持空串）：
--
--   UPDATE `instrument_scope` SET `inst_type` = 'SWAP'
--       WHERE `inst_type` = '' AND `inst_id` LIKE '%-SWAP';
--   UPDATE `instrument_scope` SET `inst_type` = 'FUTURES'
--       WHERE `inst_type` = '' AND `inst_id` REGEXP '-[0-9]{6}$';
--   UPDATE `instrument_scope` SET `inst_type` = 'SPOT'
--       WHERE `inst_type` = '' AND `market` IN ('okx', 'okx_dummy');
-- ─────────────────────────────────────────────────────────────────────────────
