#!/usr/bin/env python3
"""
测试数据字典API接口
"""

import sys
import os

# 添加项目路径
sys.path.insert(0, os.path.dirname(__file__))

from app import app
import json

def test_dict_api():
    """测试 /api/dict 接口"""
    print("=" * 60)
    print("测试数据字典API接口")
    print("=" * 60)

    with app.test_client() as client:
        # 发送GET请求
        response = client.get('/api/dict')

        print(f"\n状态码: {response.status_code}")
        print(f"Content-Type: {response.content_type}")

        if response.status_code == 200:
            data = response.get_json()
            print(f"\n响应成功: {data.get('success')}")

            if data.get('success') and data.get('data'):
                dict_data = data['data']
                print(f"\n字典类型数量: {len(dict_data)}")
                print("\n包含的字典类型:")
                for key in dict_data.keys():
                    print(f"  - {key}")

                # 详细显示 EntrustStatus
                if 'EntrustStatus' in dict_data:
                    print("\n=== EntrustStatus (委托状态) ===")
                    entrust_status = dict_data['EntrustStatus']
                    print(f"名称: {entrust_status.get('name')}")
                    print(f"类型: {entrust_status.get('type')}")
                    print(f"\n状态映射:")
                    for field in entrust_status.get('fields', []):
                        print(f"  {field['val']} -> {field['name']}")

                # 详细显示 BuySellSide
                if 'BuySellSide' in dict_data:
                    print("\n=== BuySellSide (买卖标记) ===")
                    buy_sell_side = dict_data['BuySellSide']
                    print(f"名称: {buy_sell_side.get('name')}")
                    print(f"类型: {buy_sell_side.get('type')}")
                    print(f"\n买卖映射:")
                    for field in buy_sell_side.get('fields', []):
                        print(f"  {field['val']} -> {field['name']}")

                print("\n✓ API接口测试通过！")
                return True
            else:
                print(f"\n✗ API返回失败: {data.get('message')}")
                return False
        else:
            print(f"\n✗ HTTP请求失败: {response.status_code}")
            print(f"错误信息: {response.get_data(as_text=True)}")
            return False

if __name__ == '__main__':
    success = test_dict_api()
    sys.exit(0 if success else 1)
