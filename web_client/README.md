# GTrade 策略管理 Web 界面

## 功能说明

这是一个基于 Vue 3 + Element Plus 的策略管理 Web 界面，用于管理和监控 GTrade 交易策略。

### 主要功能

1. **多标签页布局**
   - 每个标签对应一个策略模板（来自 `src/strategy_param/` 目录）
   - 自动加载并显示所有策略模板

2. **批量操作**
   - 全部启动：启动选中的所有策略
   - 全部停止：停止选中的所有策略
   - 全部重启：重启选中的所有策略
   - 刷新：重新加载策略列表

3. **策略表格**
   - 显示 indicator（指标）和 param（参数）列
   - 支持多选策略进行批量操作
   - 显示策略状态（运行中/已停止）
   - 每行提供启动/停止/重启按钮

4. **右键菜单**
   - 右键点击任意策略行
   - 可快速执行启动/停止/重启操作

## 本地部署（开发模式）

### 前提条件
- Node.js 16+
- npm 或 yarn

### 安装依赖
```bash
cd /opt/gtrade/web_client
npm install
```

### 启动开发服务器

#### 方式 1：后端在本机（默认配置）
```bash
npm run dev
```

#### 方式 2：后端在其他主机
创建 `.env` 文件并配置后端地址：
```bash
# 复制示例配置
cp .env.example .env

# 编辑 .env 文件，修改后端地址
# 例如：VITE_API_BASE_URL=http://192.168.1.100:46011
vim .env
```

然后启动：
```bash
npm run dev
```

### 访问方式

开发服务器启动后，可以通过以下方式访问：

1. **本机访问**：
   - http://localhost:3000

2. **其他主机访问**：
   - http://<服务器IP>:3000
   - 例如：http://192.168.1.100:3000

## 生产部署

### 构建生产版本
```bash
npm run build
```

构建完成后，静态文件将生成在 `dist/` 目录。

### 使用 Nginx 部署示例

1. **复制构建文件到 Nginx 目录**：
```bash
cp -r dist/* /var/www/html/gtrade/
```

2. **配置 Nginx**：
```nginx
server {
    listen 80;
    server_name your-domain.com;

    root /var/www/html/gtrade;
    index index.html;

    location / {
        try_files $uri $uri/ /index.html;
    }

    # API 代理
    location /api {
        proxy_pass http://localhost:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    }
}
```

3. **重启 Nginx**：
```bash
sudo systemctl restart nginx
```

## 后端服务配置

确保 GTrade 后端服务已启动并监听在正确的端口（默认 8080）：

```bash
cd /opt/gtrade/build2/bin
./gtrade
```

### 检查端口监听
```bash
netstat -tuln | grep 8080
```

应该看到类似输出：
```
tcp        0      0 0.0.0.0:8080            0.0.0.0:*               LISTEN
```

## 防火墙配置

如果需要从其他主机访问，确保防火墙允许相应端口：

```bash
# 允许前端端口（开发模式）
sudo ufw allow 3000/tcp

# 允许后端 API 端口
sudo ufw allow 8080/tcp

# 或者在生产环境中使用 Nginx（80/443 端口）
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp
```

## API 端点

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | /api/template/list | 获取所有策略模板 |
| GET | /api/template/config/:name | 获取模板配置 |
| GET | /api/strategy/list/:template | 获取指定模板的策略列表 |
| POST | /api/strategy/start/:id | 启动策略 |
| POST | /api/strategy/stop/:id | 停止策略 |
| POST | /api/strategy/restart/:id | 重启策略 |
| POST | /api/strategy/batch | 批量操作 |

## 故障排查

### 1. 无法从其他主机访问

**检查开发服务器监听地址**：
- 确保 `vite.config.js` 中配置了 `host: '0.0.0.0'`

**检查防火墙**：
```bash
sudo ufw status
```

**检查网络连接**：
```bash
# 在其他主机上测试连接
telnet <服务器IP> 3000
```

### 2. API 请求失败

**检查后端服务状态**：
```bash
ps aux | grep gtrade
curl http://localhost:8080/api/template/list
```

**检查 CORS 配置**：
- 后端已配置 CORS，应该允许所有来源访问

### 3. 模板列表为空

**检查策略模板文件**：
```bash
ls -la /opt/gtrade/src/strategy_param/
```

确保目录中有 `.yml` 文件。

## 开发说明

### 项目结构
```
web_client/
├── package.json           # 项目依赖
├── vite.config.js         # Vite 配置
├── index.html             # HTML 入口
├── .env.example           # 环境变量示例
└── src/
    ├── main.js            # Vue 应用入口
    ├── App.vue            # 主应用组件
    └── components/
        └── StrategyManager.vue  # 策略管理组件
```

### 技术栈
- **前端框架**：Vue 3 (Composition API)
- **UI 组件库**：Element Plus
- **HTTP 客户端**：Axios
- **构建工具**：Vite

### 添加新功能

1. 在 `src/components/` 中创建新组件
2. 在 `StrategyManager.vue` 中引入使用
3. 如需新的 API，在后端 `http_gateway.cpp` 中添加相应接口

## 许可证

本项目为 GTrade 项目的一部分。
