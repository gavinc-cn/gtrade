"""web_server 汇聚点的**引擎侧入口**：`ws://…/ws/engine`（asyncio + websockets）。

方案：doc_ai/plan/202610/20261004_1300_二通道推送与补查方案_rev4.md §2–§5。
引擎作 WS 客户端主动连入（`engine_push` 配置），本模块作服务端：

  hello{role,secret}  →  hello_ack            握手鉴权（密钥不走 URL，避免进访问日志）
  subscribe{topics}   →  ack{topics}          声明要哪些通道（trade / quote）
  event|snapshot      →  （下行）             引擎推来的委托/成交/持仓/资金
  ping{ts,rx}         ⇄  pong{ts,rx}          应用层心跳 + 已收帧数回执（对端据此估算积压）
  query{req_id,…}     →  result{req_id,…}     补查（断线重连后按游标补齐）

为什么**另起一个 asyncio 事件循环线程**：Flask 是 `threaded=True` 的多线程 WSGI，
`app.run` 无法做协议升级；websockets 是 asyncio 库，放独立线程后与 hub/Flask 之间
只保留两处交接：① 收到引擎帧→直接调 hub（线程安全，内部有锁）；
② 需要向引擎发帧→`loop.call_soon_threadsafe`。查询用 `threading.Event` 等结果，
避免把 asyncio Future 泄漏到 Flask 线程。

单进程假设不变（rev4 §8）：WS server 与 hub 必须同进程，多 worker 会让订阅与推送分裂。
"""
import asyncio
import itertools
import json
import logging
import threading
import time

import websockets

logger = logging.getLogger(__name__)

# 握手/心跳/看门狗默认值（可被 web_push 配置覆盖）
DEFAULT_WS_PORT = 46013
DEFAULT_ENGINE_PING_MS = 10000
DEFAULT_HELLO_TIMEOUT_S = 5.0
DEFAULT_QUERY_TIMEOUT_S = 5.0
DEFAULT_MAX_PAGES = 20          # 补查分页上限（防一次重连拉爆）
DEFAULT_PAGE_ROWS = 200

# 引擎可订阅的通道（与 C++ EventPublisher::PushChannel 对齐）
CHANNEL_TRADE = 'trade'
CHANNEL_QUOTE = 'quote'

# 委托终态：进入终态后不再需要"已知在途"刷新（仍会按 entno 游标增量查）
_TERMINAL_ORDER_STATUS = {'4', '6', '8', '9'}


def _now_ms():
    return int(time.monotonic() * 1000)


class _PendingQuery:
    """一次补查请求的等待槽（线程间）。"""

    __slots__ = ('event', 'result', 'error')

    def __init__(self):
        self.event = threading.Event()
        self.result = None
        self.error = None

    def wait(self, timeout):
        if not self.event.wait(timeout):
            return None, 'timeout'
        if self.error:
            return None, self.error
        return self.result, None


class EngineLink:
    """引擎侧链路：会话状态 + 线程安全桥 + 补查编排。

    线程安全边界：
      * `_conn` / `_pending` / `_stats` / `_state` 由 `_lock` 保护；
      * 对引擎的发送一律经 `_post_to_loop()` 投到 asyncio 循环线程；
      * 引擎帧在自己线程里解析后，再取锁更新状态（不阻塞循环）。
    """

    def __init__(self, cfg=None):
        self.cfg = dict(cfg or {})
        self._lock = threading.RLock()
        self._loop = None
        self._server = None
        self._thread = None
        self._conn = None
        self._connected = False
        self._hello_done = False
        self._subscribed = set()
        self._req_seq = itertools.count(1)
        self._pending = {}
        self._tasks = set()          # 后台补查任务（退出时统一取消）
        self._stopping = False
        self._listeners = []         # 引擎帧监听器（hub 注册；在 asyncio 线程回调）

        # 游标/水位（补查依据）：委托用 entno、成交用 tdno
        self._last_entno = 0
        self._last_tdno = 0
        # 已知在途委托（entno → status），重连时按 batch 刷新其终态
        self._open_orders = {}
        # 最新态缓存（供浏览器段复用；本版仅用于诊断/验证）
        self._orders = {}
        self._trades = {}
        self._positions = {}
        self._balances = {}
        self._strategies = {}
        self._depths = {}
        # 各计数（诊断）
        self._stats = {
            'connected': 0, 'hello_ok': 0, 'hello_reject': 0,
            'events': 0, 'snapshots': 0, 'results': 0,
            'queries_sent': 0, 'queries_ok': 0, 'queries_timeout': 0,
            'pings_in': 0, 'pongs_in': 0, 'rx_frames': 0,
            'last_frame_ms': 0, 'last_error': '',
        }

    # ── 配置便捷读取 ────────────────────────────────────────────────────────
    def _c(self, key, default):
        value = self.cfg.get(key, default)
        try:
            return int(value)
        except (TypeError, ValueError):
            return default

    @property
    def port(self):
        return self._c('ws_port', DEFAULT_WS_PORT)

    @property
    def secret(self):
        return str(self.cfg.get('engine_secret', '') or '')

    @property
    def ping_ms(self):
        return self._c('engine_ping_ms', DEFAULT_ENGINE_PING_MS)

    # ── 生命周期 ────────────────────────────────────────────────────────────
    def start(self):
        """启动 asyncio WS server（独立线程）；幂等。"""
        with self._lock:
            if self._thread is not None:
                return
            self._stopping = False
            self._thread = threading.Thread(target=self._run_loop, name='ws_engine_loop', daemon=True)
            self._thread.start()
        # 等 server 真正 listen（避免调用方刚 start 就连）
        for _ in range(200):
            with self._lock:
                if self._server is not None:
                    break
            time.sleep(0.01)

    def stop(self):
        """停止 WS server（进程退出/测试用）。"""
        self._stopping = True
        loop = self._loop
        if loop is not None:
            # 优雅停机：取消循环里所有任务（会话/心跳/补查）并等它们真正结束，再停循环。
            # 直接 loop.stop() 会让未收敛的任务在解释器退出时报 "Task was destroyed but it is pending"。
            async def _shutdown():
                current = asyncio.current_task()
                tasks = [t for t in asyncio.all_tasks() if t is not current]
                for task in tasks:
                    task.cancel()
                if tasks:
                    await asyncio.gather(*tasks, return_exceptions=True)
                with self._lock:
                    self._tasks.clear()
                loop.stop()

            try:
                asyncio.run_coroutine_threadsafe(_shutdown(), loop)
            except RuntimeError:
                pass
        if self._thread is not None:
            self._thread.join(timeout=3.0)
        with self._lock:
            self._thread = None
            self._server = None
            self._loop = None
            self._connected = False

    def _run_loop(self):
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        with self._lock:
            self._loop = loop
        try:
            loop.run_until_complete(self._serve())
            loop.run_forever()
        except Exception as e:  # 单次异常不能打死整个进程
            logger.exception('WS engine server 退出: %s', e)
        finally:
            try:
                loop.close()
            except Exception:
                pass

    async def _serve(self):
        try:
            self._server = await websockets.serve(
                self._handle, '127.0.0.1', self.port,
                ping_interval=None,      # 应用层心跳由本模块自己发（协议级 pong JS 侧看不到）
                ping_timeout=None,
                max_size=4 * 1024 * 1024,
            )
            logger.info('WS 引擎入口已监听 ws://127.0.0.1:%d/ws/engine', self.port)
        except Exception as e:
            logger.error('WS 引擎入口监听失败（端口 %d）：%s', self.port, e)

    def _post_to_loop(self, coro_factory):
        """把协程投到 asyncio 线程执行（Flask/其他线程调用）。"""
        loop = self._loop
        if loop is None or self._stopping:
            return False
        try:
            loop.call_soon_threadsafe(lambda: asyncio.ensure_future(coro_factory()))
            return True
        except RuntimeError:
            return False

    # ── 会话处理（asyncio 线程）─────────────────────────────────────────────
    async def _handle(self, ws):
        # websockets>=14 的 handler 只收一个参数；路径从 ws.request.path 取
        # 只接受 /ws/engine（浏览器段是 /ws/client，下一轮接入）
        request = getattr(ws, 'request', None)
        path = getattr(request, 'path', None)
        if path not in (None, '/ws/engine', '/ws/engine/'):
            await self._send_raw(ws, {'type': 'closing', 'reason': 'unknown_path'})
            await ws.close()
            return
        peer = getattr(ws, 'remote_address', None)
        logger.info('引擎 WS 连接建立: %s', peer)
        with self._lock:
            self._stats['connected'] += 1
        try:
            await self._handshake(ws)
            with self._lock:
                self._conn = ws
                self._connected = True
                self._hello_done = True
            await self._send_raw(ws, {'type': 'hello_ack', 'protocol': 1, 'ts': _now_ms()})
            # 声明订阅（服务端决定要什么；本版先要 trade）
            await self._send_subscribe(ws, [CHANNEL_TRADE, CHANNEL_QUOTE])
            # 重连即补查（rev4 §4）：先补最后一段增量，再刷已知在途委托
            self._spawn(self._recover_after_reconnect())
            await self._pump(ws)
        except Exception as e:
            logger.warning('引擎 WS 会话异常: %s', e)
            with self._lock:
                self._stats['last_error'] = str(e)
        finally:
            with self._lock:
                if self._conn is ws:
                    self._conn = None
                self._connected = False
                self._hello_done = False
                self._subscribed = set()
            logger.info('引擎 WS 连接关闭: %s', peer)

    def _spawn(self, coro):
        """登记一个后台任务（退出时随 stop() 一起取消）。"""
        task = asyncio.ensure_future(coro)
        with self._lock:
            self._tasks.add(task)
        task.add_done_callback(lambda t: self._tasks.discard(t))
        return task

    async def _handshake(self, ws):
        """首帧必须是 hello；密钥不符即 closing + 断开（失败要显式，不能静默）。"""
        timeout = float(self.cfg.get('engine_hello_timeout_s', DEFAULT_HELLO_TIMEOUT_S))
        try:
            raw = await asyncio.wait_for(ws.recv(), timeout=timeout)
        except asyncio.TimeoutError:
            with self._lock:
                self._stats['hello_reject'] += 1
            await self._send_raw(ws, {'type': 'closing', 'reason': 'hello_timeout'})
            await ws.close()
            raise RuntimeError('hello timeout')
        try:
            frame = json.loads(raw)
        except (TypeError, ValueError):
            frame = None
        if not isinstance(frame, dict) or frame.get('type') != 'hello':
            with self._lock:
                self._stats['hello_reject'] += 1
            await self._send_raw(ws, {'type': 'closing', 'reason': 'hello_required'})
            await ws.close()
            raise RuntimeError('hello required')

        expected = self.secret
        got = str(frame.get('secret', '') or '')
        if expected and got != expected:
            with self._lock:
                self._stats['hello_reject'] += 1
            logger.warning('引擎握手密钥不符，拒绝连接（peer=%s）', getattr(ws, 'remote_address', None))
            await self._send_raw(ws, {'type': 'closing', 'reason': 'auth'})
            await ws.close()
            raise RuntimeError('auth failed')
        if not expected:
            logger.warning('engine_secret 未配置：引擎入口按"仅本地回环"策略放行，生产请务必配置密钥')
        with self._lock:
            self._stats['hello_ok'] += 1

    async def _pump(self, ws):
        """主循环：收帧 → 分发；同时按 ping_ms 发应用层心跳。"""
        ping_task = asyncio.ensure_future(self._heartbeat(ws))
        try:
            async for raw in ws:
                await self._on_frame(ws, raw)
        finally:
            ping_task.cancel()

    async def _heartbeat(self, ws):
        interval = max(1.0, self.ping_ms / 1000.0)
        while True:
            await asyncio.sleep(interval)
            with self._lock:
                rx = self._stats['rx_frames']
            await self._send_raw(ws, {'type': 'ping', 'ts': _now_ms(), 'rx': rx})

    async def _on_frame(self, ws, raw):
        try:
            frame = json.loads(raw)
        except (TypeError, ValueError):
            logger.warning('引擎帧不是 JSON，忽略：%s', str(raw)[:200])
            return
        if not isinstance(frame, dict):
            return
        ftype = frame.get('type', '')
        with self._lock:
            self._stats['rx_frames'] += 1
            self._stats['last_frame_ms'] = _now_ms()

        if ftype == 'ping':
            with self._lock:
                self._stats['pings_in'] += 1
                rx = self._stats['rx_frames']
            await self._send_raw(ws, {'type': 'pong', 'ts': _now_ms(), 'rx': rx})
        elif ftype == 'pong':
            with self._lock:
                self._stats['pongs_in'] += 1
        elif ftype == 'ack':
            with self._lock:
                self._subscribed = set(frame.get('topics') or [])
            logger.info('引擎确认订阅: %s', sorted(self._subscribed))
        elif ftype == 'closing':
            logger.warning('引擎主动关闭通道: %s', frame.get('reason'))
        elif ftype == 'event':
            self._on_event(frame)
            self._notify_listeners(frame)
        elif ftype == 'snapshot':
            self._on_snapshot(frame)
            self._notify_listeners(frame)
        elif ftype == 'result':
            self._on_result(frame)
        else:
            logger.debug('未知引擎帧 type=%s', ftype)

    # ── 帧处理：状态与游标（取锁，不阻塞循环）──────────────────────────────
    def _on_event(self, frame):
        topic = frame.get('topic', '')
        data = frame.get('data') or {}
        cursor = frame.get('cursor')
        with self._lock:
            self._stats['events'] += 1
            if topic == 'order':
                entno = str(data.get('entno') or cursor or '')
                if entno:
                    self._orders[entno] = data
                    status = str(data.get('status') or '')
                    if status in _TERMINAL_ORDER_STATUS:
                        self._open_orders.pop(entno, None)
                    else:
                        self._open_orders[entno] = status
                    try:
                        self._last_entno = max(self._last_entno, int(entno))
                    except ValueError:
                        pass
            elif topic == 'trade':
                tdno = str(data.get('tdno') or cursor or '')
                if tdno:
                    self._trades[tdno] = data
                    try:
                        self._last_tdno = max(self._last_tdno, int(tdno))
                    except ValueError:
                        pass

    def _on_snapshot(self, frame):
        topic = frame.get('topic', '')
        data = frame.get('data') or {}
        key = str(frame.get('key') or '')
        with self._lock:
            self._stats['snapshots'] += 1
            if topic == 'position' and key:
                self._positions[key] = data
            elif topic == 'balance' and key:
                self._balances[key] = data
            elif topic == 'strategy' and key:
                # param/indicator 是引擎透传的 JSON 字符串，这里解析成对象，
                # 与既有 REST/SSE 的 strategies 行同形（前端不必区分来源）
                row = dict(data)
                for field in ('param', 'indicator'):
                    raw = row.get(field)
                    if isinstance(raw, str) and raw:
                        try:
                            row[field] = json.loads(raw)
                        except ValueError:
                            pass
                self._strategies[key] = row
            elif topic.startswith('depth:'):
                # 盘口：按 topic 缓存最新快照（浏览器段订阅时直接下发）
                self._depths[topic] = {'ts': frame.get('ts'), 'data': data}

    def _on_result(self, frame):
        req_id = frame.get('req_id')
        with self._lock:
            self._stats['results'] += 1
            pending = self._pending.pop(req_id, None)
        if pending is None:
            logger.debug('收到无人等待的 result req_id=%s', req_id)
            return
        if frame.get('ok') is False:
            pending.error = str(frame.get('error') or 'engine_error')
        else:
            pending.result = frame
        pending.event.set()

    # ── 补查编排 ────────────────────────────────────────────────────────────
    async def _recover_after_reconnect(self):
        """重连后按游标补查（rev4 §4）：成交走 tdno_gt 分页，委托走 entno_gt + 已知在途批量。"""
        with self._lock:
            last_tdno = self._last_tdno
            last_entno = self._last_entno
            open_entnos = list(self._open_orders.keys())[:500]

        trades = await self._query_paged('trades', last_tdno, DEFAULT_MAX_PAGES)
        orders_new = await self._query_paged('orders', last_entno, DEFAULT_MAX_PAGES)
        orders_known = []
        if open_entnos:
            orders_known = await self._query_once('orders', entnos=open_entnos)
        logger.info('重连补查完成: trades=%d, orders(新增)=%d, orders(在途刷新)=%d',
                    len(trades), len(orders_new), len(orders_known))

    async def _query_paged(self, what, cursor, max_pages):
        """分页补查：满页继续（has_more），直到空页或达上限。"""
        collected = []
        for _ in range(max_pages):
            rows = await self._query_once(what, cursor=cursor)
            collected.extend(rows)
            if len(rows) < DEFAULT_PAGE_ROWS:
                break
            try:
                cursor = max(int(r.get('tdno' if what == 'trades' else 'entno', 0)) for r in rows)
            except ValueError:
                break
        return collected

    async def _query_once(self, what, cursor=None, entnos=None):
        """发一次 query 并等 result（协程内 await 线程槽，不阻塞 asyncio 循环）。"""
        req_id = next(self._req_seq)
        pending = _PendingQuery()
        with self._lock:
            self._pending[req_id] = pending
            ws = self._conn
            self._stats['queries_sent'] += 1
        if ws is None:
            with self._lock:
                self._pending.pop(req_id, None)
            return []
        payload = {'type': 'query', 'req_id': req_id, 'what': what, 'limit': DEFAULT_PAGE_ROWS}
        if entnos:
            payload['entnos'] = entnos
        elif cursor:
            payload['cursor'] = str(cursor)
        await self._send_raw(ws, payload)

        timeout = float(self.cfg.get('engine_query_timeout_s', DEFAULT_QUERY_TIMEOUT_S))
        result, error = await asyncio.get_running_loop().run_in_executor(None, pending.wait, timeout)
        if error is not None:
            with self._lock:
                self._stats['queries_timeout'] += 1
            logger.warning('补查超时: what=%s cursor=%s (%s)', what, cursor, error)
            return []
        with self._lock:
            self._stats['queries_ok'] += 1
        return list(result.get('data') or [])

    # ── 线程安全 API（Flask 线程/测试调用）──────────────────────────────────
    def query(self, what, cursor=None, entnos=None, timeout=None):
        """同步补查（供 Flask 路由/诊断使用）。

        :return: (rows, error)；error 为 None 表示成功
        """
        done = threading.Event()
        box = {}

        async def _run():
            box['rows'] = await self._query_once(what, cursor=cursor, entnos=entnos)
            done.set()

        if not self._post_to_loop(lambda: _run()):
            return [], 'engine_link_down'
        wait_s = float(timeout if timeout is not None else self.cfg.get('engine_query_timeout_s', DEFAULT_QUERY_TIMEOUT_S)) + 1.0
        if not done.wait(wait_s):
            return [], 'timeout'
        return box.get('rows', []), None

    def subscribe(self, topics):
        """（重新）声明订阅通道；返回是否投递成功。"""
        return self._post_to_loop(lambda: self._send_subscribe(self._conn, topics))

    async def _send_subscribe(self, ws, topics):
        if ws is None:
            return
        await self._send_raw(ws, {'type': 'subscribe', 'topics': list(topics)})

    async def _send_raw(self, ws, payload):
        if ws is None:
            return
        try:
            await ws.send(json.dumps(payload, ensure_ascii=False, separators=(',', ':')))
        except Exception as e:
            logger.warning('向引擎发送失败: %s', e)

    def add_listener(self, callback):
        """注册引擎帧监听器（收到 event/snapshot 时回调，参数为解析后的 frame dict）。

        回调在 **asyncio 线程** 执行，实现里必须只做线程安全的事（如加锁入队）。
        """
        with self._lock:
            if callback not in self._listeners:
                self._listeners.append(callback)

    def remove_listener(self, callback):
        with self._lock:
            if callback in self._listeners:
                self._listeners.remove(callback)

    def _notify_listeners(self, frame):
        with self._lock:
            listeners = list(self._listeners)
        for callback in listeners:
            try:
                callback(frame)
            except Exception as e:  # 单个监听器异常不能打断链路
                logger.warning('引擎帧监听器异常: %s', e)

    # ── 供 hub 取实时数据（替换轮询上游；见 rev4 §5）────────────────────────
    def get_depth(self, market, inst_id):
        """取某标的最新盘口快照；没有则 None（调用方回落 HTTP 轮询）。

        返回形状与既有 `GET /api/trade/depth` 的 payload 一致：{timestamp, asks, bids}
        """
        topic = f'depth:{market}:{inst_id}'
        with self._lock:
            entry = self._depths.get(topic)
        if not entry:
            return None
        data = entry.get('data') or {}
        return {
            'timestamp': data.get('timestamp', 0),
            'asks': data.get('asks') or [],
            'bids': data.get('bids') or [],
        }

    def get_strategies(self):
        """取引擎推来的策略行（与 REST/SSE 的 strategies 行同形）；没有则 None。

        只有在**已收到引擎推送**时才返回（空列表也返回 None），这样"引擎没数据"会
        自然回落到 MySQL 查询，而不是让前端看到空表。
        """
        with self._lock:
            if not self._strategies:
                return None
            return list(self._strategies.values())

    def status(self):
        """诊断快照（`GET /api/push/engine`）。"""
        with self._lock:
            return {
                'listening': self._server is not None,
                'port': self.port,
                'connected': self._connected,
                'hello_done': self._hello_done,
                'subscribed': sorted(self._subscribed),
                'last_entno': self._last_entno,
                'last_tdno': self._last_tdno,
                'open_orders': len(self._open_orders),
                'orders_cached': len(self._orders),
                'trades_cached': len(self._trades),
                'positions_cached': len(self._positions),
                'balances_cached': len(self._balances),
                'strategies_cached': len(self._strategies),
                'depths_cached': len(self._depths),
                'pending_queries': len(self._pending),
                **self._stats,
            }


_link = None
_link_lock = threading.Lock()


def get_engine_link(cfg=None):
    """进程内唯一实例（首次调用按 web_push 配置构造）。"""
    global _link
    with _link_lock:
        if _link is None:
            if cfg is None:
                import os as _os
                from config import config as _config
                env = _os.environ.get('FLASK_ENV', 'development')
                cfg = getattr(_config[env](), 'WEB_PUSH', {}) or {}
            _link = EngineLink(cfg)
        return _link


def reset_engine_link():
    """销毁单例（测试用）。"""
    global _link
    with _link_lock:
        if _link is not None:
            _link.stop()
        _link = None
