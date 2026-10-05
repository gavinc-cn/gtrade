"""引擎侧链路（`/ws/engine`）集成测试：真实 asyncio WS server + 假引擎客户端。

覆盖（方案 rev4 §2–§5）：
  1. 握手鉴权：非 hello 首帧 / 密钥不符 / 超时 → closing 且断开
  2. 握手成功 → hello_ack + 服务端主动 subscribe
  3. 引擎推 event/snapshot → 链路水位（last_entno/last_tdno）与缓存更新
  4. 双向应用层心跳：ping→pong 且回带 rx（对端据此刻画积压）
  5. 补查（query/result）同步往返：Flask 线程等结果
  6. **重连即补查**：断开重连后服务端自动按游标补 trades/orders 并刷新已知在途委托

运行：`cd web_server && python3 test_ws_engine.py`（无需 MySQL，端口用 46019）
"""
import asyncio
import json
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import websockets

from ws_engine import EngineLink, DEFAULT_PAGE_ROWS

TEST_PORT = 46019
TEST_SECRET = 'unit-test-secret'

passed = 0
failed = []


def check(name, cond, detail=''):
    global passed
    if cond:
        passed += 1
        print(f'  [OK] {name}')
    else:
        failed.append(name)
        print(f'  [FAIL] {name} {detail}')


def make_link(**overrides):
    cfg = {
        'ws_port': TEST_PORT,
        'engine_secret': TEST_SECRET,
        'engine_ping_ms': 300,          # 测试用短心跳
        'engine_hello_timeout_s': 1.0,
        'engine_query_timeout_s': 0.6,
    }
    cfg.update(overrides)
    return EngineLink(cfg)


async def connect(secret=TEST_SECRET, send_hello=True):
    ws = await websockets.connect(f'ws://127.0.0.1:{TEST_PORT}/ws/engine')
    if send_hello:
        await ws.send(json.dumps({'type': 'hello', 'role': 'engine', 'secret': secret}))
    return ws


async def recv_json(ws, timeout=3.0):
    return json.loads(await asyncio.wait_for(ws.recv(), timeout=timeout))


async def wait_for_type(ws, ftype, timeout=3.0):
    """跳过心跳帧，等到指定类型的帧。"""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        frame = await recv_json(ws, timeout=max(0.1, deadline - time.monotonic()))
        if frame.get('type') == ftype:
            return frame
    raise TimeoutError(f'no {ftype} frame')


# ── 用例 ────────────────────────────────────────────────────────────────────
async def case_auth(link):
    print('1) 握手鉴权')
    # ① 非 hello 首帧
    ws = await websockets.connect(f'ws://127.0.0.1:{TEST_PORT}/ws/engine')
    await ws.send(json.dumps({'type': 'event', 'topic': 'order'}))
    try:
        frame = await recv_json(ws)
    except Exception:
        frame = {}
    check('非 hello 首帧被拒', frame.get('type') == 'closing', str(frame))
    await ws.close()

    # ② 密钥不符
    ws = await connect(secret='wrong')
    frame = await recv_json(ws)
    check('密钥不符被拒', frame.get('type') == 'closing' and frame.get('reason') == 'auth', str(frame))
    await ws.close()

    # ③ 首帧迟迟不发（走超时分支）
    ws = await websockets.connect(f'ws://127.0.0.1:{TEST_PORT}/ws/engine')
    frame = await recv_json(ws, timeout=3.0)
    check('hello 超时被拒', frame.get('type') == 'closing' and frame.get('reason') == 'hello_timeout', str(frame))
    await ws.close()

    # ④ 正确密钥
    ws = await connect()
    frame = await recv_json(ws)
    check('正确密钥 hello_ack', frame.get('type') == 'hello_ack', str(frame))
    sub = await wait_for_type(ws, 'subscribe')
    check('服务端主动声明订阅', sorted(sub.get('topics') or []) == ['quote', 'trade'], str(sub))
    await ws.close()


async def case_events(link):
    print('2) 事件与水位')
    ws = await connect()
    await wait_for_type(ws, 'hello_ack')
    await wait_for_type(ws, 'subscribe')

    await ws.send(json.dumps({'type': 'event', 'channel': 'trade', 'topic': 'order',
                              'ts': 1, 'cursor': '1001', 'status_id': 1,
                              'data': {'entno': '1001', 'status': '2', 'policy_no': 's1'}}))
    await ws.send(json.dumps({'type': 'event', 'channel': 'trade', 'topic': 'trade',
                              'ts': 2, 'cursor': '2001',
                              'data': {'tdno': '2001', 'ordno': '1001', 'td_px': 10.5}}))
    await ws.send(json.dumps({'type': 'snapshot', 'channel': 'trade', 'topic': 'position',
                              'ts': 3, 'key': 'okx|acc|BTC-USDT|c|l',
                              'data': {'instrument': 'BTC-USDT', 'available': 2}}))
    await asyncio.sleep(0.3)

    st = link.status()
    check('水位 last_entno', st['last_entno'] == 1001, str(st['last_entno']))
    check('水位 last_tdno', st['last_tdno'] == 2001, str(st['last_tdno']))
    check('委托缓存', st['orders_cached'] == 1, str(st['orders_cached']))
    check('成交缓存', st['trades_cached'] == 1, str(st['trades_cached']))
    check('持仓缓存', st['positions_cached'] == 1, str(st['positions_cached']))
    check('在途委托计数', st['open_orders'] == 1, str(st['open_orders']))

    # 终态委托应移出"在途"
    await ws.send(json.dumps({'type': 'event', 'channel': 'trade', 'topic': 'order',
                              'ts': 4, 'cursor': '1001', 'status_id': 2,
                              'data': {'entno': '1001', 'status': '4'}}))
    await asyncio.sleep(0.2)
    check('终态移出在途', link.status()['open_orders'] == 0, str(link.status()['open_orders']))
    await ws.close()


async def case_heartbeat(link):
    print('3) 应用层心跳')
    ws = await connect()
    await wait_for_type(ws, 'hello_ack')
    await wait_for_type(ws, 'subscribe')

    # 服务端心脏跳动（ping_ms=300）应在 1.5s 内到达，并带 rx
    ping = await wait_for_type(ws, 'ping', timeout=2.0)
    check('服务端发应用层 ping', 'rx' in ping, str(ping))

    # 引擎回 ping → 服务端回 pong，同样带 rx
    await ws.send(json.dumps({'type': 'ping', 'ts': 1, 'rx': 0}))
    pong = await wait_for_type(ws, 'pong')
    check('服务端回 pong 带 rx', 'rx' in pong, str(pong))
    await ws.close()


async def case_query(link):
    print('4) 补查往返（同步 API）')
    ws = await connect()
    await wait_for_type(ws, 'hello_ack')
    await wait_for_type(ws, 'subscribe')

    # 消费掉重连补查自动发出的 query，避免与本次断言混淆
    await asyncio.sleep(0.3)
    consumed = []
    try:
        while True:
            frame = await asyncio.wait_for(ws.recv(), timeout=0.2)
            consumed.append(json.loads(frame))
    except asyncio.TimeoutError:
        pass

    result_box = {}

    def run_query():
        rows, err = link.query('trades', cursor=2001, timeout=2.0)
        result_box['rows'] = rows
        result_box['err'] = err

    t = threading.Thread(target=run_query, daemon=True)
    t.start()
    # 等服务端的 query 帧并回 result
    deadline = time.monotonic() + 3
    qframe = None
    while time.monotonic() < deadline and qframe is None:
        try:
            frame = json.loads(await asyncio.wait_for(ws.recv(), timeout=0.5))
        except asyncio.TimeoutError:
            continue
        if frame.get('type') == 'query':
            qframe = frame
    check('服务端发出 query', qframe is not None and qframe.get('what') == 'trades', str(qframe))
    if qframe:
        check('query 带游标', qframe.get('cursor') == '2001', str(qframe))
        await ws.send(json.dumps({'type': 'result', 'req_id': qframe['req_id'], 'what': 'trades', 'ok': True,
                                  'count': 1, 'data': [{'tdno': '2002', 'td_px': 11.0}]}))
    t.join(timeout=3)
    check('同步补查拿到结果', result_box.get('rows') and result_box['rows'][0].get('tdno') == '2002',
          str(result_box))
    check('补查无错误', result_box.get('err') is None, str(result_box.get('err')))
    await ws.close()


async def case_reconnect_recovery(link):
    print('5) 重连即补查')
    # 先制造一段历史：一条成交 + 一条未终态委托
    ws = await connect()
    await wait_for_type(ws, 'hello_ack')
    await wait_for_type(ws, 'subscribe')
    await ws.send(json.dumps({'type': 'event', 'channel': 'trade', 'topic': 'trade', 'ts': 1,
                              'cursor': '3001', 'data': {'tdno': '3001'}}))
    await ws.send(json.dumps({'type': 'event', 'channel': 'trade', 'topic': 'order', 'ts': 2,
                              'cursor': '4001', 'data': {'entno': '4001', 'status': '2'}}))
    await asyncio.sleep(0.3)
    await ws.close()
    await asyncio.sleep(0.3)

    # 重连：服务端应自动补查 trades(tdno_gt=3001) 与 orders(entno_gt=4001) 与 by_entnos
    ws2 = await connect()
    await wait_for_type(ws2, 'hello_ack')
    await wait_for_type(ws2, 'subscribe')

    seen = []
    deadline = time.monotonic() + 4
    while time.monotonic() < deadline:
        try:
            frame = json.loads(await asyncio.wait_for(ws2.recv(), timeout=0.5))
        except asyncio.TimeoutError:
            break
        if frame.get('type') == 'query':
            seen.append(frame)
            # 回空结果让分页收敛
            await ws2.send(json.dumps({'type': 'result', 'req_id': frame['req_id'],
                                       'what': frame.get('what'), 'ok': True, 'count': 0, 'data': []}))

    kinds = {(f.get('what'), 'entnos' in f) for f in seen}
    check('重连后补查 trades(游标)', ('trades', False) in kinds, str(kinds))
    check('重连后补查 orders(游标)', ('orders', False) in kinds, str(kinds))
    check('重连后刷新在途委托(batch)', ('orders', True) in kinds, str(kinds))
    cursors = {f.get('what'): f.get('cursor') for f in seen if 'cursor' in f}
    check('成交游标=重连前水位', cursors.get('trades') == '3001', str(cursors))
    check('委托游标=重连前水位', cursors.get('orders') == '4001', str(cursors))
    await ws2.close()


async def main():
    link = make_link()
    link.start()
    time.sleep(0.3)
    try:
        await case_auth(link)
        await case_events(link)
        await case_heartbeat(link)
        await case_query(link)
        await case_reconnect_recovery(link)
    finally:
        link.stop()

    print(f'\n断言通过 {passed}，失败 {len(failed)}')
    if failed:
        print('失败项：' + ', '.join(failed))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(asyncio.run(main()))
