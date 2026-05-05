# OKX WebSocket 简单连接测试

## 概述

这是一个极简的 OKX WebSocket 连接测试程序，**不依赖项目的任何其他组件**，仅用于测试是否能连通 OKX WebSocket 服务器。

## 编译

```bash
cd /opt/gtrade/build2
cmake .. -DCMAKE_BUILD_TYPE=Release
make okx_ws_test -j12
```

可执行文件：`build2/bin/okx_ws_test` (约 14MB)

## 使用

```bash
# 使用默认 URL（测试环境）
./build2/bin/okx_ws_test

# 使用生产环境
./build2/bin/okx_ws_test wss://ws.okx.com:8443/ws/v5/public
```

## 测试流程

1. 初始化 TLS 上下文
2. 连接到 WebSocket 服务器
3. 等待连接建立（最多 10 秒）
4. 发送订阅消息（BTC-USDT bbo-tbt）
5. 等待接收消息（最多 10 秒）
6. 保持连接 3 秒
7. 关闭连接

## 输出示例（成功）

```
==========================================
OKX WebSocket Simple Connection Test
==========================================
[INFO] Target URL: wss://wspap.okx.com:8443/ws/v5/public
[TLS] Context initialized
[INFO] Connecting...
[WAIT] Waiting for connection... (1/10)
[CONN] ✓ WebSocket connection opened!
[SEND] {"op":"subscribe","args":[{"channel":"bbo-tbt","instId":"BTC-USDT"}]}

==========================================
[SUCCESS] ✓ Connection established!
==========================================
[INFO] Waiting for messages...
[RECV] Event message: {"event":"subscribe","arg":{"channel":"bbo-tbt","instId":"BTC-USDT"}}...
[RECV] ✓ Data message received!
[DATA] {"arg":{"channel":"bbo-tbt","instId":"BTC-USDT"},"data":[{"asks":[["96850.1","0.1"]],"bids":[["96850...

==========================================
[SUCCESS] ✓ Messages received!
==========================================
[INFO] Keeping connection for 3 more seconds...
[RECV] ✓ Data message received!
[DATA] {"arg":{"channel":"bbo-tbt","instId":"BTC-USDT"},"data":[{"asks":[["96851.2","0.15"]],"bids":[["96...

[INFO] Closing connection...
[CONN] Connection closed

==========================================
[DONE] Test completed successfully!
==========================================
```

## 代码说明

### 特点

- ✅ 独立程序，不依赖项目其他组件
- ✅ 仅链接基础库：pthread, ssl, crypto, boost_system
- ✅ 简单清晰的输出
- ✅ 自动测试连接和消息接收
- ✅ 约 200 行代码，易于理解

### 依赖

- websocketpp（header-only）
- Boost ASIO
- OpenSSL

### 核心功能

- `on_tls_init()`: 初始化 TLS 上下文
- `on_open()`: 连接建立后发送订阅消息
- `on_message()`: 接收并显示消息
- `on_fail()`: 显示连接失败信息
- `on_close()`: 处理连接关闭

## 常见问题

### TLS 握手失败

如果看到 `TLS handshake failed` 错误：

```
[CONN] ✗ Connection failed!
[ERROR] websocketpp.transport.asio.socket:8 - TLS handshake failed
```

**可能原因：**

1. **网络问题**：容器或主机无法访问 OKX 服务器
   ```bash
   # 测试网络连通性
   curl -I https://www.okx.com
   ping ws.okx.com
   ```

2. **防火墙**：端口 8443 被阻止
   ```bash
   # 测试端口连通性
   telnet ws.okx.com 8443
   # 或
   nc -zv ws.okx.com 8443
   ```

3. **OpenSSL 版本不兼容**
   ```bash
   # 查看版本
   openssl version
   # 推荐：OpenSSL 1.1.1 或更高
   ```

4. **代理设置**：如果在公司网络环境，可能需要配置代理

5. **DNS 问题**：无法解析域名
   ```bash
   nslookup ws.okx.com
   ```

### 在正常网络环境中测试

建议在以下环境中测试：

- ✅ 个人电脑（非受限网络）
- ✅ 云服务器（AWS, 阿里云等）
- ✅ 有公网访问的 VPS

避免在以下环境测试：

- ❌ 公司内网（可能有防火墙限制）
- ❌ 受限的容器环境
- ❌ 无公网访问的开发环境

## 修改和扩展

### 订阅其他交易对

修改 `on_open()` 函数中的订阅消息：

```cpp
std::string subscribe_msg = R"({"op":"subscribe","args":[{"channel":"bbo-tbt","instId":"ETH-USDT"}]})";
```

### 订阅其他频道

```cpp
// Ticker 数据
std::string subscribe_msg = R"({"op":"subscribe","args":[{"channel":"tickers","instId":"BTC-USDT"}]})";

// 深度数据
std::string subscribe_msg = R"({"op":"subscribe","args":[{"channel":"books","instId":"BTC-USDT"}]})";

// 成交数据
std::string subscribe_msg = R"({"op":"subscribe","args":[{"channel":"trades","instId":"BTC-USDT"}]})";
```

### 启用详细日志

将日志级别改为显示全部：

```cpp
// 在 main() 函数中，注释掉这两行：
// g_client.clear_access_channels(websocketpp::log::alevel::all);
// g_client.clear_error_channels(websocketpp::log::elevel::all);
```

## 下一步

如果此测试成功：
- ✅ WebSocket 连接正常
- ✅ 可以继续使用项目中的完整 WebSocket 实现

如果此测试失败：
- ❌ 需要先解决网络或环境问题
- ❌ 检查防火墙、代理、DNS 设置

## 相关文档

- [OKX WebSocket API 文档](https://www.okx.com/docs-v5/en/#websocket-api)
- [WebSocket TLS 修复](../doc/websocket_tls_fix_20251204.md)

## 测试检查清单

- [ ] 程序编译成功
- [ ] 能够初始化 TLS 上下文
- [ ] 能够连接到服务器
- [ ] 能够发送订阅消息
- [ ] 能够接收订阅确认
- [ ] 能够接收行情数据
- [ ] 程序正常退出
