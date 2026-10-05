"""SSE 推送通道的路由层：票据 + `/api/stream*` + 事件流生成器。

设计契约见 doc_ai/plan/202610/20261003_1610_web行情推送改造方案.md 的 §2（阶段 1）。

四条路由（外加一条前端配置下发）：
  POST /api/stream/ticket       JWT 换一次性握手票据（EventSource 不能带请求头，token 只能进 URL，
                                而 Werkzeug 访问日志会原样打印 query —— 见方案 §2.6）
  GET  /api/stream?topics=&ticket=  建立 SSE 流（URL 的 topic 为初始订阅集，首帧 retry + ready）
  POST /api/stream/subscribe    JWT，增量订阅（切标的/进页面）
  POST /api/stream/unsubscribe  JWT，退订（切走/离开页面）
  GET  /api/settings/push       JWT，下发前端所需参数（降级轮询间隔、看门狗阈值等）

与方案的一处**有意偏离**：上游失败事件名用 `upstream_error` 而非方案里的 `error`。
原因：EventSource 自身在网络断开/重连时就会派发 `error` 事件，服务端再发一个同名 `event: error`
会让前端无法区分"连接断了"和"上游拉取失败"，`onerror` 会被两种语义混用。
"""
import json
import logging
import os
import queue
import threading
import time
import uuid

from flask import Blueprint, Response, jsonify, request

from auth import token_required, verify_token
from config import config
from push_hub import (EVENT_CLOSING, EVENT_PING, get_hub, now_ms)

logger = logging.getLogger(__name__)

push_bp = Blueprint('push_stream', __name__)

# 一次性握手票据：{ticket: (过期单调 ms, username)}
_tickets = {}
_tickets_lock = threading.Lock()


def _cfg():
    """当前环境的 web_push 配置段（每次读取，便于测试注入）。"""
    env = os.environ.get('FLASK_ENV', 'development')
    return getattr(config[env](), 'WEB_PUSH', {}) or {}


def _cfg_int(key, default):
    try:
        return int(_cfg().get(key, default))
    except (TypeError, ValueError):
        return default


# ── 票据 ────────────────────────────────────────────────────────────────────
def _purge_tickets_locked():
    """清理过期票据（调用方须持有锁）。"""
    now = now_ms()
    for key in [k for k, (expire, _) in _tickets.items() if expire < now]:
        _tickets.pop(key, None)


def issue_ticket(username):
    """签发一次性握手票据。

    :return: (ticket, ttl_seconds)
    """
    ttl = _cfg_int('ticket_ttl_s', 30)
    ticket = uuid.uuid4().hex
    with _tickets_lock:
        _purge_tickets_locked()
        _tickets[ticket] = (now_ms() + ttl * 1000.0, username)
    return ticket, ttl


def consume_ticket(ticket):
    """消费票据（单次有效、用后即焚）；无效/过期返回 None。"""
    if not ticket:
        return None
    with _tickets_lock:
        item = _tickets.pop(ticket, None)
        _purge_tickets_locked()
    if item is None:
        return None
    expire, username = item
    return username if now_ms() <= expire else None


# ── SSE 帧编码 ──────────────────────────────────────────────────────────────
def encode_sse(event, data):
    """把一个事件编码成 SSE 帧（`event:` + 多行 `data:` + 空行结束）。

    值统一走 JSON；`default=str` 兜底（strat_info 若有新增时间列不会打断整条流）。
    """
    payload = json.dumps(data, ensure_ascii=False, default=str, separators=(',', ':'))
    lines = [f'event: {event}']
    for line in payload.split('\n'):
        lines.append(f'data: {line}')
    return '\n'.join(lines) + '\n\n'


def _event_stream(conn):
    """单条 SSE 连接的生成器：取队列 -> 写帧；空闲超过 ping 周期写**心跳事件帧**。

    契约（rev4 §2/§5）：
      * 每个数据帧带 `seq`（连接内序号）与 `ts`（服务端 epoch ms）——客户端据此自测滞后；
      * 心跳是普通事件帧 `event: ping`，**不是 SSE 注释行**：注释帧前端 EventSource 收不到，
        只能等业务帧，这正是现状看门狗被迫设 90s 的根因；
      * 队列满（客户端消费不过来）时下发 `event: closing` 再断开，让客户端区分"主动断开"
        与"网络掉线"，重连后按游标补查/重订阅恢复。

    客户端断开的感知时机：生成器只在**写**的时候才会发现对端已断（SSE 单向的固有上限），
    所以 ping 周期同时也是"空闲订阅的最坏释放延迟"。生成器退出时释放该连接的全部订阅。
    """
    hub = get_hub()
    ping_ms = _ping_ms_for(conn)
    last_write = now_ms()
    logger.info("SSE 开始推流 stream_id=%s channel=%s topics=%s",
                conn.stream_id, conn.channel, sorted(conn.topics))
    try:
        # 首帧重连间隔（EventSource 用；前端实际自己管理重连，这里只作兜底）
        yield 'retry: 3000\n\n'
        while True:
            if conn.overflowed:
                reason = conn.closing_reason or 'overflow'
                logger.warning("SSE 队列溢出，主动断开 stream_id=%s（reason=%s，队列容量=%d）",
                               conn.stream_id, reason, conn.queue.maxsize)
                # closing 直接写（队列已满，不能再入队）；写完即断开
                yield encode_sse(EVENT_CLOSING, {'reason': reason, 'ts': int(time.time() * 1000)})
                return
            try:
                event = conn.queue.get(timeout=0.5)
            except queue.Empty:
                event = None
            if event is not None:
                data = dict(event['data'])
                data['seq'] = conn.next_seq()
                data['ts'] = int(time.time() * 1000)   # 服务端时刻：客户端算自身滞后
                yield encode_sse(event['event'], data)
                last_write = now_ms()
                continue
            if now_ms() - last_write >= ping_ms:
                yield encode_sse(EVENT_PING, {'ts': int(time.time() * 1000)})
                last_write = now_ms()
    finally:
        hub.close_connection(conn)


def _ping_ms_for(conn):
    """按通道取心跳周期（rev4 §2：quote 10s / trade 5s）。"""
    from push_hub import CHANNEL_TRADE
    if conn.channel == CHANNEL_TRADE:
        return _cfg_int('ping_interval_trade_ms', 5000)
    if conn.channel == 'quote':
        return _cfg_int('ping_interval_quote_ms', 10000)
    return _cfg_int('ping_interval_ms', 20000)   # mixed：过渡期沿用旧值


def _channel_of(raw):
    """解析并校验 channel 参数（缺省 mixed = 过渡期不校验 topic 归属）。"""
    from push_hub import CHANNEL_MIXED, CHANNEL_QUOTE, CHANNEL_TRADE
    if not raw:
        return CHANNEL_MIXED
    return raw if raw in (CHANNEL_QUOTE, CHANNEL_TRADE, CHANNEL_MIXED) else None


# ── 路由 ────────────────────────────────────────────────────────────────────
@push_bp.route('/api/stream/ticket', methods=['POST'])
@token_required
def create_stream_ticket():
    """签发一次性握手票据（EventSource 无法自定义请求头，故 token 不能直接进 URL）。"""
    if not _cfg().get('enabled', True):
        return jsonify({'success': False, 'message': '推送通道已关闭'}), 503
    username = (getattr(request, 'current_user', None) or {}).get('username', '')
    ticket, ttl = issue_ticket(username)
    return jsonify({'success': True, 'ticket': ticket, 'expires_in': ttl})


@push_bp.route('/api/stream', methods=['GET'])
def open_stream():
    """建立 SSE 多路复用流。

    鉴权二选一：`?ticket=`（正常路径，一次性票据）或 `?token=`（仅本地调试，可用
    `web_push.allow_url_token: false` 关闭——Werkzeug 访问日志会把 query 原样落盘）。
    """
    if not _cfg().get('enabled', True):
        return jsonify({'success': False, 'message': '推送通道已关闭'}), 503

    username = consume_ticket(request.args.get('ticket', ''))
    if username is None and _cfg().get('allow_url_token', True):
        raw_token = request.args.get('token', '')
        payload = verify_token(raw_token) if raw_token else None
        if payload:
            logger.warning("SSE 使用 URL 明文 token 鉴权（仅本地调试可用，请改用 ticket）")
            username = payload.get('username', '')
    if username is None:
        return jsonify({'success': False, 'message': '票据无效或已过期'}), 401

    raw_topics = request.args.get('topics', '')
    topics = [t.strip() for t in raw_topics.split(',') if t.strip()]
    channel = _channel_of(request.args.get('channel', ''))
    if channel is None:
        return jsonify({'success': False, 'message': '非法 channel（只能是 quote/trade）'}), 400
    hub = get_hub()
    hub.start()
    conn, error = hub.open_connection(username, topics, channel)
    if error is not None:
        status, message = error
        return jsonify({'success': False, 'message': message}), status

    resp = Response(_event_stream(conn), mimetype='text/event-stream')
    resp.headers['Content-Type'] = 'text/event-stream; charset=utf-8'
    resp.headers['Cache-Control'] = 'no-cache'
    resp.headers['X-Accel-Buffering'] = 'no'  # 反代必须关缓冲，否则事件被攒批（方案 §6）
    return resp


def _authorized_conn():
    """校验控制面请求：JWT 已在装饰器校验，这里再确认 stream_id 归属同一用户。

    :return: (conn, error_response)
    """
    if not _cfg().get('enabled', True):
        return None, (jsonify({'success': False, 'message': '推送通道已关闭'}), 503)
    body = request.get_json(silent=True) or {}
    stream_id = body.get('stream_id') or ''
    topics = body.get('topics')
    if not stream_id:
        return None, (jsonify({'success': False, 'message': '缺少 stream_id'}), 400)
    if not isinstance(topics, list) or not topics:
        return None, (jsonify({'success': False, 'message': 'topics 必须为非空数组'}), 400)
    conn = get_hub().get_connection(stream_id)
    if conn is None:
        return None, (jsonify({'success': False, 'message': 'stream_id 不存在或已关闭'}), 404)
    username = (getattr(request, 'current_user', None) or {}).get('username', '')
    if conn.username and username and conn.username != username:
        return None, (jsonify({'success': False, 'message': 'stream_id 不属于当前用户'}), 403)
    return (conn, [str(t) for t in topics]), None


@push_bp.route('/api/stream/subscribe', methods=['POST'])
@token_required
def stream_subscribe():
    """增量订阅（切标的：unsubscribe 旧 + subscribe 新，**流不断**）。"""
    result, error = _authorized_conn()
    if error is not None:
        return error
    conn, topics = result
    ok, status, payload = get_hub().subscribe(conn, topics)
    return jsonify(payload), status


@push_bp.route('/api/stream/unsubscribe', methods=['POST'])
@token_required
def stream_unsubscribe():
    """退订（幂等）。计数归零后中枢立即停止该 topic 的上游采样。"""
    result, error = _authorized_conn()
    if error is not None:
        return error
    conn, topics = result
    return jsonify(get_hub().unsubscribe(conn, topics))


@push_bp.route('/api/stream/query', methods=['POST'])
@token_required
def stream_query():
    """通道内补查（rev4 §4）：结果沿**同一条流**回流（`event: result`，`req_id` 配对）。

    请求体：{stream_id, req_id, what: orders|trades|strategies, cursor?, entnos?, limit?}
    说明：补查放回通道内是为了让"补查结果与推送事件"共享同一条连接的 FIFO 顺序；
    真正防倒挂仍靠客户端实体守卫（委托 status_id / 成交 tdno）。
    """
    body = request.get_json(silent=True) or {}
    stream_id = body.get('stream_id') or ''
    what = body.get('what') or ''
    if not stream_id or not what:
        return jsonify({'success': False, 'message': '缺少 stream_id 或 what'}), 400
    conn = get_hub().get_connection(stream_id)
    if conn is None:
        return jsonify({'success': False, 'message': 'stream_id 不存在或已关闭'}), 404
    username = (getattr(request, 'current_user', None) or {}).get('username', '')
    if conn.username and username and conn.username != username:
        return jsonify({'success': False, 'message': 'stream_id 不属于当前用户'}), 403

    cursor = body.get('cursor')
    try:
        cursor = int(cursor) if cursor is not None else None
    except (TypeError, ValueError):
        return jsonify({'success': False, 'message': 'cursor 必须是整数'}), 400
    entnos = body.get('entnos')
    if entnos is not None and not isinstance(entnos, list):
        return jsonify({'success': False, 'message': 'entnos 必须是数组'}), 400
    limit = body.get('limit')
    try:
        limit = int(limit) if limit is not None else None
    except (TypeError, ValueError):
        limit = None

    ok, payload = get_hub().query_connection(conn, what, cursor=cursor, entnos=entnos,
                                             limit=limit, req_id=body.get('req_id'))
    if not ok:
        return jsonify({'success': False, 'message': payload.get('error', 'query failed')}), 503
    # 响应只表示"已受理并入队"，真正数据沿流返回（保证顺序）
    return jsonify({'success': True, 'data': {'req_id': payload['req_id'], 'count': payload['count']}})


@push_bp.route('/api/settings/push', methods=['GET'])
@token_required
def get_push_settings():
    """下发前端所需推送参数（避免前端硬编码；降级轮询节奏也由服务端给）。"""
    cfg = _cfg()
    return jsonify({
        'success': True,
        'data': {
            'enabled': bool(cfg.get('enabled', True)),
            'depth_poll_ms': _cfg_int('depth_poll_ms', 250),
            'depth_min_push_ms': _cfg_int('depth_min_push_ms', 500),
            'strategies_min_push_ms': _cfg_int('strategies_min_push_ms', 1000),
            'max_push_ms': _cfg_int('max_push_ms', 60000),
            'ping_interval_ms': _cfg_int('ping_interval_ms', 20000),
            'ping_interval_quote_ms': _cfg_int('ping_interval_quote_ms', 10000),
            'ping_interval_trade_ms': _cfg_int('ping_interval_trade_ms', 5000),
            'channels': ['quote', 'trade'],
            'degraded_after_ms': _cfg_int('degraded_after_ms', 10000),
            'watchdog_ms': _cfg_int('watchdog_ms', 90000),
            'fallback_depth_poll_ms': _cfg_int('fallback_depth_poll_ms', 1000),
            'fallback_strategies_poll_ms': _cfg_int('fallback_strategies_poll_ms', 10000),
        },
    })


@push_bp.route('/api/push/engine', methods=['GET'])
@token_required
def get_engine_link_status():
    """引擎侧链路诊断（水位/缓存计数/心跳与补查统计）。"""
    from ws_engine import get_engine_link
    return jsonify({'success': True, 'data': get_engine_link().status()})


@push_bp.route('/api/push/engine/query', methods=['GET'])
@token_required
def engine_link_query():
    """手动补查（调试用）：?what=orders|trades&cursor=…&entnos=a,b,c&limit=N。

    正式路径是各通道内的 query 帧；这条 HTTP 路由只为 curl 复现与排障保留。
    """
    from ws_engine import get_engine_link
    what = request.args.get('what', 'trades')
    if what not in ('orders', 'trades'):
        return jsonify({'success': False, 'message': 'what 只能是 orders/trades'}), 400
    cursor = request.args.get('cursor', type=int)
    raw_entnos = request.args.get('entnos', '')
    entnos = [t for t in (x.strip() for x in raw_entnos.split(',')) if t] or None
    rows, error = get_engine_link().query(what, cursor=cursor, entnos=entnos)
    if error:
        return jsonify({'success': False, 'message': error}), 503
    return jsonify({'success': True, 'data': {'count': len(rows), 'rows': rows}})


def register(app):
    """把推送路由挂到 Flask 应用、启动中枢采样线程与引擎侧 WS 入口。"""
    app.register_blueprint(push_bp)
    cfg = _cfg()
    if cfg.get('enabled', True):
        get_hub().start()
        # 引擎侧推送入口（引擎作 WS 客户端连入 /ws/engine）；起不来不影响其余功能
        if cfg.get('ws_enabled', True):
            try:
                from ws_engine import get_engine_link
                get_engine_link(cfg).start()
            except Exception as e:
                logger.error("引擎侧推送入口启动失败（引擎推送将不可用）：%s", e)
    logger.info("推送通道已注册：/api/stream*（enabled=%s, ws_port=%s）",
                cfg.get('enabled', True), cfg.get('ws_port'))
    return push_bp
