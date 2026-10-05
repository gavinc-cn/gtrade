#!/usr/bin/env python3
"""SSE 推送通道集成测试：真实 HTTP + 真实 SSE 长流 + 桩上游引擎（含真实 MySQL 策略表）。

覆盖方案 §5.2/§5.3 的集成项（桩引擎可控"变化/无变化/404"，MySQL 用真实库）：

1. 建流首帧 retry + ready，`strategies` 订阅即全量、`depth` 订阅即首推（reason=initial）
2. 变化检测：上游数据冻结期间不推（同一帧不重复发）
3. 限频下限：恢复变化后推送频率不超过 1/depth_min_push_ms
4. 切标的不断流：unsubscribe 旧 + subscribe 新，**不重连**（无第二个 ready、stream_id 不变），
   同一条流上的 `strategies` 增量推送不中断（多路复用互不干扰）
5. 退订即停：控制面退订后中枢立即停止对该标的的上游采样（桩引擎计数不再增长）
6. 404 标的 -> `upstream_error(not_subscribed)`；`stream_id` 不存在 -> 404 JSON
7. 传输量对比：同长度窗口内 SSE 收字节 vs 旧轮询（1Hz 盘口 + 10s 策略表）请求字节基线

前置：MySQL（config 里的 host/port）可连；端口 46018/46019 空闲。
运行：cd web_server && python3 test_push_stream.py
"""
import asyncio
import json
import os
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

import pymysql
import requests

sys.path.insert(0, os.path.dirname(__file__))

from config import config  # noqa: E402

APP_PORT = 46018
STUB_PORT = 46019
ENGINE_WS_PORT = 46017           # 引擎入口（测试用；env 覆盖 web_push.ws_port）
ENGINE_WS_SECRET = 'push-e2e-secret'
BASE = f'http://127.0.0.1:{APP_PORT}'
STUB = f'http://127.0.0.1:{STUB_PORT}'
INST_A = 'BTC-USDT-SWAP'
INST_B = 'ETH-USDT-SWAP'
TOPIC_A = f'depth:okx_dummy:{INST_A}'
TOPIC_B = f'depth:okx_dummy:{INST_B}'
TEST_STRAT = '__push_e2e_strat__'


class StubEngine:
    """桩上游：可控变化/冻结/404，并记录每个标的的请求次数。"""

    def __init__(self):
        self.lock = threading.Lock()
        self.calls = {}
        self.tick = 0
        self.frozen = False
        self.server = None

    def handle_depth(self, inst_id, market):
        with self.lock:
            self.calls[inst_id] = self.calls.get(inst_id, 0) + 1
            self.tick += 1
            tick = self.tick
        if inst_id == 'NO-SUCH-INST':
            return 404, {'success': False, 'error': 'not_subscribed'}
        ts = 1000 if self.frozen else 1000 + tick
        return 200, {
            'success': True, 'inst_id': inst_id, 'market': market, 'timestamp': ts,
            'asks': [[100.0 + (ts % 5), 1.5]], 'bids': [[99.0, 2.5]],
        }

    def call_count(self, inst_id=None):
        with self.lock:
            if inst_id is None:
                return sum(self.calls.values())
            return self.calls.get(inst_id, 0)

    def start(self):
        engine = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):  # noqa: N802
                parsed = urlparse(self.path)
                query = parse_qs(parsed.query)
                if parsed.path != '/api/trade/depth':
                    self.send_response(404)
                    self.end_headers()
                    return
                status, body = engine.handle_depth(
                    (query.get('inst_id') or [''])[0], (query.get('market') or ['okx'])[0])
                payload = json.dumps(body).encode()
                self.send_response(status)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(payload)))
                self.send_header('Connection', 'close')  # 明确不复用（引擎侧的 keep-alive 陷阱）
                self.end_headers()
                self.wfile.write(payload)

            def log_message(self, *args):
                pass

        self.server = ThreadingHTTPServer(('127.0.0.1', STUB_PORT), Handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()


class FakeEngineWs:
    """假引擎：连 web_server 的 /ws/engine，走真实握手/订阅/心跳/补查契约。

    在独立线程里跑 asyncio（与服务端 ws_engine.py 对称）；供测试从外部注入事件帧与查询应答。
    """

    def __init__(self, port, secret):
        self.port = port
        self.secret = secret
        self.loop = None
        self.ws = None
        self.frames = []          # 收到的全部帧（dict）
        self.lock = threading.Lock()
        self.ready = threading.Event()
        self.query_reply = {'result': {'ok': True, 'count': 1, 'data': [{'entno': '9001', 'status': '2'}]}}
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self._thread.start()
        return self.ready.wait(10)

    def _run(self):
        asyncio.set_event_loop(asyncio.new_event_loop())
        self.loop = asyncio.get_event_loop()
        self.loop.run_until_complete(self._main())

    async def _main(self):
        import websockets
        try:
            async with websockets.connect(f'ws://127.0.0.1:{self.port}/ws/engine') as ws:
                self.ws = ws
                await ws.send(json.dumps({'type': 'hello', 'role': 'engine', 'secret': self.secret}))
                async for raw in ws:
                    frame = json.loads(raw)
                    with self.lock:
                        self.frames.append(frame)
                    ftype = frame.get('type')
                    if ftype == 'hello_ack':
                        self.ready.set()
                    elif ftype == 'subscribe':
                        await ws.send(json.dumps({'type': 'ack', 'topics': frame.get('topics')}))
                    elif ftype == 'ping':
                        await ws.send(json.dumps({'type': 'pong', 'ts': frame.get('ts'), 'rx': len(self.frames)}))
                    elif ftype == 'query':
                        reply = dict(self.query_reply.get('result') or {})
                        reply.update({'type': 'result', 'req_id': frame.get('req_id'), 'what': frame.get('what')})
                        await ws.send(json.dumps(reply))
        except Exception as e:
            print(f'[FakeEngineWs] 退出: {e!r}')

    def send(self, payload):
        """线程安全地发一帧（从测试主线程调用）。"""
        if self.loop is None or self.ws is None:
            return False
        asyncio.run_coroutine_threadsafe(self.ws.send(json.dumps(payload)), self.loop)
        return True

    def received(self, ftype):
        with self.lock:
            return [f for f in self.frames if f.get('type') == ftype]


class SseReader:
    """后台读 SSE 流：逐行解析事件并打时间戳、累计字节数。"""

    def __init__(self, url):
        self.url = url
        self.events = []          # [{t, event, data, raw}]
        self.bytes = 0
        self.error = None
        self.resp = None
        self.started_at = None
        self._stop = False
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.started_at = time.monotonic()
        self._thread.start()

    def _run(self):
        try:
            with requests.get(self.url, stream=True, timeout=(5, 30)) as resp:
                self.resp = resp
                event_name, data_lines = None, []
                for raw in resp.iter_lines(decode_unicode=True):
                    if self._stop:
                        return
                    if raw is None:
                        continue
                    self.bytes += len(raw.encode('utf-8')) + 1
                    line = raw
                    if line == '':
                        if event_name and data_lines:
                            self.events.append({
                                't': time.monotonic() - self.started_at,
                                'event': event_name,
                                'data': json.loads('\n'.join(data_lines)),
                            })
                        event_name, data_lines = None, []
                        continue
                    if line.startswith('event:'):
                        event_name = line[6:].strip()
                    elif line.startswith('data:'):
                        data_lines.append(line[5:].strip())
                    elif line.startswith('retry:'):
                        self.events.append({
                            't': time.monotonic() - self.started_at,
                            'event': '__retry__',
                            'data': line[6:].strip(),
                        })
        except Exception as e:  # 流被服务端关闭 / 客户端 stop 都会走到这里
            if not self._stop:
                self.error = repr(e)

    def stop(self):
        self._stop = True

    def slice(self, event=None, since=0.0, until=None):
        """取时间窗内的事件。"""
        out = []
        for e in self.events:
            if e['t'] < since:
                continue
            if until is not None and e['t'] > until:
                continue
            if event is not None and e['event'] != event:
                continue
            out.append(e)
        return out

    def topics(self, event):
        return [e['data'].get('topic') for e in self.events if e['event'] == event]


def wait_http(url, timeout_s=20):
    """等 HTTP 服务起来。"""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            requests.get(url, timeout=1)
            return True
        except requests.RequestException:
            time.sleep(0.2)
    return False


def run():
    print("=== SSE 推送通道集成测试 ===")
    failures = []

    def check(name, cond, detail=''):
        print(f"{'PASS' if cond else 'FAIL'}  {name}{(' -> ' + str(detail)) if detail and not cond else ''}")
        if not cond:
            failures.append(name)

    # ── 准备：测试用策略行（真实 MySQL，结束前删除） ──────────────────────
    db = pymysql.connect(**config['development']().database_config)
    with db.cursor() as cur:
        cur.execute("DELETE FROM strat_info WHERE strat_name = %s", (TEST_STRAT,))
        cur.execute(
            "INSERT INTO strat_info (strat_name, strat_template, param, indicator, status) "
            "VALUES (%s, %s, %s, %s, %s)",
            (TEST_STRAT, 'StratSMA', '{}', json.dumps({'pnl': 1}), 0))
    db.commit()

    stub = StubEngine()
    stub.start()
    log_path = '/tmp/push_stream_app.log'
    app_log = open(log_path, 'w', encoding='utf-8')
    env = dict(os.environ)
    env['FLASK_ENV'] = 'development'
    env['GTRADE_HTTP_GATEWAY'] = STUB
    env['WEB_PUSH_WS_PORT'] = str(ENGINE_WS_PORT)
    env['WEB_PUSH_ENGINE_SECRET'] = ENGINE_WS_SECRET
    env['WEB_PUSH_PING_INTERVAL_TRADE_MS'] = '1000'   # trade 通道心跳缩短，便于断言 ping 帧
    child = subprocess.Popen(
        [sys.executable, '-c',
         f"from app import app; app.run(host='127.0.0.1', port={APP_PORT}, debug=False, threaded=True)"],
        cwd=os.path.dirname(os.path.abspath(__file__)), env=env, stdout=app_log, stderr=subprocess.STDOUT)

    reader = None
    try:
        check('Flask 应用就绪', wait_http(f'{BASE}/api/health'), log_path)

        # ── 登录 + 票据 ───────────────────────────────────────────────────
        web_cfg = config['development']()
        resp = requests.post(f'{BASE}/api/login',
                             json={'username': web_cfg.WEB_USER, 'password': web_cfg.WEB_PASSWORD}, timeout=5)
        token = resp.json()['token']
        auth = {'Authorization': f'Bearer {token}'}

        resp = requests.get(f'{BASE}/api/settings/push', headers=auth, timeout=5)
        push_cfg = resp.json()['data']
        check('下发前端推送配置', resp.status_code == 200 and push_cfg['depth_min_push_ms'] == 500, resp.text)

        resp = requests.post(f'{BASE}/api/stream/ticket', headers=auth, timeout=5)
        ticket = resp.json().get('ticket')
        check('签发一次性票据', resp.status_code == 200 and bool(ticket), resp.text)
        # 票据一次性：首次能建流（200），同一个票据再用必须 401
        first = requests.get(f'{BASE}/api/stream?topics={TOPIC_A}&ticket={ticket}', stream=True, timeout=5)
        check('票据首次可建流', first.status_code == 200, first.status_code)
        first.close()
        reuse = requests.get(f'{BASE}/api/stream?topics={TOPIC_A}&ticket={ticket}', timeout=5)
        check('票据一次性（复用被拒 401）', reuse.status_code == 401, reuse.text)

        resp = requests.post(f'{BASE}/api/stream/ticket', headers=auth, timeout=5)
        ticket = resp.json()['ticket']

        # ── 建流（两个 topic 复用同一条流） ───────────────────────────────
        url = (f'{BASE}/api/stream?topics={TOPIC_A},strategies&ticket={ticket}')
        reader = SseReader(url)
        reader.start()
        time.sleep(1.5)

        ready = reader.slice('ready')
        check('首帧 ready', len(ready) == 1 and ready[0]['data']['topics'] == sorted([TOPIC_A, 'strategies']), ready)
        check('ready 带 stream_id', bool(ready and ready[0]['data'].get('stream_id')))
        stream_id = ready[0]['data']['stream_id'] if ready else ''

        headers = {k.lower(): v for k, v in reader.resp.headers.items()} if reader.resp else {}
        check('SSE 响应头正确',
              headers.get('content-type', '').startswith('text/event-stream')
              and headers.get('x-accel-buffering') == 'no'
              and headers.get('transfer-encoding') == 'chunked', headers)
        print(f"      SSE 响应头: {headers}")

        depth_evs = reader.slice('depth')
        check('订阅即首推 depth(initial)', bool(depth_evs) and depth_evs[0]['data']['reason'] == 'initial', depth_evs)
        check('depth 事件带 topic/seq/version/stale_ms',
              depth_evs and all(k in depth_evs[0]['data'] for k in ('topic', 'seq', 'version', 'stale_ms')), depth_evs)

        strat_evs = reader.slice('strategies')
        check('订阅即全量 strategies(full)', len(strat_evs) == 1 and strat_evs[0]['data']['full'] is True, strat_evs)
        check('全量含测试策略行',
              strat_evs and any(r['strat_name'] == TEST_STRAT for r in strat_evs[0]['data']['changed']), strat_evs)

        # ── 变化检测：上游冻结期间不推 ─────────────────────────────────────
        # 冻结瞬间还会有一帧"最后一次变化"要落地（含限频窗），先等它出完再计时
        stub.frozen = True
        time.sleep(1.2)
        t0 = time.monotonic() - reader.started_at
        time.sleep(1.6)
        t1 = time.monotonic() - reader.started_at
        frozen_depth = reader.slice('depth', since=t0, until=t1)
        check('上游无变化期间不推 depth', len(frozen_depth) == 0, frozen_depth)

        # ── 限频下限：恢复变化后不超过 1/0.5s ──────────────────────────────
        stub.frozen = False
        t2 = time.monotonic() - reader.started_at
        time.sleep(2.0)
        t3 = time.monotonic() - reader.started_at
        live_depth = reader.slice('depth', since=t2, until=t3)
        check('恢复变化后持续推送', len(live_depth) >= 2, live_depth)
        check('推送频率不超过下限(0.5s)', len(live_depth) <= 5, len(live_depth))
        gaps = [round(live_depth[i + 1]['t'] - live_depth[i]['t'], 3) for i in range(len(live_depth) - 1)]
        check('相邻推送间隔 >= 0.45s', all(g >= 0.45 for g in gaps), gaps)
        print(f"      depth 推送 {len(live_depth)} 帧 / 2.0s，间隔 {gaps}")

        # ── 切标的不断流 + 同流上的 strategies 不受影响 ────────────────────
        before_depth_a = stub.call_count(INST_A)
        resp = requests.post(f'{BASE}/api/stream/unsubscribe', headers=auth,
                             json={'stream_id': stream_id, 'topics': [TOPIC_A]}, timeout=5)
        check('控制面退订', resp.status_code == 200 and resp.json()['removed'] == [TOPIC_A], resp.text)
        resp = requests.post(f'{BASE}/api/stream/subscribe', headers=auth,
                             json={'stream_id': stream_id, 'topics': [TOPIC_B]}, timeout=5)
        check('控制面订阅新标的', resp.status_code == 200 and resp.json()['added'] == [TOPIC_B], resp.text)

        t4 = time.monotonic() - reader.started_at
        with db.cursor() as cur:            # 切标的同时改策略指标 -> 验证同流多路复用不中断
            cur.execute("UPDATE strat_info SET indicator = %s WHERE strat_name = %s",
                        (json.dumps({'pnl': 42}), TEST_STRAT))
        db.commit()
        time.sleep(2.0)
        t5 = time.monotonic() - reader.started_at

        check('切标的不重连（无第二个 ready）', len(reader.slice('ready')) == 1, reader.slice('ready'))
        new_depth = [e for e in reader.slice('depth', since=t4, until=t5) if e['data']['topic'] == TOPIC_B]
        old_depth = [e for e in reader.slice('depth', since=t4, until=t5) if e['data']['topic'] == TOPIC_A]
        check('新标的开始推送 depth', len(new_depth) >= 2, new_depth)
        check('旧标的停止推送 depth', len(old_depth) == 0, old_depth)
        check('旧标的不再被采样', stub.call_count(INST_A) - before_depth_a <= 1,
              stub.call_count(INST_A) - before_depth_a)

        deltas = [e for e in reader.slice('strategies', since=t4, until=t5) if e['data']['full'] is False]
        check('同一条流上 strategies 增量照常推送（多路复用互不干扰）',
              len(deltas) == 1 and any(r['strat_name'] == TEST_STRAT for r in deltas[0]['data']['changed']), deltas)
        check('增量只带变化行（不含 removed）',
              deltas and deltas[0]['data']['removed'] == [] and len(deltas[0]['data']['changed']) == 1, deltas)

        # ── 退订即停（refcount 归零） ──────────────────────────────────────
        resp = requests.post(f'{BASE}/api/stream/unsubscribe', headers=auth,
                             json={'stream_id': stream_id, 'topics': [TOPIC_B, 'strategies']}, timeout=5)
        check('全部退订', resp.status_code == 200 and set(resp.json()['removed']) == {TOPIC_B, 'strategies'}, resp.text)
        time.sleep(0.5)
        count_after_unsub = stub.call_count()
        time.sleep(1.2)
        check('退订后停止上游采样', stub.call_count() == count_after_unsub,
              f"{count_after_unsub} -> {stub.call_count()}")

        # ── 错误语义 ──────────────────────────────────────────────────────
        resp = requests.post(f'{BASE}/api/stream/subscribe', headers=auth,
                             json={'stream_id': stream_id, 'topics': ['depth:okx_dummy:NO-SUCH-INST']}, timeout=5)
        check('订阅不存在的标的不报错（异步推错误事件）', resp.status_code == 200, resp.text)
        time.sleep(1.2)
        errs = reader.slice('upstream_error')
        check('上游 404 -> upstream_error(not_subscribed)',
              len(errs) == 1 and errs[0]['data']['code'] == 'not_subscribed', errs)
        check('错误事件带 topic', errs and errs[0]['data']['topic'] == 'depth:okx_dummy:NO-SUCH-INST', errs)

        resp = requests.post(f'{BASE}/api/stream/subscribe', headers=auth,
                             json={'stream_id': 'deadbeef', 'topics': [TOPIC_A]}, timeout=5)
        check('未知 stream_id -> 404', resp.status_code == 404, resp.text)
        resp = requests.post(f'{BASE}/api/stream/subscribe', headers=auth,
                             json={'stream_id': stream_id, 'topics': ['bogus']}, timeout=5)
        check('非法 topic -> 400', resp.status_code == 400, resp.text)
        resp = requests.post(f'{BASE}/api/stream/subscribe', headers={}, json={'stream_id': stream_id, 'topics': [TOPIC_A]}, timeout=5)
        check('控制面无 JWT -> 401', resp.status_code == 401, resp.text)
        resp = requests.get(f'{BASE}/api/stream?topics={TOPIC_A}&ticket=nope', timeout=5)
        check('无效票据 -> 401 JSON', resp.status_code == 401 and resp.json()['success'] is False, resp.text)

        # ── 阶段 2 通道契约：引擎 WS 直推 -> SSE 扇出 + 通道内补查 ─────────
        engine_ws = FakeEngineWs(ENGINE_WS_PORT, ENGINE_WS_SECRET)
        check('假引擎完成 /ws/engine 握手', engine_ws.start())
        deadline = time.time() + 5
        while time.time() < deadline and not engine_ws.received('subscribe'):
            time.sleep(0.1)
        subs = engine_ws.received('subscribe')
        check('引擎侧收到订阅声明',
              bool(subs) and {'trade', 'quote'} <= set(subs[0].get('topics') or []), subs)

        ticket_t = requests.post(f'{BASE}/api/stream/ticket', headers=auth, timeout=5).json().get('ticket')
        trade_reader = SseReader(f'{BASE}/api/stream?channel=trade&topics=order,trade&ticket={ticket_t}')
        trade_reader.start()
        time.sleep(1.0)
        ready_t = trade_reader.slice('ready')
        check('trade 通道建流成功', len(ready_t) == 1 and ready_t[0]['data']['channel'] == 'trade', ready_t)

        ticket_bad = requests.post(f'{BASE}/api/stream/ticket', headers=auth, timeout=5).json().get('ticket')
        bad = requests.get(f'{BASE}/api/stream?channel=trade&topics=depth:okx_dummy:{INST_A}&ticket={ticket_bad}',
                           timeout=5)
        check('trade 通道拒收 depth（400）', bad.status_code == 400, bad.text)

        stream_id = ready_t[0]['data']['stream_id'] if ready_t else ''
        engine_ws.send({'type': 'event', 'channel': 'trade', 'topic': 'order', 'ts': 111,
                        'cursor': '9001', 'status_id': 2, 'data': {'entno': '9001', 'status': '2', 'filled': 0}})
        deadline = time.time() + 5
        while time.time() < deadline and not trade_reader.slice('order'):
            time.sleep(0.1)
        order_evs = trade_reader.slice('order')
        check('引擎委托帧扇出为 SSE order 事件', len(order_evs) >= 1, order_evs)
        od = order_evs[0]['data'] if order_evs else {}
        check('order 事件带 cursor', od.get('cursor') == '9001', od)
        check('order 事件带 status_id', od.get('status_id') == 2, od)
        check('order 事件带服务端 ts', isinstance(od.get('ts'), int), od)

        engine_ws.send({'type': 'event', 'channel': 'trade', 'topic': 'trade', 'ts': 222,
                        'cursor': '9100', 'data': {'tdno': '9100', 'ordno': '9001', 'td_px': 12.5}})
        deadline = time.time() + 5
        while time.time() < deadline and not trade_reader.slice('trade'):
            time.sleep(0.1)
        trade_evs = trade_reader.slice('trade')
        check('引擎成交帧扇出为 SSE trade 事件', len(trade_evs) >= 1, trade_evs)
        check('trade 事件带 cursor=tdno',
              bool(trade_evs) and trade_evs[0]['data'].get('cursor') == '9100', trade_evs)

        resp = requests.post(f'{BASE}/api/stream/query', headers=auth, timeout=10,
                             json={'stream_id': stream_id, 'req_id': 42, 'what': 'orders',
                                   'cursor': 9001, 'limit': 200})
        check('补查请求被受理', resp.status_code == 200 and resp.json()['data']['req_id'] == 42, resp.text)
        deadline = time.time() + 5
        while time.time() < deadline and not engine_ws.received('query'):
            time.sleep(0.1)
        queries = engine_ws.received('query')
        # 链路建立时会自动发起"重连补查"（无 cursor），这里按游标定位测试自己发起的那次
        mine = [q for q in queries if q.get('cursor') == '9001']
        check('引擎收到补查请求', bool(mine), queries)
        deadline = time.time() + 5
        while time.time() < deadline and not trade_reader.slice('result'):
            time.sleep(0.1)
        results = trade_reader.slice('result')
        check('补查结果沿同一条流回流', bool(results) and results[0]['data'].get('req_id') == 42, results)
        check('补查结果带数据',
              bool(results) and results[0]['data']['data'][0]['entno'] == '9001', results)

        deadline = time.time() + 5
        while time.time() < deadline and not trade_reader.slice('ping'):
            time.sleep(0.1)
        pings = trade_reader.slice('ping')
        check('trade 通道收到 ping 事件帧', len(pings) >= 1, pings)
        check('ping 帧带 ts', bool(pings) and isinstance(pings[0]['data'].get('ts'), int), pings)

        trade_reader.stop()

        # ── 传输量对比（方案 §0 基线：1 次轮询 = 785 请求头 + 233 响应头 + 223 正文） ──
        window = t5 - t2                       # 观察窗口（活跃推送期）
        frames = len(reader.slice('depth', since=t2, until=t5))
        poll_per_frame = 785 + 233 + 223       # 旧方案单次轮询全链路字节
        poll_window = window * poll_per_frame + max(1.0, window / 10.0) * 2000  # + 策略表 10s 轮询
        sse_per_frame = reader.bytes / max(1, frames)
        check('单帧开销：SSE 低于轮询的一半', sse_per_frame < poll_per_frame * 0.5,
              f"SSE {sse_per_frame:.0f}B/帧 vs 轮询 {poll_per_frame}B/次")
        check('同窗口总字节：SSE 低于轮询基线', reader.bytes < poll_window,
              f"SSE {reader.bytes}B vs 轮询 {poll_window:.0f}B（窗口 {window:.1f}s）")
        print(f"      窗口 {window:.1f}s：SSE {reader.bytes}B / {frames} 帧盘口 -> {sse_per_frame:.0f}B/帧；"
              f"旧轮询同窗口 {poll_window:.0f}B（{poll_per_frame}B/次 × {window:.0f} 次）")
        print(f"      depth 上游采样次数 {stub.calls}")

    finally:
        if reader:
            reader.stop()
        child.terminate()
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill()
        app_log.close()
        if stub.server:
            stub.server.shutdown()
        with db.cursor() as cur:
            cur.execute("DELETE FROM strat_info WHERE strat_name = %s", (TEST_STRAT,))
        db.commit()
        db.close()

    print()
    if failures:
        print(f"FAILED ({len(failures)}): " + ', '.join(failures))
        print(f"应用日志: {log_path}")
        return 1
    print("ALL PASS")
    return 0


if __name__ == '__main__':
    sys.exit(run())
