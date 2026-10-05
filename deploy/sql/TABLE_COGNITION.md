# 表级认知（TABLE COGNITION）

> 借鉴 AOCI-CODE 的 Database Volume 思想：每张关键表一行 FRAS——F=职责、R=强关系（改表必须同时确认的代码）、A=主键与对外契约、S=非显性约束（从表结构推不出或容易踩的坑）。
> 维护规则：`deploy/sql/*.sql` 是表结构的唯一权威；结构变更时同步更新本文件对应行。写代码前先读本表，避免重新翻 schema。

## 关键表速览

| 表 | F 职责 | R 强关系 | A 主键/契约 | S 非显性约束 |
|---|--------|----------|-------------|--------------|
| `strat_info` | 策略注册信息与运行状态（名称/模板/参数/指标/状态） | 读写方：`db_server/mysql_gateway`、`http_server`；参数模板定义在 `src/strategy_param/` | PK `id`；UK `strat_name` | `status`：0-停止 / 1-运行 / 2-暂停；`param`、`indicator` 为 JSON 列，改字段需同步 C++ 侧 JSON 解析 |
| `kline` | K 线 OHLCV 落库 | 写入方：`db_server/mysql_gateway`（由 `KlineManager` 聚合产出）；对应 C++ 结构体 `KLine` | PK `(market, instrument, ex_time)` | PK 不含周期字段（`coefficient`/`scale`），多周期同表存储时写入方需自行保证 `(market, instrument, ex_time)` 唯一，否则跨周期同刻会主键冲突 |
| `depth1` | 一档行情快照（bid1/ask1）落库 | 写入方：`db_server/mysql_gateway`；来源 `kDepth1` 消息 | PK `(market, instrument, ex_time)` | 表名中的 `1` 即档位，只存一档；建表语句与其他表不同：无 `IF NOT EXISTS`、未显式声明 `ENGINE=InnoDB` |
| `order` | 委托全生命周期（报单/确认/成交/撤单/错误） | 写入方：`OrderManager` → `db_server/mysql_gateway`；对应 C++ 结构体 `Order` | PK `entno`（本地委托号）；索引 `policy_no`、`status` | 字段映射约定：`filled_px` → 持仓 `avg_price`（OrderManager 内）；`ex_entno` 才是交易所单号，勿与 `entno` 混用；状态三件套 `status`/`status_id`/`status_gid` 并存 |
| `trade` | 成交回报落库 | 写入方：`OrderManager` 流程；对应 C++ 结构体 `Trade` | PK `tdno` | `ordno` 关联 `order.entno`，两表字段增删需联动确认 |
| `position` | 账户/策略持仓 | 写入方：`OrderManager` / 持仓对账流程；对应 C++ 结构体 `Position` | PK `(market, account_id, inst_type, instrument, pos_side)` | 与 `portfolio_position` 同源结构体 `Position`，列结构几乎一致；改列必须两表同步 |
| `portfolio_position` | 组合维度持仓（多 `portfolio` 维度） | 同 `position` | PK 在 `position` 基础上多 `portfolio` 维度 | 同 `position`：与 `position` 表同步维护 |
| `balance` | 账户资金（可用/冻结/总额） | 写入方：`OrderManager` 资金更新流程；对应 C++ 结构体 `Balance` | PK `(market, account_id, currency)` | 同账户同币种仅一行，写入方需 upsert 语义而非追加插入 |
| `strategy_log` | 策略运行日志 | 写入方：策略日志通道；对应 C++ 结构体 `StrategyLog` | PK `(strat_id, log_time, seq)` | `seq` 用于同纳秒多条日志防主键碰撞，写方必须递增填入；`content` 上限 512 字符 |
| `instrument_scope` | web 设置页维护的标的范围订阅（引擎启动时加载恢复） | 读写方：`db_server/mysql_gateway`（异步全量替换 / 启动同步读）；写入方 `StrategyEngine::ApplyInstrumentScope` | PK `(market, inst_id, inst_type)` | 保存语义为全量替换：事务内先 `DELETE` 再逐条 `INSERT`；空范围合法且会清空表；C++ 侧单次上限 `kMaxScopeItems`（128），改上限需同步两端。`inst_type` 是标的唯一性的一部分（OKX 币币杠杆复用现货 instId），空串表示待引擎按行情缓存归一化；2026-10-03 由二元主键升级，ALTER 段见 `instrument_scope.sql` |
| `init_database.sql` | （非表）建库脚本：`gtrade` 库 + utf8mb4 + InnoDB | 部署入口：`deploy/` | — | 仅建库与字符集，不建表；各表由独立 `*.sql` 创建 |

## 使用方式

- 查表结构细节：读 `deploy/sql/<表名>.sql`（本文件是索引与约束提示，不是 schema 替代品）。
- 新增表：在 `deploy/sql/` 加 `*.sql`，并在这里补一行 FRAS。
- 数据库连接：MySQL `gtrade/gtrade123`，Docker 端口 3307（见根 AGENTS.md「Database」）。
