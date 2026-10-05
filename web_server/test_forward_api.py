#!/usr/bin/env python3
"""转发路由冒烟测试：web_srv → gtrade HttpGateway（:46012）。

只覆盖无副作用路径，可在开发机直接反复执行：
1. 未带 token → 401（@token_required 生效）
2. GET /api/trade/depth 透传（200 或引擎无数据 404 均说明链路通）
3. POST /api/trade/place_order 无效参数 → 网关 400 原样透传
4. POST /api/strategy/start 不存在策略 → 网关 404 原样透传
5. DELETE /api/strategy/delete 不存在策略 → 幂等成功 200（引擎语义）原样透传
6. POST /api/strategy/batch 非法 operation → 网关 400 原样透传
7. 引擎不可达（注入假网关地址）→ 502 {"success": false, "message": "引擎不可达..."}

前置：gtrade 主程序在 :46012 运行（用例 2~6）；MySQL 可达（导入 app 依赖）。
运行：cd web_server && /opt/miniconda3/envs/my_pyenv/bin/python test_forward_api.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))

from app import app  # noqa: E402
import gtrade_client  # noqa: E402
from config import load_web_config  # noqa: E402

NO_SUCH = '__no_such_strategy__'


def _login(client):
    """登录拿 JWT，返回可复用的请求头。"""
    cfg = load_web_config()
    resp = client.post('/api/login', json={'username': cfg['user'], 'password': cfg['password']})
    assert resp.status_code == 200, f"登录失败: {resp.status_code} {resp.get_data(as_text=True)}"
    return {'Authorization': f"Bearer {resp.get_json()['token']}"}


def run():
    print("=== 转发路由冒烟测试 ===")
    failures = []

    with app.test_client() as client:
        # 1. 未带 token → 401
        resp = client.get('/api/trade/depth?inst_id=BTC-USDT-SWAP')
        print(f"1. 无 token 访问 → {resp.status_code} {resp.get_json()}")
        if resp.status_code != 401:
            failures.append('1 鉴权 401')

        headers = _login(client)

        # 2. 盘口透传（引擎正常时 200；无数据时 404，均含 success 字段）
        resp = client.get('/api/trade/depth?inst_id=BTC-USDT-SWAP', headers=headers)
        body = resp.get_json() or {}
        print(f"2. GET depth → {resp.status_code} {str(body)[:120]}")
        if resp.status_code not in (200, 404) or 'success' not in body:
            failures.append('2 depth 透传')

        # 3. 无效下单 → 网关 400 透传（不回显本地校验错误，形态与引擎一致）
        resp = client.post('/api/trade/place_order', json={'inst_id': 'X'}, headers=headers)
        body = resp.get_json() or {}
        print(f"3. 无效下单 → {resp.status_code} {str(body)[:120]}")
        if resp.status_code != 400 or body.get('success') is not False or 'error' not in body:
            failures.append('3 place_order 400 透传')

        # 4. 不存在策略启动 → 网关 404 透传
        resp = client.post(f'/api/strategy/start/{NO_SUCH}', headers=headers)
        body = resp.get_json() or {}
        print(f"4. 不存在策略启动 → {resp.status_code} {str(body)[:120]}")
        if resp.status_code != 404 or body.get('success') is not False:
            failures.append('4 strategy start 404 透传')

        # 5. 不存在策略删除 → 幂等成功 200（引擎 DeleteStrategy 对不存在策略也返回 success）
        resp = client.delete(f'/api/strategy/delete/{NO_SUCH}', headers=headers)
        body = resp.get_json() or {}
        print(f"5. 不存在策略删除 → {resp.status_code} {str(body)[:120]}")
        if resp.status_code not in (200, 404) or 'success' not in body:
            failures.append('5 strategy delete 透传')

        # 6. 批量操作非法 operation → 网关 400 透传（验证 batch body 转发）
        resp = client.post('/api/strategy/batch',
                           json={'operation': '__bogus__', 'strategy_ids': [NO_SUCH]},
                           headers=headers)
        body = resp.get_json() or {}
        print(f"6. 非法 batch → {resp.status_code} {str(body)[:120]}")
        if resp.status_code != 400 or body.get('success') is not False:
            failures.append('6 strategy batch 400 透传')

        # 7. 引擎不可达 → 502（注入假网关地址，仅本进程内生效）
        orig_gateway = gtrade_client._gateway
        gtrade_client._gateway = lambda: 'http://127.0.0.1:9'
        try:
            resp = client.get('/api/trade/depth?inst_id=BTC-USDT-SWAP', headers=headers)
            body = resp.get_json() or {}
            print(f"7. 引擎不可达 → {resp.status_code} {str(body)[:120]}")
            if resp.status_code != 502 or '引擎不可达' not in body.get('message', ''):
                failures.append('7 引擎不可达 502')
        finally:
            gtrade_client._gateway = orig_gateway

        # 8. 撤单 order_id 传字符串（19 位大整数）→ 转发层归一化为精确整数后送达引擎：
        #    引擎查不到该（不存在的）委托 → 500 order not found。
        #    若未归一化，网关的 IsInt64() 会直接判非法 → 400 Missing or invalid account_id/order_id。
        #    用例无副作用（委托号不存在，不会真的撤掉任何单）。
        resp = client.post('/api/trade/cancel_order',
                           json={'account_id': 'okx_account3_dummy', 'order_id': '1000000000000000001'},
                           headers=headers)
        body = resp.get_json() or {}
        print(f"8. 撤单字符串 entno → {resp.status_code} {str(body)[:120]}")
        if resp.status_code != 500 or 'order not found' not in body.get('error', ''):
            failures.append('8 撤单字符串 entno 归一化')

    if failures:
        print(f"\n失败项: {failures}")
        return 1
    print("\n全部通过")
    return 0


if __name__ == '__main__':
    sys.exit(run())
