#!/usr/bin/env python3
"""测试持仓核算API"""

import requests
import json


def test_reconciliation_api():
    """测试核算相关的API端点"""
    base_url = 'http://localhost:5000'

    print('测试持仓核算API...\n')

    # 测试1: 获取核算状态
    print('1. 测试 GET /api/reconciliation/status')
    try:
        response = requests.get(f'{base_url}/api/reconciliation/status')
        print(f'   状态码: {response.status_code}')
        data = response.json()
        print(f'   响应: {json.dumps(data, indent=2, ensure_ascii=False)}')

        if data.get('success'):
            print('   ✓ 获取核算状态成功')
        else:
            print('   ✗ 获取核算状态失败')
    except Exception as e:
        print(f'   ✗ 请求失败: {e}')

    print()

    # 测试2: 获取不一致的持仓
    print('2. 测试 GET /api/reconciliation/inconsistent')
    try:
        response = requests.get(f'{base_url}/api/reconciliation/inconsistent')
        print(f'   状态码: {response.status_code}')
        data = response.json()
        print(f'   响应: {json.dumps(data, indent=2, ensure_ascii=False)}')

        if data.get('success'):
            inconsistent_count = len(data.get('data', []))
            print(f'   ✓ 获取不一致持仓成功，共 {inconsistent_count} 个')

            if inconsistent_count > 0:
                print('   不一致的持仓:')
                for item in data['data'][:5]:  # 只显示前5个
                    print(f'     - {item["market"]}/{item["account_id"]}/{item["instrument"]}/{item["pos_side"]}')
                    print(f'       不一致类型: {item["mismatch_types"]}')
        else:
            print('   ✗ 获取不一致持仓失败')
    except Exception as e:
        print(f'   ✗ 请求失败: {e}')

    print('\n测试完成!')


if __name__ == '__main__':
    test_reconciliation_api()
