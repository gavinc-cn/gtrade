#!/usr/bin/env python3
"""测试登录 API"""

import requests
import json

# 测试登录
def test_login():
    url = 'http://localhost:5000/api/login'
    data = {
        'username': 'admin',
        'password': 'admin'
    }

    print(f"测试登录 API: {url}")
    print(f"发送数据: {json.dumps(data, indent=2)}")

    try:
        response = requests.post(url, json=data)
        print(f"\n状态码: {response.status_code}")
        print(f"响应数据: {json.dumps(response.json(), indent=2)}")

        if response.status_code == 200 and response.json().get('success'):
            print("\n✓ 登录成功!")
            print(f"Token: {response.json().get('token')[:50]}...")
        else:
            print("\n✗ 登录失败!")

    except requests.exceptions.ConnectionError:
        print("\n✗ 无法连接到后端服务，请确保后端已启动 (python app.py)")
    except Exception as e:
        print(f"\n✗ 错误: {e}")

if __name__ == '__main__':
    test_login()
