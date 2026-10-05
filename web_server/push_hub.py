"""推送中枢（push_hub）—— 上游采样 + 变更检测 + 限频合并 + 连接扇出。

设计契约见 doc_ai/plan/202610/20261003_1610_web行情推送改造方案.md 的 §1/§2（阶段 1）。

职责四件（方案 §2.4）：
1. 维护全局订阅表 topic -> refcount（哪个连接在订哪个 topic），计数归零立即停止拉取；
2. 按 topic 类型以固定周期从上游取数：depth 拉引擎 HttpGateway（:46012）HTTP 接口，
   strategies 查 MySQL `strat_info`（复用 strategy_service 的既有查询）；
3. 变更检测（内容指纹）+ 限频合并（下限内攒批、上限保底、订阅即首推）；
4. 把事件扇出到各连接的 queue（快照类丢最旧帧、增量类队列满则断连）。

线程模型：一个上游采样线程（daemon，进程内唯一）+ 每个 SSE 连接一个 Flask 处理线程。
跨线程通信全部走连接级 `queue.Queue`；共享状态用一把 `RLock` 保护，**网络/DB IO 一律在锁外做**，
避免采样线程阻塞在前端请求线程的锁上。

为什么是"采样"而不是"推送"：引擎 HttpGateway 是请求-响应式，MySQL 不会通知 Flask，
上游没有推送通道（方案 §2.4/§3 有实测数据与阶段 3 的改造方向）。

上游陷阱（方案 §2.5 实测）：**禁止对 :46012 复用 keep-alive 连接**——复用连接 + 引擎同步跳
= 47–52 ms/次，而每次新建连接（gtrade_client.forward 走的 requests.request，无 Session）
实测 5–6 ms/次。本模块因此固定使用 gtrade_client.forward，不引入 Session。
"""
import json
import logging
import os
import queue
import threading
import time
import uuid

from config import config
import gtrade_client
from strategy_service import get_strategy_service

logger = logging.getLogger(__name__)

# topic 类型：`<type>[:<key>...]`
TOPIC_DEPTH = 'depth'            # depth:<market>:<inst_id>
TOPIC_STRATEGIES = 'strategies'  # strategies（全表，无 key）
# 引擎直推类 topic（无上游轮询：数据由 /ws/engine 的事件帧当场扇出；rev4 §5）
TOPIC_ORDER = 'order'
TOPIC_TRADE = 'trade'
TOPIC_POSITION = 'position'
TOPIC_BALANCE = 'balance'
KNOWN_TYPES = (TOPIC_DEPTH, TOPIC_STRATEGIES, TOPIC_ORDER, TOPIC_TRADE, TOPIC_POSITION, TOPIC_BALANCE)
# 需要上游轮询的 topic 类型（其余为引擎直推）
POLLED_TYPES = (TOPIC_DEPTH, TOPIC_STRATEGIES)
EVENT_ONLY_TYPES = (TOPIC_ORDER, TOPIC_TRADE, TOPIC_POSITION, TOPIC_BALANCE)

# 通道（rev4 §1）：quote 只装盘口快照；trade 装委托/成交/持仓/资金/策略指标；
# mixed 是过渡期默认（不校验 topic 归属），前端拆成两条流后应显式声明。
CHANNEL_QUOTE = 'quote'
CHANNEL_TRADE = 'trade'
CHANNEL_MIXED = 'mixed'
CHANNEL_TOPICS = {
    CHANNEL_QUOTE: (TOPIC_DEPTH,),
    CHANNEL_TRADE: (TOPIC_STRATEGIES, TOPIC_ORDER, TOPIC_TRADE, TOPIC_POSITION, TOPIC_BALANCE),
}

# 事件名 = topic 类型；EVENT_READY 为建流首帧，EVENT_UPSTREAM_ERROR 为上游失败事件
# （连接不断，按周期重试）。上游失败事件**不能叫 `error`**：EventSource 自己就用 `error`
# 派发连接异常，同名会让前端无法区分"连接断了"和"上游拉取失败"（见 push_stream 模块说明）。
EVENT_READY = 'ready'
EVENT_UPSTREAM_ERROR = 'upstream_error'
EVENT_PING = 'ping'        # 应用层心跳（普通事件帧；SSE 注释行前端根本收不到）
EVENT_CLOSING = 'closing'  # 主动断开前的通知（overflow / slow_consumer / …）
EVENT_RESULT = 'result'    # 通道内补查响应（req_id 配对）


def _queue_size_for(channel, cfg):
    """按通道取推送队列容量：quote 更浅（快照只有最新一帧有用）。"""
    if channel == CHANNEL_QUOTE:
        return cfg.get('queue_size_quote', 2)
    if channel == CHANNEL_TRADE:
        return cfg.get('queue_size_trade', cfg.get('queue_size', 8))
    return cfg.get('queue_size', 8)


def topic_matches_channel(ttype, channel):
    """topic 类型是否属于该通道（mixed 不做限制）。"""
    allowed = CHANNEL_TOPICS.get(channel)
    return True if allowed is None else ttype in allowed

# 上游错误码（方案 §2.2/§2.9）
ERR_NOT_SUBSCRIBED = 'not_subscribed'
ERR_ENGINE_UNAVAILABLE = 'engine_unavailable'
ERR_DB_UNAVAILABLE = 'db_unavailable'


def now_ms():
    """单调毫秒时钟。限频/退避全部基于单调钟，避免系统时间跳变导致限频失效。"""
    return time.monotonic() * 1000.0


def parse_topic(raw):
    """解析 topic 字符串 -> (type, keys) 元组；非法返回 None。

    - `depth:<market>:<inst_id>`（两段 key 都非空且不含 ':'）
    - `strategies` / `order` / `trade` / `position` / `balance`（无 key）
    """
    if not isinstance(raw, str):
        return None
    parts = raw.split(':')
    ttype = parts[0]
    keys = parts[1:]
    if ttype == TOPIC_DEPTH:
        if len(keys) != 2 or not all(keys):
            return None
        return ttype, tuple(keys)
    if ttype in (TOPIC_STRATEGIES, *EVENT_ONLY_TYPES):
        return (ttype, ()) if not keys else None
    return None


def depth_topic(market, inst_id):
    """构造 depth topic（前端与服务端共用同一拼法）。"""
    return f"{TOPIC_DEPTH}:{market}:{inst_id}"


def _hash_json(value):
    """对 JSON 值取稳定指纹（dict 按 key 排序；非 JSON 值退化为 repr）。

    用于 strategies 的行级变更检测：`update_time` + `status` + indicator/param 指纹。
    指标 JSON 随策略规模增长，直接比较字符串也可以，但指纹更省内存、比较更快。
    """
    try:
        return hash(json.dumps(value, sort_keys=True, ensure_ascii=False, default=str))
    except (TypeError, ValueError):
        return hash(repr(value))


class PushConnection:
    """一条 SSE 连接的服务端侧状态。

    连接由 push_stream 的生成器创建，生成器退出（客户端断开 / 队列溢出断连）时
    必须调用 `hub.close_connection()`，否则订阅计数不会归零、上游会一直采样。
    """

    def __init__(self, stream_id, username, cfg, channel=CHANNEL_MIXED):
        self.stream_id = stream_id
        self.username = username
        self.channel = channel          # quote | trade | mixed（mixed = 过渡期：不校验 topic）
        # 队列容量**刻意很小**：SSE 的消费者是同进程的生成器，正常一取一空，
        # 队列满即说明客户端读得慢（TCP 背压）。深队列会让客户端在不知情的情况下
        # 消化几分钟前的行情，且让看门狗失效（rev4 §5）——故溢出统一走 closing + 断连。
        default_size = _queue_size_for(channel, cfg)
        self.queue = queue.Queue(maxsize=max(1, default_size))
        self.topics = set()
        self.overflowed = False   # 队列满 -> 主动断连（重连后由客户端补查/重订阅恢复）
        self.closing_reason = None  # 主动断开时下发的 closing 原因
        self.dropped = 0          # 历史字段：统一断连策略后不再丢帧，保留以兼容日志/测试
        self.seq = 0              # 连接内事件序号（单调递增，允许因丢帧而跳号）
        self.closed = False

    def next_seq(self):
        """取下一个连接内序号（由 SSE 生成器在写帧时调用）。"""
        self.seq += 1
        return self.seq

    def enqueue(self, event, droppable=True):
        """把事件放入本连接队列。

        统一策略（rev4 §5）：**队列满 = 客户端已经不健康** ⇒ 标记溢出，由事件流生成器
        下发 `closing{reason}` 后断开，重连后由客户端重订阅/补查恢复。不再"丢最旧帧"——
        静默丢帧会让客户端在不知情的情况下消费过期数据，而且深队列会让看门狗失效。

        :param event: 事件字典（含 event/data 两键，内容只读，可多连接共享）
        :param droppable: 历史参数（快照类可丢）；统一断连策略下不再区分，保留以兼容调用方
        :return: True 正常入队；False 溢出（本连接应被断开）
        """
        del droppable  # 统一策略后不再按"可否丢弃"分流
        try:
            self.queue.put_nowait(event)
            return True
        except queue.Full:
            self.overflowed = True
            return False


class TopicState:
    """单个 topic 的服务端状态（订阅集 + 上游快照 + 限频账本）。"""

    __slots__ = (
        'topic', 'ttype', 'keys', 'subscribers', 'last_push_ms', 'last_poll_ms', 'next_poll_ms',
        'last_upstream_ms', 'backoff_ms', 'snapshot', 'fingerprint', 'pushed_fingerprint',
        'pending', 'force_push', 'version', 'last_error_code',
    )

    def __init__(self, topic, ttype, keys):
        self.topic = topic
        self.ttype = ttype
        self.keys = keys
        self.subscribers = set()       # PushConnection 集合（refcount = len）
        self.last_push_ms = 0.0        # 上次真正推送的时间（单调 ms）
        self.last_poll_ms = 0.0        # 上次上游采样时间
        self.next_poll_ms = 0.0        # 下次应采样时间（退避后可能更晚）
        self.last_upstream_ms = 0.0    # 上游最后一次成功时间（供 stale_ms）
        self.backoff_ms = 0.0          # 当前失败退避间隔（0=正常节奏）
        self.snapshot = None           # 上游最新数据（depth 快照 / strategies 行列表）
        self.fingerprint = None        # 上游最新指纹（变化检测用）
        self.pushed_fingerprint = {}   # 上次**已推送**的行指纹（strategies 增量 diff 基准）
        self.pending = False           # 有未推送的变化（限频窗内合并中）
        self.force_push = False        # 订阅后首推（不受下限约束，reason=initial）
        self.version = 0               # 上游版本号（策略表变更批次；depth 阶段 1 恒 0）
        self.last_error_code = None    # 上次已推送的错误码（只推状态变化）


class PushHub:
    """推送中枢单例：订阅表 + 采样线程 + 限频合并 + 扇出。"""

    def __init__(self, cfg=None, strategy_service=None, engine_link=None):
        self.cfg = dict(cfg or {})
        self._lock = threading.RLock()
        self._topics = {}                 # topic -> TopicState
        self._conns = {}                  # stream_id -> PushConnection
        self._thread = None
        self._stop = threading.Event()
        self._strategy_service = strategy_service  # 允许测试注入 stub
        self._engine_link = engine_link            # 允许测试注入 stub（引擎侧推送链路）
        self._tick_s = max(0.01, int(self.cfg.get('hub_tick_ms', 50)) / 1000.0)
        self._started = False

    # ── 配置便捷读取（单位换算与缺省值集中在此） ──────────────────────────────
    def _c(self, key, default):
        value = self.cfg.get(key, default)
        try:
            return int(value)
        except (TypeError, ValueError):
            return default

    @property
    def min_push_ms(self):
        return {
            TOPIC_DEPTH: self._c('depth_min_push_ms', 500),
            TOPIC_STRATEGIES: self._c('strategies_min_push_ms', 1000),
        }

    @property
    def max_push_ms(self):
        return self._c('max_push_ms', 60000)

    @property
    def max_topics_per_conn(self):
        return self._c('max_topics_per_conn', 8)

    # ── 生命周期 ────────────────────────────────────────────────────────────
    def start(self):
        """启动采样线程（幂等）。"""
        with self._lock:
            if self._started:
                return
            self._started = True
            self._stop.clear()
            self._thread = threading.Thread(target=self._sampler_loop, name='push_hub_sampler', daemon=True)
            self._thread.start()
            # 引擎直推：注册监听器（链路未启用/未连接时为空操作）
            link = None
            try:
                link = self._engine()
            except Exception as e:  # pragma: no cover
                logger.debug('引擎链路不可用，跳过直推注册: %s', e)
            if link is not None:
                link.add_listener(self._on_engine_frame)
            logger.info("推送中枢已启动: tick=%dms depth_poll=%dms db_poll=%dms",
                        self._tick_s * 1000, self._c('depth_poll_ms', 250), self._c('db_poll_ms', 1000))

    def stop(self):
        """停止采样线程并标记所有连接关闭（进程退出/测试用）。"""
        with self._lock:
            self._started = False
            self._stop.set()
            conns = list(self._conns.values())
            link = self._engine_link
        if link is not None and link is not _ENGINE_LINK_DISABLED:
            link.remove_listener(self._on_engine_frame)
        for conn in conns:
            conn.closed = True

    def _engine(self):
        """引擎侧推送链路（rev4 §5：实时数据优先来自引擎，连不上则回落轮询）。

        惰性获取：ws 入口未启用时 import 失败也只记一次 debug，不影响既有轮询路径。
        """
        if self._engine_link is None:
            if not self._c_bool('upstream_engine_first', True):
                return None
            try:
                from ws_engine import get_engine_link
                self._engine_link = get_engine_link()
            except Exception as e:  # pragma: no cover - 仅在 ws 未启用时发生
                logger.debug("引擎侧链路不可用，回落轮询：%s", e)
                self._engine_link = _ENGINE_LINK_DISABLED
        return None if self._engine_link is _ENGINE_LINK_DISABLED else self._engine_link

    def _c_bool(self, key, default):
        value = self.cfg.get(key, default)
        if isinstance(value, str):
            return value.strip().lower() in ('1', 'true', 'yes', 'on')
        return bool(value)

    def _strategy_svc(self):
        if self._strategy_service is None:
            self._strategy_service = get_strategy_service()
        return self._strategy_service

    # ── 连接与订阅管理（由 Flask 请求线程调用） ──────────────────────────────
    def open_connection(self, username, topics, channel=CHANNEL_MIXED):
        """建立一条流：校验 topic 与通道归属、登记订阅、下发 ready 并安排首推。

        :param channel: quote | trade | mixed（mixed = 过渡期，不校验归属）
        :return: (conn, error)，error 为 None 表示成功；否则为 (status, message) 元组
        """
        if channel not in (CHANNEL_QUOTE, CHANNEL_TRADE, CHANNEL_MIXED):
            return None, (400, f'非法 channel: {channel}')
        parsed = []
        for raw in topics:
            item = parse_topic(raw)
            if item is None:
                return None, (400, f'非法 topic: {raw}')
            if not topic_matches_channel(item[0], channel):
                return None, (400, f'topic {raw} 不属于通道 {channel}')
            parsed.append((raw, item))
        if len(parsed) > self.max_topics_per_conn:
            return None, (400, f'topic 数量超限（上限 {self.max_topics_per_conn}）')

        conn = PushConnection(uuid.uuid4().hex, username, self.cfg, channel)
        with self._lock:
            self._conns[conn.stream_id] = conn
            for raw, (ttype, keys) in parsed:
                self._attach(conn, raw, ttype, keys)
        logger.info("SSE 流建立 stream_id=%s user=%s topics=%s", conn.stream_id, username, sorted(conn.topics))
        conn.enqueue({
            'event': EVENT_READY,
            'data': {'stream_id': conn.stream_id, 'channel': conn.channel, 'topics': sorted(conn.topics)},
        }, droppable=True)
        return conn, None

    def close_connection(self, conn):
        """关闭一条流：释放其全部订阅（refcount 减一），归零即停止上游采样。"""
        if conn is None:
            return
        with self._lock:
            if conn.closed:
                return
            conn.closed = True
            self._conns.pop(conn.stream_id, None)
            for topic in list(conn.topics):
                state = self._topics.get(topic)
                if state is None:
                    continue
                state.subscribers.discard(conn)
                if not state.subscribers:
                    # 无人订阅 -> 立即摘除 topic，上游零负载（方案 §2.4）
                    self._topics.pop(topic, None)
            conn.topics.clear()
        logger.info("SSE 流关闭 stream_id=%s 丢弃快照帧=%d", conn.stream_id, conn.dropped)

    def get_connection(self, stream_id):
        """按 stream_id 取连接（控制面 subscribe/unsubscribe 用）。"""
        with self._lock:
            return self._conns.get(stream_id)

    def subscribe(self, conn, topics):
        """增量订阅（原子：任一条非法或超限则整批不生效）。

        :return: (ok, status, payload)
        """
        parsed = []
        for raw in topics:
            item = parse_topic(raw)
            if item is None:
                return False, 400, {'success': False, 'message': f'非法 topic: {raw}'}
            if not topic_matches_channel(item[0], conn.channel):
                return False, 400, {'success': False, 'message': f'topic {raw} 不属于通道 {conn.channel}'}
            parsed.append((raw, item))
        with self._lock:
            if conn.closed:
                return False, 404, {'success': False, 'message': 'stream_id 已失效'}
            fresh = [(raw, item) for raw, item in parsed if raw not in conn.topics]
            if len(conn.topics) + len(fresh) > self.max_topics_per_conn:
                return False, 400, {
                    'success': False,
                    'message': f'topic 数量超限（上限 {self.max_topics_per_conn}）',
                }
            for raw, (ttype, keys) in fresh:
                self._attach(conn, raw, ttype, keys)
            added = [raw for raw, _ in fresh]
        if added:
            logger.info("SSE 订阅 stream_id=%s added=%s total=%d", conn.stream_id, added, len(conn.topics))
        return True, 200, {'success': True, 'added': added, 'topics': sorted(conn.topics)}

    def unsubscribe(self, conn, topics):
        """增量退订（幂等：未订阅的 topic 静默忽略）。"""
        removed = []
        with self._lock:
            for raw in topics:
                if raw not in conn.topics:
                    continue
                conn.topics.discard(raw)
                state = self._topics.get(raw)
                if state is not None:
                    state.subscribers.discard(conn)
                    if not state.subscribers:
                        self._topics.pop(raw, None)
                removed.append(raw)
        if removed:
            logger.info("SSE 退订 stream_id=%s removed=%s total=%d", conn.stream_id, removed, len(conn.topics))
        return {'success': True, 'removed': removed, 'topics': sorted(conn.topics)}

    def _attach(self, conn, topic, ttype, keys):
        """把连接挂到 topic 上（调用方须持有锁）。

        - 轮询类（depth/strategies）：建 TopicState 并安排首推（首订立即采样）；
        - **引擎直推类**（order/trade/position/balance）：不建状态、不采样，
          数据由 `publish_engine_frame()` 在收到 /ws/engine 事件帧时当场扇出。
        """
        conn.topics.add(topic)
        if ttype in EVENT_ONLY_TYPES:
            return
        state = self._topics.get(topic)
        if state is None:
            state = TopicState(topic, ttype, keys)
            state.next_poll_ms = 0.0       # 首订立即采样（不等下一个采样周期）
            self._topics[topic] = state
        # 首推不受下限约束（reason=initial）；已有订阅者时给新连接补一次当前快照
        state.force_push = True
        state.subscribers.add(conn)

    # ── 引擎直推帧的扇出（由 EngineLink 在 asyncio 线程回调）──────────────────
    def publish_engine_frame(self, frame):
        """把 /ws/engine 的 event/snapshot 帧扇出给订阅了对应 topic 的浏览器连接。

        事件名 = topic 类型（order / trade / position / balance / strategies / depth），
        帧内带 `topic`/`ts`/`cursor`（游标用于客户端实体守卫）与 `data`。
        """
        if not isinstance(frame, dict):
            return
        ftype = frame.get('type')
        topic_name = frame.get('topic') or ''
        if ftype == 'event':
            event_name = topic_name
            payload = {
                'topic': topic_name,
                'ts': frame.get('ts'),
                'cursor': frame.get('cursor'),
                'status_id': frame.get('status_id'),
                'data': frame.get('data') or {},
            }
            send_topic = topic_name
        elif ftype == 'snapshot':
            if topic_name == 'strategy':
                # 策略指标：**复用既有 strategies 契约**（full/changed/removed），前端无需区分来源。
                # 引擎每次指标变化推一条快照 ⇒ 当作一次"单行增量"；删除行仍由轮询差集检出。
                event_name = TOPIC_STRATEGIES
                send_topic = TOPIC_STRATEGIES
                payload = {
                    'topic': send_topic,
                    'ts': frame.get('ts'),
                    'full': False,
                    'reason': 'engine',
                    'style': 'push',
                    'changed': [frame.get('data') or {}],
                    'removed': [],
                }
            else:
                event_name = topic_name
                send_topic = topic_name
                payload = {
                    'topic': send_topic,
                    'key': frame.get('key'),
                    'ts': frame.get('ts'),
                    'data': frame.get('data') or {},
                }
        else:
            return

        sent = 0
        with self._lock:
            for conn in list(self._conns.values()):
                if send_topic not in conn.topics:
                    continue
                if conn.enqueue({'event': event_name, 'data': payload}):
                    sent += 1
        if sent:
            logger.debug('引擎帧扇出 topic=%s -> %d 条连接', send_topic, sent)
        return sent

    def query_connection(self, conn, what, cursor=None, entnos=None, limit=None, req_id=None):
        """通道内补查（rev4 §4）：结果作为 `result` 事件**沿该连接的队列**回流。

        顺序说明：结果在 hub 锁内入队 ⇒ "结果帧之前入队的事件必然更早推送"；由于数据读取
        发生在锁外（引擎查询可能耗时数毫秒），严格全序仍需客户端实体守卫（status_id/tdno）
        兜底——这也是 rev4 §3 把水位列为可选项、把守卫列为必需项的原因。
        :return: (ok, payload)
        """
        if what in ('orders', 'trades'):
            link = self._engine()
            if link is None:
                return False, {'error': 'engine_link_down'}
            rows, error = link.query(what, cursor=cursor, entnos=entnos, timeout=None)
            if error:
                return False, {'error': error}
        elif what == 'strategies':
            link = self._engine()
            rows = (link.get_strategies() if link is not None else None)
            if rows is None:
                return False, {'error': 'engine_link_down'}
        else:
            return False, {'error': f'unsupported what: {what}'}

        payload = {
            'req_id': req_id,
            'what': what,
            'count': len(rows),
            'has_more': bool(limit) and len(rows) >= int(limit),
            'ts': int(time.time() * 1000),
            'data': rows,
        }
        conn.enqueue({'event': EVENT_RESULT, 'data': payload})
        return True, payload

    def _on_engine_frame(self, frame):
        """EngineLink 监听器入口（asyncio 线程）——只做线程安全的扇出。"""
        self.publish_engine_frame(frame)

    def stats(self):
        """运行状态快照（诊断/测试用）。"""
        with self._lock:
            return {
                'started': self._started,
                'connections': len(self._conns),
                'topics': [
                    {
                        'topic': st.topic,
                        'refcount': len(st.subscribers),
                        'pending': st.pending,
                        'version': st.version,
                        'stale_ms': int(now_ms() - st.last_upstream_ms) if st.last_upstream_ms else -1,
                        'last_error': st.last_error_code,
                    }
                    for st in self._topics.values()
                ],
            }

    # ── 采样线程 ────────────────────────────────────────────────────────────
    def _sampler_loop(self):
        """采样主循环：每 tick 检查各 topic 是否到期，到期则拉上游并决定推送。"""
        while not self._stop.is_set():
            try:
                self._sampler_tick()
            except Exception as e:  # 单次异常不能打死采样线程
                logger.exception("推送中枢采样异常: %s", e)
            self._stop.wait(self._tick_s)

    def _sampler_tick(self):
        """一 tick：① 取到期的 topic；② 锁外拉上游；③ 回锁内应用结果并推送。"""
        now = now_ms()
        due = []
        with self._lock:
            for state in self._topics.values():
                if not state.subscribers:
                    continue
                if now >= state.next_poll_ms:
                    due.append((state.topic, state.ttype, state.keys))
        # 锁外做 IO（引擎 HTTP / MySQL），避免阻塞前端请求线程
        results = []
        for topic, ttype, keys in due:
            results.append((topic, self._poll_upstream(ttype, keys)))
        with self._lock:
            for topic, result in results:
                state = self._topics.get(topic)
                if state is None or not state.subscribers:
                    continue  # 采样期间被退订，丢弃结果
                self._apply_upstream(state, result)
            self._dispatch_due()

    def _poll_upstream(self, ttype, keys):
        """按 topic 类型拉一次上游，返回 ('ok', payload, fingerprint) 或 ('err', code, message)。"""
        if ttype == TOPIC_DEPTH:
            return self._poll_depth(keys[0], keys[1])
        if ttype == TOPIC_STRATEGIES:
            return self._poll_strategies()
        return 'err', ERR_ENGINE_UNAVAILABLE, f'未知 topic 类型: {ttype}'

    def _poll_depth(self, market, inst_id):
        """取盘口快照：**优先**用引擎推来的最新帧（引擎→web_server 推送链路），
        链路未就绪时回落到 HTTP 轮询引擎（每次新建连接，禁 keep-alive——见模块 docstring）。"""
        link = self._engine()
        if link is not None:
            fed = link.get_depth(market, inst_id)
            if fed is not None:
                return 'ok', fed, ('v', fed.get('timestamp', 0), tuple(tuple(x) for x in fed.get('asks', [])),
                                   tuple(tuple(x) for x in fed.get('bids', [])))
        try:
            body, status = gtrade_client.forward(
                'GET', '/api/trade/depth',
                params={'inst_id': inst_id, 'market': market},
                timeout=self._c('depth_timeout_s', 2),
            )
        except gtrade_client.EngineUnavailable as e:
            return 'err', ERR_ENGINE_UNAVAILABLE, str(e)
        if status == 404:
            msg = body.get('error') if isinstance(body, dict) else ''
            return 'err', ERR_NOT_SUBSCRIBED, msg or 'not_subscribed'
        if status != 200 or not isinstance(body, dict) or not body.get('success'):
            msg = ''
            if isinstance(body, dict):
                msg = body.get('error') or body.get('message') or ''
            return 'err', ERR_ENGINE_UNAVAILABLE, msg or f'引擎返回异常（HTTP {status}）'
        asks = body.get('asks') or []
        bids = body.get('bids') or []
        payload = {'timestamp': body.get('timestamp', 0), 'asks': asks, 'bids': bids}
        version = body.get('version')  # 阶段 2 引擎侧才有；阶段 1 缺失
        if version is not None:
            fingerprint = ('v', version)
        else:
            # 阶段 1：内容指纹（timestamp + 档位价格/量）。阶段 2 接上 version 后
            # 由 use_depth_version 开关切到版本比对（缺失即自动回退，无需改配置）
            fingerprint = (
                payload['timestamp'],
                tuple(tuple(x) for x in asks),
                tuple(tuple(x) for x in bids),
            )
        return 'ok', payload, fingerprint

    def _poll_strategies(self):
        """查一次 MySQL `strat_info`（复用 strategy_service 既有查询）。

        指纹 = 行级 (update_time, status, indicator 哈希, param 哈希)：指标/参数/状态任一变化
        都能被检出；行删除由快照差集检出（见 `_push` 的全量/增量分支）。
        """
        link = self._engine()
        rows = None
        if link is not None:
            fed = link.get_strategies()
            if fed:
                rows = fed
        if rows is None:
            try:
                rows = self._strategy_svc().get_all_strategies()
            except Exception as e:
                return 'err', ERR_DB_UNAVAILABLE, str(e)
        fingerprint = {}
        for row in rows:
            name = row.get('strat_name')
            if not name:
                continue
            fingerprint[name] = (
                row.get('update_time'),
                row.get('status'),
                _hash_json(row.get('indicator')),
                _hash_json(row.get('param')),
            )
        return 'ok', rows, fingerprint

    def _apply_upstream(self, state, result):
        """把一次上游结果落到 topic 状态上（调用方须持有锁）。"""
        now = now_ms()
        state.last_poll_ms = now
        if result[0] == 'err':
            _, code, message = result
            # 失败退避：基准周期 -> ×4 -> 上限（方案 §2.9：250ms -> 1s -> 5s）
            base = self._base_poll_ms(state.ttype)
            if state.backoff_ms <= 0:
                state.backoff_ms = base
            else:
                state.backoff_ms = min(state.backoff_ms * 4, self._c('retry_max_ms', 5000))
            state.next_poll_ms = now + state.backoff_ms
            if state.last_error_code != code:
                # 只在错误状态变化时推一次，避免每 250ms 刷屏；连接不断，其他 topic 照常
                state.last_error_code = code
                logger.warning("topic 上游失败 %s: %s %s", state.topic, code, message)
                self._fanout(state, {
                    'event': EVENT_UPSTREAM_ERROR,
                    'data': {'topic': state.topic, 'code': code, 'message': message},
                }, droppable=True)
            return
        _, payload, fingerprint = result
        state.backoff_ms = 0.0
        state.next_poll_ms = now + self._base_poll_ms(state.ttype)
        state.last_upstream_ms = now
        state.last_error_code = None
        if fingerprint == state.fingerprint:
            return  # 上游没变：不推、不计 pending（上限保底走 keepalive）
        state.snapshot = payload
        if state.ttype == TOPIC_STRATEGIES:
            state.version += 1  # 策略表变更批次（阶段 1 的 depth 无版本，恒 0）
        state.fingerprint = fingerprint
        state.pending = True

    def _base_poll_ms(self, ttype):
        """topic 类型的正常采样周期（无变化时的重采样节奏）。"""
        return self._c('depth_poll_ms', 250) if ttype == TOPIC_DEPTH else self._c('db_poll_ms', 1000)

    def _dispatch_due(self):
        """把到期的事件推给订阅者（调用方须持有锁）：先首推/变化，再上限保底。"""
        now = now_ms()
        limits = self.min_push_ms
        max_ms = self.max_push_ms
        for state in list(self._topics.values()):
            if not state.subscribers:
                continue
            min_ms = limits.get(state.ttype, 1000)
            if state.pending and (state.force_push or now - state.last_push_ms >= min_ms):
                self._push(state, 'initial' if state.force_push else 'change')
                continue
            if state.pending:
                continue  # 窗内合并：等下限到期再推（下一 tick 处理）
            if now - state.last_push_ms >= max_ms:
                # 上限保底：无变化也推一帧（depth 为当前快照，strategies 为全量）
                self._push(state, 'keepalive')

    def _push(self, state, reason):
        """组装并扇出一帧（调用方须持有锁）。"""
        if state.ttype == TOPIC_DEPTH and state.snapshot is None:
            # 还没有成功采样过（错误事件已推过），无数据可推；首推标记留给首次成功
            if reason != 'initial':
                state.pending = False
            return
        state.pending = False
        state.force_push = False
        state.last_push_ms = now_ms()
        stale_ms = int(state.last_push_ms - state.last_upstream_ms) if state.last_upstream_ms else -1
        if state.ttype == TOPIC_DEPTH:
            data = {
                'topic': state.topic,
                'version': state.version,   # 阶段 1 恒 0（阶段 2 引擎回带 version）
                'reason': reason,
                'stale_ms': stale_ms,
                'data': state.snapshot,
            }
            droppable = True
        else:
            full = reason in ('initial', 'keepalive')
            rows = state.snapshot or []
            if full:
                changed, removed = rows, [name for name in state.pushed_fingerprint if name not in state.fingerprint]
                state.pushed_fingerprint = dict(state.fingerprint or {})
            else:
                changed, removed = self._diff_strategies(state)
                if not changed and not removed:
                    return  # 合并后净变化为零（改了又改回来），不推
            data = {
                'topic': state.topic,
                'version': state.version,
                'full': full,
                'reason': reason,
                'stale_ms': stale_ms,
                'changed': changed,
                'removed': removed,
            }
            droppable = False
        self._fanout(state, {'event': state.ttype, 'data': data}, droppable=droppable)

    def _diff_strategies(self, state):
        """算增量：与上次**已推送**的行指纹比较（changed / removed）。

        基准必须是"上次真正推出去的内容"，不能拿"上次采样"作基准——限频窗内可能有多批
        变化被合并，用采样基准会漏掉中间批次。
        """
        rows = state.snapshot or []
        pushed = state.pushed_fingerprint
        fingerprint = state.fingerprint or {}
        changed = [row for row in rows
                   if pushed.get(row.get('strat_name')) != fingerprint.get(row.get('strat_name'))]
        removed = [name for name in pushed if name not in fingerprint]
        state.pushed_fingerprint = dict(fingerprint)
        return changed, removed

    def _fanout(self, state, event, droppable):
        """把事件投给该 topic 的所有连接；不可丢事件溢出即标记断连。

        event 只读且按引用共享：同一 topic 的盘口快照**上游只拉一次**，N 个连接拿到的是同一份
        对象（无逐连接拷贝）。但编码发生在各连接的生成器里（每帧要带各自的 seq），故 JSON
        序列化仍是 N 次——省下的是采样与拷贝，不是编码。
        """
        for conn in list(state.subscribers):
            if not conn.enqueue(event, droppable=droppable):
                logger.warning("SSE 队列溢出，断开连接 stream_id=%s（增量事件不允许丢弃）", conn.stream_id)


# 引擎链路获取失败的哨兵（避免每次采样都重复 import 失败）
_ENGINE_LINK_DISABLED = object()

_hub = None
_hub_lock = threading.Lock()


def get_hub():
    """取进程内唯一的中枢实例（首次调用时按当前环境配置构造）。"""
    global _hub
    with _hub_lock:
        if _hub is None:
            env = os.environ.get('FLASK_ENV', 'development')
            cfg = getattr(config[env](), 'WEB_PUSH', {}) or {}
            _hub = PushHub(cfg)
        return _hub


def reset_hub():
    """销毁单例（测试用）。"""
    global _hub
    with _hub_lock:
        if _hub is not None:
            _hub.stop()
        _hub = None
