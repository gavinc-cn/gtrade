#!/usr/bin/env python3
"""推送中枢纯逻辑测试（不起服务、不连引擎/MySQL）。

覆盖方案 §5.1 的单测项：topic 解析与上限、限频状态机（首推/合并/保底/退订即停）、
depth 变化检测与 stale_ms、strategies 增量 diff（新增/修改/删除）、队列语义
（快照丢最旧帧、增量满则断连）、上游失败与错误事件、票据一次性与过期。

做法：假时钟替换 push_hub.now_ms（限频全部基于单调钟），上游用桩函数替换
gtrade_client.forward 与 strategy_service，逐步 `_sampler_tick()` 驱动，断言完全确定。

运行：cd web_server && python3 test_push_hub.py
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))

import gtrade_client  # noqa: E402
import push_hub  # noqa: E402
import push_stream  # noqa: E402
from push_hub import PushHub, parse_topic  # noqa: E402

CFG = {
    'hub_tick_ms': 50,
    'depth_poll_ms': 250,
    'depth_min_push_ms': 500,
    'depth_timeout_s': 2,
    'db_poll_ms': 1000,
    'strategies_min_push_ms': 1000,
    'max_push_ms': 60000,
    'queue_size': 8,
    'retry_max_ms': 5000,
    'ping_interval_ms': 20000,
    'max_topics_per_conn': 8,
    'ticket_ttl_s': 30,
}

DEPTH_TOPIC = 'depth:okx_dummy:BTC-USDT-SWAP'


class FakeClock:
    """可控单调时钟（毫秒）。"""

    def __init__(self, start=1000.0):
        self.t = start
        self.calls = 0

    def __call__(self):
        self.calls += 1
        return self.t

    def advance(self, ms):
        self.t += ms


class StubEngine:
    """桩上游：按需返回盘口快照 / 404 / 异常，并记录调用次数。"""

    def __init__(self):
        self.ts = 1
        self.asks = [[100.0, 1.0]]
        self.bids = [[99.0, 2.0]]
        self.mode = 'ok'          # ok | 404 | down
        self.calls = 0

    def __call__(self, method, path, json_body=None, params=None, timeout=None):
        self.calls += 1
        if self.mode == 'down':
            raise gtrade_client.EngineUnavailable('引擎不可达: 连接被拒绝')
        if self.mode == '404':
            return {'success': False, 'error': 'not_subscribed'}, 404
        return {
            'success': True,
            'inst_id': params.get('inst_id'),
            'market': params.get('market'),
            'timestamp': self.ts,
            'asks': self.asks,
            'bids': self.bids,
        }, 200


class StubStrategyService:
    """桩策略服务：返回可控的 strat_info 行列表。"""

    def __init__(self, rows=None):
        self.rows = rows or []
        self.calls = 0
        self.fail = False

    def get_all_strategies(self):
        self.calls += 1
        if self.fail:
            raise RuntimeError('db down')
        return [dict(r) for r in self.rows]


class StubEngineLink:
    """桩引擎侧链路：验证 hub 优先用引擎推来的数据（rev4 §5）。"""

    def __init__(self):
        self.depth = None          # None = 引擎还没推过该标的
        self.strategies = None     # None = 引擎还没推过策略行
        self.depth_calls = 0
        self.strategy_calls = 0

    def get_depth(self, market, inst_id):
        self.depth_calls += 1
        return self.depth

    def get_strategies(self):
        self.strategy_calls += 1
        return self.strategies


def drain(conn):
    """取出连接队列里的全部事件。"""
    events = []
    while True:
        try:
            events.append(conn.queue.get_nowait())
        except Exception:
            break
    return events


def names(events):
    return [e['event'] for e in events]


def run():
    print("=== 推送中枢纯逻辑测试 ===")
    failures = []

    def check(name, cond, detail=''):
        print(f"{'PASS' if cond else 'FAIL'}  {name}{(' -> ' + str(detail)) if detail and not cond else ''}")
        if not cond:
            failures.append(name)

    clock = FakeClock()
    push_hub.now_ms = clock  # 全局假时钟（模块内所有限频判定都走它）

    # ── 1. topic 解析 ──────────────────────────────────────────────────────
    check('topic: depth 合法', parse_topic(DEPTH_TOPIC) == ('depth', ('okx_dummy', 'BTC-USDT-SWAP')))
    check('topic: strategies 合法', parse_topic('strategies') == ('strategies', ()))
    check('topic: depth 少 key 非法', parse_topic('depth:okx') is None)
    check('topic: depth 空 key 非法', parse_topic('depth:okx:') is None)
    check('topic: strategies 带 key 非法', parse_topic('strategies:x') is None)
    check('topic: 未知类型非法', parse_topic('orders') is None)

    # ── 2. 连接上限与非法 topic ────────────────────────────────────────────
    hub = PushHub(CFG, strategy_service=StubStrategyService())
    conn, err = hub.open_connection('u', [DEPTH_TOPIC])
    check('建流成功', err is None and conn is not None)
    ready = drain(conn)
    check('首事件为 ready', names(ready) == ['ready'] and ready[0]['data']['topics'] == [DEPTH_TOPIC], ready)

    _, err = hub.open_connection('u', [f'depth:okx:INS{i}' for i in range(9)])
    check('超上限 8 建流被拒', err is not None and err[0] == 400, err)
    _, err = hub.open_connection('u', ['bogus'])
    check('非法 topic 建流被拒', err is not None and err[0] == 400, err)

    ok, status, payload = hub.subscribe(conn, [f'depth:okx:INS{i}' for i in range(8)])
    check('增量订阅超上限被拒(原子)', (not ok) and status == 400, payload)
    check('被拒后订阅集不变', conn.topics == {DEPTH_TOPIC}, conn.topics)

    # ── 3. 首推不受下限约束 + 变化检测 ────────────────────────────────────
    engine = StubEngine()
    push_hub.gtrade_client.forward = engine
    clock.advance(10)
    hub._sampler_tick()   # 首采样 -> 有变化 -> force_push -> 立即推 initial
    evs = drain(conn)
    check('首推 reason=initial', len(evs) == 1 and evs[0]['event'] == 'depth'
          and evs[0]['data']['reason'] == 'initial', evs)
    check('首推带快照', evs and evs[0]['data']['data']['asks'] == [[100.0, 1.0]], evs)

    # ── 4. 变化检测 / 下限内合并 / 到期只推一帧 ────────────────────────────
    # 节奏：推送发生在 t=1010，采样点 1260/1510/1760/2010，下限 500ms
    base_calls = engine.calls
    clock.advance(250)          # t=1260：上游没变 -> 不推
    hub._sampler_tick()
    check('无变化不推', drain(conn) == [])

    engine.ts = 2
    clock.advance(250)          # t=1510：变化且窗满 -> 立即推（建立新基准）
    hub._sampler_tick()
    evs = drain(conn)
    check('窗满即推', len(evs) == 1 and evs[0]['data']['reason'] == 'change'
          and evs[0]['data']['data']['timestamp'] == 2, evs)

    engine.ts = 3
    clock.advance(250)          # t=1760：变化，但距上次推送仅 250ms -> 攒批
    hub._sampler_tick()
    check('下限内不推(攒批)', drain(conn) == [])
    engine.ts = 4
    clock.advance(250)          # t=2010：再次变化且窗满 -> 合并成一帧
    hub._sampler_tick()
    evs = drain(conn)
    check('窗内多次变化合并为一帧', len(evs) == 1 and evs[0]['data']['reason'] == 'change', evs)
    check('推的是最新一帧', evs and evs[0]['data']['data']['timestamp'] == 4, evs)
    check('上游按 250ms 采样(1000ms 内 4 次)', engine.calls - base_calls == 4, engine.calls - base_calls)

    # ── 5. stale_ms 与上限保底 ────────────────────────────────────────────
    clock.advance(60000)
    hub._sampler_tick()
    evs = drain(conn)
    check('60s 保底推 keepalive', len(evs) == 1 and evs[0]['data']['reason'] == 'keepalive', evs)
    check('上游可达时 stale_ms 很小(区分"无变化")', evs and evs[0]['data']['stale_ms'] < 1000, evs)

    # 上游失联：保底帧照发（带着旧快照），stale_ms 持续增长（区分"上游失联"）
    engine.mode = 'down'
    clock.advance(60000)
    hub._sampler_tick()
    evs = drain(conn)
    depth_evs = [e for e in evs if e['event'] == 'depth']
    check('失联后保底帧仍发', len(depth_evs) == 1 and depth_evs[0]['data']['reason'] == 'keepalive', evs)
    check('失联时 stale_ms 增长', depth_evs and depth_evs[0]['data']['stale_ms'] > 50000, evs)
    engine.mode = 'ok'

    # ── 6. 退订即停（refcount 归零 -> 摘除 topic -> 不再采上游） ────────────
    before = engine.calls
    hub.unsubscribe(conn, [DEPTH_TOPIC])
    clock.advance(1000)
    hub._sampler_tick()
    check('退订后停止上游采样', engine.calls == before, engine.calls - before)
    check('退订后 topic 摘除', hub.stats()['topics'] == [], hub.stats())

    # ── 7. 上游失败语义 ────────────────────────────────────────────────────
    conn2, _ = hub.open_connection('u', [DEPTH_TOPIC])
    drain(conn2)
    engine.mode = '404'
    clock.advance(10)
    hub._sampler_tick()
    evs = drain(conn2)
    check('404 推 upstream_error', len(evs) == 1 and evs[0]['event'] == 'upstream_error'
          and evs[0]['data']['code'] == 'not_subscribed', evs)
    clock.advance(250)
    hub._sampler_tick()
    clock.advance(250)
    hub._sampler_tick()
    check('错误状态不重复刷屏', drain(conn2) == [])
    engine.mode = 'down'
    clock.advance(1000)
    hub._sampler_tick()
    evs = drain(conn2)
    check('引擎不可达错误码', len(evs) == 1 and evs[0]['data']['code'] == 'engine_unavailable', evs)
    engine.mode = 'ok'
    engine.ts = 9
    clock.advance(5000)
    hub._sampler_tick()
    evs = drain(conn2)
    check('恢复后继续推数据', len(evs) == 1 and evs[0]['event'] == 'depth', evs)
    check('恢复后清错误状态', hub.stats()['topics'][0]['last_error'] is None, hub.stats())
    hub.close_connection(conn2)

    # ── 8. strategies 增量 diff ───────────────────────────────────────────
    svc = StubStrategyService([
        {'strat_name': 'a', 'strat_template': 'T', 'status': 0, 'update_time': 't1', 'indicator': {'pnl': 1}, 'param': {}},
        {'strat_name': 'b', 'strat_template': 'T', 'status': 0, 'update_time': 't1', 'indicator': {'pnl': 2}, 'param': {}},
    ])
    hub2 = PushHub(CFG, strategy_service=svc)
    conn3, _ = hub2.open_connection('u', ['strategies'])
    drain(conn3)
    clock.advance(10)
    hub2._sampler_tick()
    evs = drain(conn3)
    check('订阅即发全量', len(evs) == 1 and evs[0]['data']['full'] is True
          and len(evs[0]['data']['changed']) == 2, evs)

    clock.advance(1000)
    hub2._sampler_tick()
    check('无变化不推(策略)', drain(conn3) == [])

    svc.rows[0]['indicator'] = {'pnl': 11}   # 修改 a
    svc.rows[1]['status'] = 1                # 修改 b 的状态
    svc.rows.append({'strat_name': 'c', 'strat_template': 'T', 'status': 0,
                     'update_time': 't2', 'indicator': {}, 'param': {}})  # 新增 c
    clock.advance(1000)
    hub2._sampler_tick()
    evs = drain(conn3)
    check('增量只带变化行', len(evs) == 1 and evs[0]['data']['full'] is False
          and sorted(r['strat_name'] for r in evs[0]['data']['changed']) == ['a', 'b', 'c'], evs)

    clock.advance(1000)
    svc.rows = [r for r in svc.rows if r['strat_name'] != 'b']  # 删除 b
    hub2._sampler_tick()
    evs = drain(conn3)
    check('删除行进 removed', len(evs) == 1 and evs[0]['data']['removed'] == ['b']
          and evs[0]['data']['changed'] == [], evs)

    clock.advance(1000)
    svc.fail = True
    hub2._sampler_tick()
    evs = drain(conn3)
    check('DB 失败错误码', len(evs) == 1 and evs[0]['data']['code'] == 'db_unavailable', evs)
    svc.fail = False

    # ── 9. 队列语义：快照丢最旧帧 / 增量满则断连 ──────────────────────────
    qconn, _ = hub2.open_connection('u', ['strategies'])
    drain(qconn)
    for i in range(CFG['queue_size']):
        ok = qconn.enqueue({'event': 'strategies', 'data': {'i': i}}, droppable=False)
        check(f'增量入队第{i + 1}条', ok)
    ok = qconn.enqueue({'event': 'strategies', 'data': {'i': 'overflow'}}, droppable=False)
    check('增量队列满 -> 断连标记', ok is False and qconn.overflowed is True)
    hub2.close_connection(qconn)

    # 统一策略（rev4 §5）：**任何通道**队列满都只是标记溢出、不再丢帧——
    # 由事件流生成器下发 closing 后断连，客户端重连后重订阅（quote）/补查（trade）恢复。
    dconn, _ = hub2.open_connection('u', [DEPTH_TOPIC], push_hub.CHANNEL_QUOTE)
    drain(dconn)
    check('quote 通道队列更浅', dconn.queue.maxsize == CFG.get('queue_size_quote', 2), dconn.queue.maxsize)
    for i in range(dconn.queue.maxsize):
        dconn.enqueue({'event': 'depth', 'data': {'i': i}}, droppable=True)
    ok = dconn.enqueue({'event': 'depth', 'data': {'i': 'newest'}}, droppable=True)
    check('quote 队列满 -> 断连标记（不丢最旧帧）', ok is False and dconn.overflowed is True)
    q = [e['data']['i'] for e in drain(dconn)]
    check('溢出后队列内容未被改写', q == list(range(dconn.queue.maxsize)), q)

    dconn2, _ = hub2.open_connection('u', [DEPTH_TOPIC, 'strategies'])
    drain(dconn2)
    for i in range(CFG['queue_size']):
        dconn2.enqueue({'event': 'depth', 'data': {'i': i}}, droppable=True)
    ok = dconn2.enqueue({'event': 'depth', 'data': {'i': 'x'}}, droppable=True)
    check('mixed 通道满同样标记断连', ok is False and dconn2.overflowed is True)
    hub2.close_connection(dconn2)
    hub2.close_connection(dconn)

    # ── 10. 票据：一次性 + 过期 ───────────────────────────────────────────
    push_hub.now_ms = clock
    push_stream.now_ms = clock              # push_stream 里是 from 导入的绑定，需单独替换
    push_stream._cfg = lambda: CFG          # 测试注入配置
    ticket, ttl = push_stream.issue_ticket('u')
    check('票据 TTL 来自配置', ttl == 30)
    check('票据首次可用', push_stream.consume_ticket(ticket) == 'u')
    check('票据二次不可用(一次性)', push_stream.consume_ticket(ticket) is None)
    ticket2, _ = push_stream.issue_ticket('u')
    clock.advance(30001)
    check('票据过期失效', push_stream.consume_ticket(ticket2) is None)
    check('空票据返回 None', push_stream.consume_ticket('') is None)

    # ── 11b. 引擎优先：有引擎推送就不打 HTTP/查库，链路没数据才回落 ─────────
    link = StubEngineLink()
    hub2 = PushHub(CFG, strategy_service=StubStrategyService(), engine_link=link)
    engine = StubEngine()
    push_hub.gtrade_client.forward = engine
    conn2, err = hub2.open_connection('u', [DEPTH_TOPIC])
    check('引擎优先: 建流成功', err is None, err)

    # ① 引擎没数据 → 回落 HTTP（并且不报错）
    clock.advance(10)
    hub2._sampler_tick()
    check('引擎无数据回落 HTTP', engine.calls >= 1, engine.calls)
    check('引擎链路被询问过', link.depth_calls >= 1, link.depth_calls)

    # ② 引擎有数据 → 不再打 HTTP
    before = engine.calls
    link.depth = {'timestamp': 999, 'asks': [[11.0, 1.0]], 'bids': [[10.0, 1.0]]}
    clock.advance(250)
    push_hub.gtrade_client.forward = engine
    hub2._sampler_tick()
    check('引擎有数据不再打 HTTP', engine.calls == before, (before, engine.calls))

    # ③ 策略指标：引擎推过就用引擎的，不再查库
    svc = StubStrategyService([{'strat_name': 'db_only', 'status': 1}])
    link2 = StubEngineLink()
    link2.strategies = [{'strat_name': 'from_engine', 'status': 1, 'update_time': '2026-10-04 12:00:00',
                         'indicator': {}, 'param': {}}]
    hub3 = PushHub(CFG, strategy_service=svc, engine_link=link2)
    conn3, err = hub3.open_connection('u', ['strategies'])
    check('引擎优先: strategies 建流成功', err is None, err)
    clock.advance(10)
    hub3._sampler_tick()
    events = [e for e in drain(conn3) if e['event'] == 'strategies']
    pushed = events[-1]['data']['changed'] if events else []
    check('策略行来自引擎', any(r.get('strat_name') == 'from_engine' for r in pushed), pushed)
    check('策略行不查库', svc.calls == 0, svc.calls)

    # ── 11c. 通道归属 + 引擎直推扇出 + 通道内补查（rev4 §1/§4/§5）──────────
    link4 = StubEngineLink()
    hub4 = PushHub(CFG, strategy_service=StubStrategyService(), engine_link=link4)

    # ① 通道归属校验：quote 只装盘口，trade 不装盘口
    _, err = hub4.open_connection('u', ['strategies'], push_hub.CHANNEL_QUOTE)
    check('quote 通道拒收 strategies', err is not None and err[0] == 400, err)
    _, err = hub4.open_connection('u', [DEPTH_TOPIC], push_hub.CHANNEL_TRADE)
    check('trade 通道拒收 depth', err is not None and err[0] == 400, err)
    qc, err = hub4.open_connection('u', [DEPTH_TOPIC], push_hub.CHANNEL_QUOTE)
    check('quote 通道接受 depth', err is None, err)
    check('ready 帧带 channel', drain(qc)[0]['data']['channel'] == 'quote')

    # ② 引擎直推类 topic：不建轮询状态、不采样
    tc, err = hub4.open_connection('u', ['order', 'trade'], push_hub.CHANNEL_TRADE)
    check('trade 通道接受 order/trade', err is None, err)
    tracked = [st['topic'] for st in hub4.stats()['topics']]
    check('引擎直推 topic 不建采样状态',
          all(t not in ('order', 'trade') for t in tracked), tracked)
    drain(tc)

    # ③ 引擎帧扇出：只发给订阅了该 topic 的连接，并带 cursor（客户端实体守卫用）
    frame = {'type': 'event', 'channel': 'trade', 'topic': 'order', 'ts': 123,
             'cursor': '5001', 'status_id': 3, 'data': {'entno': '5001', 'status': '2'}}
    hub4.gtrade_client = push_hub.gtrade_client
    sent = hub4.publish_engine_frame(frame)
    events = drain(tc)
    check('引擎 order 帧扇出到 trade 连接', sent == 1 and events[-1]['event'] == 'order', (sent, events))
    check('扇出帧带 cursor/status_id', events[-1]['data']['cursor'] == '5001'
          and events[-1]['data']['status_id'] == 3, events[-1]['data'])
    check('未订阅该 topic 的连接收不到', drain(qc) == [], 'quote conn 不应收到 order')

    # ④ 引擎 snapshot（策略行）扇出成 strategies 事件
    tc2, _ = hub4.open_connection('u', ['strategies'], push_hub.CHANNEL_TRADE)
    drain(tc2)
    hub4.publish_engine_frame({'type': 'snapshot', 'channel': 'trade', 'topic': 'strategy',
                               'ts': 456, 'key': 's1',
                               'data': {'id': 's1', 'strat_name': 's1', 'status': 1}})
    ev2 = drain(tc2)
    check('策略快照扇出为 strategies 事件', bool(ev2) and ev2[-1]['event'] == 'strategies'
          and ev2[-1]['data']['changed'][0]['strat_name'] == 's1', ev2)
    check('策略快照沿用 full/changed/removed 契约',
          ev2[-1]['data']['full'] is False and ev2[-1]['data']['removed'] == [], ev2[-1]['data'])

    # ⑤ 通道内补查：结果沿连接队列回流（req_id 配对）
    link4.strategies = [{'strat_name': 'from_engine', 'status': 1}]
    ok, payload = hub4.query_connection(tc2, 'strategies', req_id=7)
    ev3 = drain(tc2)
    check('补查结果沿同连接回流', ok and ev3 and ev3[-1]['event'] == 'result', (ok, ev3))
    check('补查结果带 req_id 与数据', ev3[-1]['data']['req_id'] == 7
          and ev3[-1]['data']['count'] == 1, ev3[-1]['data'])

    # ⑥ 引擎链路不可用时补查返回错误（不抛异常）
    hub5 = PushHub(CFG, strategy_service=StubStrategyService(), engine_link=None)
    hub5._engine_link = push_hub._ENGINE_LINK_DISABLED
    ok, payload = hub5.query_connection(qc, 'orders', req_id=1)
    check('链路不可用时补查报错', ok is False and payload['error'] == 'engine_link_down', payload)

    # ── 11d. 事件流生成器：ts/seq、心跳事件帧、溢出 closing（rev4 §2/§5）────
    stream_hub = PushHub(CFG, strategy_service=StubStrategyService(), engine_link=StubEngineLink())
    conn6, _ = stream_hub.open_connection('u', [DEPTH_TOPIC], push_hub.CHANNEL_QUOTE)

    # ① 数据帧带头部 seq 与 ts（客户端据此算自身滞后）
    push_stream.now_ms = clock
    gen = push_stream._event_stream(conn6)
    first = next(gen)
    check('首帧为 retry 指令', first.startswith('retry:'), first[:20])
    payload = json.loads(next(gen).split('data: ')[1].strip())
    check('数据帧带 seq 与 ts', isinstance(payload.get('seq'), int) and isinstance(payload.get('ts'), int), payload)
    check('ready 帧带 channel', payload.get('channel') == 'quote', payload)
    gen.close()

    # ② 空闲超过心跳周期 -> 普通事件帧 ping（不是 SSE 注释行）
    conn7, _ = stream_hub.open_connection('u', [DEPTH_TOPIC], push_hub.CHANNEL_QUOTE)
    drain(conn7)
    gen7 = push_stream._event_stream(conn7)
    next(gen7)                 # retry
    clock.advance(12000)       # 生成器已记录 last_write，此时超过 ping_interval_quote_ms(10s)
    ping_frame = next(gen7)
    check('空闲后发 ping 事件帧', ping_frame.startswith('event: ping'), ping_frame[:40])
    check('ping 帧带 ts', isinstance(json.loads(ping_frame.split('data: ')[1].strip()).get('ts'), int))
    gen7.close()

    # ③ 队列溢出 -> 先发 closing{reason} 再断开（客户端据此区分主动断开与掉线）
    conn8, _ = stream_hub.open_connection('u', [DEPTH_TOPIC], push_hub.CHANNEL_QUOTE)
    drain(conn8)
    for i in range(conn8.queue.maxsize):
        conn8.enqueue({'event': 'depth', 'data': {'i': i}})
    conn8.enqueue({'event': 'depth', 'data': {'i': 'overflow'}})
    gen8 = push_stream._event_stream(conn8)
    next(gen8)                 # retry
    closing_frame = next(gen8)
    check('溢出时下发 closing', closing_frame.startswith('event: closing'), closing_frame[:40])
    closing_payload = json.loads(closing_frame.split('data: ')[1].strip())
    check('closing 带 reason', closing_payload.get('reason') == 'overflow', closing_payload)
    try:
        next(gen8)
        check('closing 后生成器结束', False, '仍有帧')
    except StopIteration:
        check('closing 后生成器结束', True)

    # ── 11. SSE 帧编码 ────────────────────────────────────────────────────
    frame = push_stream.encode_sse('depth', {'topic': DEPTH_TOPIC, 'data': {'bids': [[1.0, 2.0]]}})
    check('SSE 帧以 event 开头、空行结尾', frame.startswith('event: depth\ndata: ') and frame.endswith('\n\n'), frame)
    check('SSE 帧可解析回 JSON', push_stream.encode_sse('e', {'v': 1}).split('data: ')[1].strip() == '{"v":1}')

    print()
    if failures:
        print(f"FAILED ({len(failures)}): " + ', '.join(failures))
        return 1
    print("ALL PASS")
    return 0


if __name__ == '__main__':
    sys.exit(run())
