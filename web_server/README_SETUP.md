# Web Client 数据库集成设置指南

本指南说明如何让 web_client 从数据库查询并显示策略信息。

## 问题说明

Web Client 当前无法显示策略信息，因为：
1. MySQL数据库未运行
2. 数据库中没有策略数据

## 解决方案

### 方案一：使用 Docker Compose（推荐）

这是最简单的方法，会自动启动MySQL并设置所有必要的服务。

#### 步骤 1: 启动 MySQL 容器

```bash
cd /opt/gtrade

# 只启动MySQL服务
docker-compose -f docker-compose.app.yml up -d mysql

# 等待MySQL就绪（约10-15秒）
docker-compose -f docker-compose.app.yml logs -f mysql

# 看到 "ready for connections" 后按 Ctrl+C
```

#### 步骤 2: 初始化数据库表结构

```bash
# 执行SQL初始化脚本
docker exec -i gtrade_mysql mysql -ugtrade -pgtrade123 gtrade < deploy/sql/strat_info.sql
```

#### 步骤 3: 插入示例数据

```bash
cd web_server

# 使用本地配置运行数据插入脚本
FLASK_ENV=development python3 << 'EOF'
import sys
sys.path.insert(0, '/opt/gtrade/web_server')

# 临时修改配置使用localhost:3307
import os
os.environ['DB_HOST'] = 'localhost'
os.environ['DB_PORT'] = '3307'

# 执行数据插入
exec(open('test_insert_sample_data.py').read())
EOF
```

#### 步骤 4: 启动 Web 服务器

```bash
cd /opt/gtrade/web_server

# 使用环境变量指定数据库连接
DB_HOST=localhost DB_PORT=3307 python3 app.py
```

服务器将在 http://localhost:5000 启动

#### 步骤 5: 启动前端

新开一个终端：

```bash
cd /opt/gtrade/web_client

# 首次运行需要安装依赖
npm install

# 启动开发服务器
npm run dev
```

前端将在 http://localhost:5173 启动（Vite默认端口）

#### 步骤 6: 访问应用

打开浏览器访问: http://localhost:5173

你应该能看到策略列表中显示示例数据。

### 方案二：手动设置（如果没有 Docker）

如果您的环境没有 Docker，需要手动安装 MySQL：

#### 步骤 1: 安装 MySQL

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install mysql-server

# 启动MySQL
sudo systemctl start mysql
```

#### 步骤 2: 创建数据库和用户

```bash
sudo mysql -u root << 'EOF'
CREATE DATABASE gtrade;
CREATE USER 'gtrade'@'localhost' IDENTIFIED BY 'gtrade123';
GRANT ALL PRIVILEGES ON gtrade.* TO 'gtrade'@'localhost';
FLUSH PRIVILEGES;
EOF
```

#### 步骤 3: 初始化表结构

```bash
cd /opt/gtrade
mysql -ugtrade -pgtrade123 gtrade < deploy/sql/strat_info.sql
```

#### 步骤 4: 修改 Web 服务器配置

编辑 `web_server/config.py`，修改数据库配置：

```python
# 将 DB_HOST 从 'mysql' 改为 'localhost'
DB_HOST = os.environ.get('DB_HOST') or 'localhost'
DB_PORT = int(os.environ.get('DB_PORT') or 3306)  # 标准MySQL端口
```

#### 步骤 5: 插入示例数据并启动服务

```bash
cd /opt/gtrade/web_server

# 插入示例数据
python3 test_insert_sample_data.py

# 启动Web服务器
python3 app.py
```

#### 步骤 6: 启动前端（同上）

```bash
cd /opt/gtrade/web_client
npm install
npm run dev
```

## 验证设置

### 测试数据库连接

```bash
cd /opt/gtrade/web_server

python3 << 'EOF'
import os
os.environ['DB_HOST'] = 'localhost'
os.environ['DB_PORT'] = '3307'  # 如果使用Docker，否则改为3306

from database import get_db_manager
db = get_db_manager()

if db.test_connection():
    print("✓ 数据库连接成功!")
else:
    print("✗ 数据库连接失败")
EOF
```

### 测试API端点

```bash
# 测试健康检查
curl http://localhost:5000/api/health

# 测试策略列表
curl http://localhost:5000/api/template/list

# 测试获取特定模板的策略
curl http://localhost:5000/api/strategy/list/StratSMA
```

## 常见问题

### 1. 前端无法连接后端

确保：
- Web服务器正在运行 (http://localhost:5000)
- 检查浏览器控制台是否有CORS错误
- 前端配置中的API地址正确

### 2. 数据库连接失败

检查：
```bash
# 如果使用Docker
docker ps | grep gtrade_mysql

# 如果使用本地MySQL
sudo systemctl status mysql

# 测试连接
mysql -h localhost -P 3307 -ugtrade -pgtrade123 gtrade  # Docker
# 或
mysql -h localhost -ugtrade -pgtrade123 gtrade  # 本地MySQL
```

### 3. 表格显示空白

可能原因：
- 数据库中没有数据
- API返回数据格式不正确
- 前端解析数据失败

检查步骤：
```bash
# 1. 检查数据库中是否有数据
mysql -h localhost -P 3307 -ugtrade -pgtrade123 gtrade \
  -e "SELECT * FROM strat_info;"

# 2. 检查API返回
curl http://localhost:5000/api/strategy/list/StratSMA | jq

# 3. 查看浏览器开发者工具的Network和Console标签
```

## 数据结构说明

策略信息表 `strat_info` 包含以下字段：

- `id`: 策略ID (自增)
- `strat_name`: 策略名称 (唯一)
- `strat_template`: 策略模板类型 (如 StratSMA, FutureArbitrageV3)
- `param`: 策略参数 (JSON格式)
- `indicator`: 策略指标 (JSON格式)
- `status`: 运行状态 (0=停止, 1=运行中, 2=暂停)
- `create_time`: 创建时间
- `update_time`: 更新时间

Web Client 会：
1. 从 `/api/template/list` 获取所有策略模板
2. 从 `/api/strategy/list/{template}` 获取每个模板的策略列表
3. 从 `/api/template/config/{template}` 获取模板的参数和指标定义
4. 动态生成表格列显示参数和指标

## 后续开发

如需添加真实的C++后端集成：

1. 修改 `app.py` 中的策略操作函数 (start_strategy, stop_strategy等)
2. 调用C++后端的HTTP接口 (在 http_gateway.cpp 中实现)
3. 确保策略状态同步到数据库

当前实现只更新数据库状态，不实际控制C++策略引擎。
