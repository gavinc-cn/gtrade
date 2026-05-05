<div align="center">

# GTrade

**高性能量化加密货币交易平台**

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![Python](https://img.shields.io/badge/Python-3.12-green.svg)](https://www.python.org/)
[![Vue 3](https://img.shields.io/badge/Vue-3.4-brightgreen.svg)](https://vuejs.org/)
[![License](https://img.shields.io/badge/License-Proprietary-red.svg)]()

[功能特性](#-功能特性) · [系统架构](#-系统架构) · [快速开始](#-快速开始) · [策略开发](#-策略开发) · [AI集成](#-ai-集成)

</div>

---

GTrade 是一个面向专业量化交易的全栈平台，覆盖从策略研发、历史回测、实盘交易到风控对账的完整生命周期。核心交易引擎基于 C++17 构建，采用消息驱动架构，支持纳秒级延迟的实盘交易，同时通过 MCP 协议与 AI 深度集成，实现自然语言驱动的策略开发和管理。

## ✨ 功能特性

### 🏎️ 极致性能的交易引擎
- **C++17 核心**：消息驱动架构 + 无锁共享内存 IPC，关键路径亚微秒级延迟
- **SIMD 加速 JSON 解析**：关键数据路径使用字节跳动的 `sonic_json`（SIMD 指令加速）
- **引擎线程池**：每个服务（策略、MySQL、HTTP、WebSocket）运行在独立命名线程上，共享工作线程池

### 🔄 三级策略隔离架构
统一 `IStrategyProxy` 接口，支持三种部署模式，按需平衡延迟与安全：

| 模式 | 隔离级别 | 延迟 | 适用场景 |
|------|----------|------|----------|
| **进程内** | 共享进程 | 最低 | 高频策略，对延迟极致敏感 |
| **子进程** | 独立进程 | 低 | 需要崩溃隔离的策略，共享内存环缓冲区通信 |
| **远程** | 跨机器 | 网络 | 分布式部署，ZMQ ROUTER/DEALER 通信 |

子进程模式支持**自动崩溃重启 + 检查点恢复**，策略状态通过共享内存持久化，重启后无缝恢复。

### 🛡️ 高可用（HA）系统
- **主备架构**：Primary/Standby 自动切换
- **WAL 持久化**：共享内存 WAL 环缓冲区（写入延迟 < 1μs）+ 独立 `gtrade_repl` 进程负责文件 WAL 和网络复制
- **快照恢复**：定期快照订单/成交/仓位，备机快速拉起
- **防脑裂**：单调递增 Term 机制防止过期主机接管

### 📊 专业级回测引擎
- CSV 历史数据回放，支持全品种全周期
- 两种成交模式：**立即成交**（`i`）和 **Orderbook 撮合模拟**（`s`）
- 可调速度（倍速 / 固定间隔）
- 输出：交易记录 CSV + 策略日志，完整复盘分析
- 策略代码**零修改**：同一份策略代码通过编译标志切换实盘/回测模式

### 🧩 动态策略插件系统
- 策略编译为 `.so` 共享库，运行时 `dlopen` 热加载
- CMake 宏 `add_strategy_plugin()` 一键构建
- 编译模式校验：防止回测版 `.so` 误用于实盘环境
- 自注册工厂模式：`StrategyFactory::Register()` + 静态初始化

### 🤖 AI 原生集成
通过 MCP（Model Context Protocol）与 AI 助手（Claude、Cursor 等）深度集成：

- **自然语言管理**：对话式创建、启动、停止策略
- **AI 代码生成**：自动生成策略 C++ 代码并编译为 `.so` 插件
- **一键回测**：AI 编写策略 → 自动编译 → 自动回测 → 输出分析报告
- **自动研究流水线**（AutoResearch）：AI 策略生成 → 回测评估 → 晋升筛选（Sharpe/Calmar/IC）→ 人工审批 → 实盘部署

### 🔌 多客户端支持
- **Web 控制台**（Vue 3 + Element Plus）：策略监控、参数配置、指标查看
- **Qt 桌面客户端**（Qt5 + gRPC/TLS）：跨平台原生体验，支持 mTLS 认证
- **REST API**：Flask 后端，JWT 认证，完整的策略 CRUD 和监控接口

### 📈 对账与风控
- **自动对账服务**：每 60 秒比对组合仓位 vs 交易所实际仓位
- **Web 对账面板**：实时展示差异，快速定位问题
- **Slack 告警**：策略异常、仓位不一致等事件实时推送

---

## 🏗 系统架构

```
                          ┌──────────────────┐
                          │  Qt Desktop App  │  (Qt5 + gRPC/TLS)
                          └────────┬─────────┘
                                   │ gRPC
 ┌──────────────┐    ┌────────────┴─────────────┐    ┌────────────────┐
 │  Web Client  │◄──►│     Flask Web Server      │    │   MCP Server   │
 │ (Vue3+EP)    │REST│   (Python/REST API)       │    │  (AI/AI IDE)   │
 └──────────────┘    └────────────┬─────────────┘    └───────┬────────┘
                                   │ HTTP                      │ HTTP
                     ┌─────────────┴──────────────────────────┘
                     │
           ┌─────────┴─────────┐
           │  C++ Core Engine  │
           │  ┌──────────────┐ │
           │  │StrategyEngine│ │── OkxWs (WebSocket 行情)
           │  │  ┌────────┐  │ │── OkxTrade (WebSocket 交易)
           │  │  │Strategies│ │ │── OkexClient (REST API)
           │  │  └────────┘  │ │── MySqlGateway (持久化)
           │  └──────────────┘ │── HttpGateway (管理接口)
           │                   │── MessageServer (Slack 告警)
           │                   │── DesktopGateway (gRPC)
           │                   │── QueryServer (并行查询)
           └───────────────────┘
                     │
           ┌─────────┴──────────┐
           │   gtrade_repl      │  (WAL 持久化 + HA 复制)
           └────────────────────┘
```

---

## 📦 技术栈

| 层级 | 技术 |
|------|------|
| **核心引擎** | C++17, CMake, Boost.Asio, spdlog, sonic_json, TA-Lib, Crypto++, LZ4 |
| **网络通信** | WebSocket++, cpp-httplib, gRPC, ZMQ |
| **数据存储** | MySQL, ClickHouse, 共享内存 IPC |
| **Web 后端** | Python 3.12, Flask, SQLAlchemy, PyJWT |
| **Web 前端** | Vue 3.4, Element Plus 2.5, Vite 5 |
| **桌面客户端** | Qt5, gRPC, Protobuf, Conan |
| **AI 集成** | FastMCP, Anthropic Claude, AutoResearch Pipeline |
| **部署运维** | Docker, Docker Compose, Shell Scripts |

---

## 🚀 快速开始

### 环境要求

- Ubuntu 22.04+ / macOS
- CMake 3.10+, GCC 11+ (支持 C++17)
- MySQL 8.0+
- Node.js 18+ (Web 前端)
- Python 3.10+ (Web 后端 / MCP Server)

### 1. 克隆项目

```bash
git clone <repo-url> && cd gtrade
```

### 2. 配置环境变量

```bash
cp .env.example .env
# 编辑 .env 填入数据库密码、Web 凭据、API Key 等
```

### 3. 构建 C++ 核心引擎

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -G Ninja
ninja install
```

### 4. 启动服务

**方式一：Docker Compose（推荐）**

```bash
docker-compose -f docker-compose.app.yml up -d
```

**方式二：手动启动**

```bash
cd deploy
bash start.sh   # 一键启动：gtrade + gtrade_repl + web_server + web_client + mcp_server
```

### 5. 访问系统

| 服务 | 地址 |
|------|------|
| Web 控制台 | http://localhost:5173 |
| REST API | http://localhost:5000 |
| MCP Server (SSE) | http://127.0.0.1:8765 |

---

## 🧠 策略开发

### 内置策略类型

| 策略 | 说明 |
|------|------|
| `SpotGrid` | 现货网格交易 |
| `StratSMA` | SMA 均线交叉 |
| `FutureArbitrageV3` | 期货套利（如 BTC-USDT vs BTC-USDT-SWAP） |
| `IndicatorKline` | K线指标记录 |
| `StratSpotGridFull` | 高级现货网格 |
| `StrategyRecorder` | 行情数据录制 |

### 创建自定义策略

**1. 编写策略类**（继承 `StrategyBase`）

```cpp
class MyStrategy : public StrategyBase {
public:
    void OnInit() override { /* 初始化 */ }
    void OnStart() override { /* 订阅行情、启动定时器 */ }
    void OnStop() override { /* 清理资源 */ }
    // ... 实现行情回调、下单等
};
```

**2. 注册工厂函数**

```cpp
// strategy_plugin.h 定义的 C 接口
extern "C" StrategyBase* gtrade_create_strategy() {
    return new MyStrategy();
}
```

**3. 编译为 .so 插件**

```cmake
add_strategy_plugin(my_strategy sources...)
```

**4. 部署运行**

通过 Web 控制台、REST API 或 AI 助手加载并启动策略。

### 策略 API 能力

- ✅ 行情订阅（订单簿、K线、成交流）
- ✅ 下单 / 撤单
- ✅ 同步市场数据查询
- ✅ 定时器（一次性 / 周期性）
- ✅ Slack 消息通知
- ✅ 检查点持久化（崩溃可恢复）
- ✅ 策略参数 & 指标持久化到数据库

---

## 🤖 AI 集成

### MCP Server

GTrade 提供完整的 MCP Server，支持 AI 助手通过自然语言管理整个交易系统：

```json
// Claude Desktop 配置
{
  "mcpServers": {
    "gtrade": {
      "command": "python",
      "args": ["mcp_server/server.py"],
      "env": {
        "FLASK_BASE_URL": "http://127.0.0.1:5000"
      }
    }
  }
}
```

**MCP 工具集：**

| 类别 | 功能 |
|------|------|
| **查询** | 列出策略类型、策略实例、指标数据 |
| **控制** | 创建、启动、停止、重启、删除策略 |
| **回测** | 运行回测、查询状态、获取结果 |
| **代码生成** | 读取参考代码、写入策略代码、编译插件 |

### AutoResearch 自动研究流水线

AI 驱动的端到端策略研究：

1. **策略生成**：AI 根据市场特征自动生成策略代码
2. **自动回测**：编译 → 多参数组合并行回测
3. **晋升评估**：自动筛选（Sharpe > 1.5, MaxDD < 15%, Calmar > 1.0）
4. **熔断保护**：连续失败自动暂停，防止资源浪费
5. **人工审批**：实盘部署前需人工确认

---

## 🗄 项目结构

```
gtrade/
├── src/                    # C++ 核心引擎源码
│   ├── strategy_engine/    # 策略引擎（加载、调度、代理）
│   ├── strategy/           # 内置策略实现
│   ├── websocket/          # WebSocket 客户端（OKX）
│   ├── clients/            # REST API 客户端
│   ├── db_server/          # 数据库网关（MySQL/ClickHouse）
│   ├── http_server/        # HTTP 管理接口
│   ├── desktop_gateway/    # Qt 桌面端 gRPC 网关
│   ├── message_server/     # Slack 消息服务
│   ├── query_server/       # 并行查询服务
│   ├── backtest/           # 回测引擎
│   ├── strategy_runner/    # 子进程策略运行器
│   ├── services/           # 核心服务（HA、WAL、Reconciliation）
│   ├── common/             # 公共工具
│   ├── 3rd/                # 第三方库
│   └── proto/              # Protobuf/gRPC 定义
├── web_server/             # Python Flask REST API
├── web_client/             # Vue 3 前端
├── qt_client/              # Qt5 桌面客户端
├── mcp_server/             # MCP AI 集成服务
├── autoresearch/           # AI 自动策略研究流水线
├── strategy_plugin/        # 策略插件输出目录
├── strategy_config/        # 策略实例配置
├── config/                 # 系统配置
├── deploy/                 # 部署脚本 & SQL
├── proto/                  # 协议定义
├── test/                   # 测试
├── tools/                  # 开发工具
└── python/                 # Python SDK（gtrade_py）
```

---

## 🔐 安全设计

- 环境变量管理所有密钥，代码零硬编码
- JWT 认证保护 REST API
- gRPC + TLS/mTLS 保护桌面客户端通信
- 策略子进程崩溃隔离，不影响主引擎
- 检查点校验和防止状态损坏

---

## 📋 支持的交易所

| 交易所 | 状态 | 功能 |
|--------|------|------|
| **OKX** | ✅ 主力支持 | 现货 / 永续合约 / WebSocket 行情+交易 |
| **OKX Demo** | ✅ 沙盒环境 | 模拟交易，策略测试 |

---

## 🛠 运维管理

```bash
cd deploy
bash start.sh     # 启动所有服务
bash stop.sh      # 停止所有服务
bash restart.sh   # 重启
bash status.sh    # 查看运行状态
```

**日志管理：**
- 主引擎：spdlog 异步日志，按天轮转
- 每个策略独立日志文件，便于排查
- Core Dump 启用（`ulimit -c unlimited`），支持事后分析

---

<div align="center">

**GTrade** — 从策略研发到实盘交易，AI 原生的量化交易平台

</div>
