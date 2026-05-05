#!/usr/bin/env python3
"""测试API端点"""
import sys
import os

# 添加项目路径
sys.path.insert(0, os.path.dirname(__file__))

from app import app
import json

def test_api():
    """测试API端点"""
    print("=== 测试API端点 ===\n")

    with app.test_client() as client:
        # 测试健康检查
        print("1. 测试健康检查 /api/health")
        response = client.get('/api/health')
        print(f"   状态码: {response.status_code}")
        print(f"   响应: {response.get_json()}\n")

        # 测试持仓API
        print("2. 测试持仓API /api/positions")
        response = client.get('/api/positions')
        print(f"   状态码: {response.status_code}")
        data = response.get_json()
        if data and data.get('success'):
            positions = data.get('data', {}).get('positions', [])
            total = data.get('data', {}).get('total', 0)
            print(f"   成功: {data.get('success')}")
            print(f"   总记录数: {total}")
            print(f"   返回记录数: {len(positions)}")
            if positions:
                print(f"   第一条记录: {json.dumps(positions[0], indent=2, ensure_ascii=False)}")
        else:
            print(f"   失败: {data}\n")

        # 测试组合持仓API
        print("\n3. 测试组合持仓API /api/portfolio_positions")
        response = client.get('/api/portfolio_positions')
        print(f"   状态码: {response.status_code}")
        data = response.get_json()
        if data and data.get('success'):
            positions = data.get('data', {}).get('positions', [])
            total = data.get('data', {}).get('total', 0)
            print(f"   成功: {data.get('success')}")
            print(f"   总记录数: {total}")
            print(f"   返回记录数: {len(positions)}")
            if positions:
                print(f"   第一条记录: {json.dumps(positions[0], indent=2, ensure_ascii=False)}")
        else:
            print(f"   失败: {data}\n")

    print("\n=== 测试完成 ===")

if __name__ == '__main__':
    test_api()
