"""mock_exchange 协议层：把内核状态翻译成某家交易所的线上协议。

- `ws.py`：WS 会话与订阅表（订阅匹配、事件转发、心跳 `ping`→`pong`）；
- `http.py` / `admin.py` / `auth.py`：REST 路由装配、管理面、签名解析（后续任务）。

内核（`mockex/core/`）不认识任何交易所字段名，协议拼写只出现在本层与
`mockex/exchanges/<exchange>/codec.py`。
"""
