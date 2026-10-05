#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import requests
import json

# 测试API基础URL
BASE_URL = "http://localhost:5002"

def test_health():
    """测试健康检查接口"""
    print("=== 测试健康检查接口 ===")
    try:
        response = requests.get(f"{BASE_URL}/api/health")
        print(f"状态码: {response.status_code}")
        print(f"响应: {response.json()}")
        return response.status_code == 200
    except Exception as e:
        print(f"错误: {e}")
        return False

def test_get_strategies():
    """测试获取策略列表"""
    print("\n=== 测试获取策略列表 ===")
    try:
        response = requests.get(f"{BASE_URL}/api/strategies")
        print(f"状态码: {response.status_code}")
        print(f"响应: {response.json()}")
        return response.status_code == 200
    except Exception as e:
        print(f"错误: {e}")
        return False

def test_create_strategy():
    """测试创建策略"""
    print("\n=== 测试创建策略 ===")
    strategy_data = {
        "strat_name": "测试策略001",
        "strat_template": "demo_okx",
        "param": {
            "symbol": "BTC-USDT",
            "amount": 100,
            "price_threshold": 0.01
        },
        "indicator": {
            "sma_period": 20,
            "rsi_period": 14
        },
        "status": 0
    }
    
    try:
        response = requests.post(
            f"{BASE_URL}/api/strategies",
            headers={"Content-Type": "application/json"},
            json=strategy_data
        )
        print(f"状态码: {response.status_code}")
        print(f"响应: {response.json()}")
        
        if response.status_code == 201:
            return response.json().get('id')
        return None
    except Exception as e:
        print(f"错误: {e}")
        return None

def test_get_strategy_by_template(template):
    """测试根据模板获取策略"""
    print(f"\n=== 测试获取模板 {template} 的策略 ===")
    try:
        response = requests.get(f"{BASE_URL}/api/strategies/{template}")
        print(f"状态码: {response.status_code}")
        print(f"响应: {response.json()}")
        return response.status_code == 200
    except Exception as e:
        print(f"错误: {e}")
        return False

def test_update_strategy(strategy_id):
    """测试更新策略"""
    print(f"\n=== 测试更新策略 {strategy_id} ===")
    update_data = {
        "strat_name": "测试策略001_更新",
        "status": 1,
        "param": {
            "symbol": "ETH-USDT",
            "amount": 200,
            "price_threshold": 0.02
        }
    }
    
    try:
        response = requests.put(
            f"{BASE_URL}/api/strategies/{strategy_id}",
            headers={"Content-Type": "application/json"},
            json=update_data
        )
        print(f"状态码: {response.status_code}")
        print(f"响应: {response.json()}")
        return response.status_code == 200
    except Exception as e:
        print(f"错误: {e}")
        return False

def test_get_strategy_by_id(strategy_id):
    """测试根据ID获取策略"""
    print(f"\n=== 测试获取策略 {strategy_id} 详情 ===")
    try:
        response = requests.get(f"{BASE_URL}/api/strategies/{strategy_id}")
        print(f"状态码: {response.status_code}")
        print(f"响应: {response.json()}")
        return response.status_code == 200
    except Exception as e:
        print(f"错误: {e}")
        return False

def test_delete_strategy(strategy_id):
    """测试删除策略"""
    print(f"\n=== 测试删除策略 {strategy_id} ===")
    try:
        response = requests.delete(f"{BASE_URL}/api/strategies/{strategy_id}")
        print(f"状态码: {response.status_code}")
        print(f"响应: {response.json()}")
        return response.status_code == 200
    except Exception as e:
        print(f"错误: {e}")
        return False

def main():
    """主测试函数"""
    print("开始测试GTRADE后端API...")
    
    # 1. 测试健康检查
    if not test_health():
        print("健康检查失败，停止测试")
        return
    
    # 2. 测试获取策略列表
    test_get_strategies()
    
    # 3. 测试创建策略
    strategy_id = test_create_strategy()
    if not strategy_id:
        print("创建策略失败，跳过后续测试")
        return
    
    # 4. 测试根据模板获取策略
    test_get_strategy_by_template("demo_okx")
    
    # 5. 测试获取策略详情
    test_get_strategy_by_id(strategy_id)
    
    # 6. 测试更新策略
    test_update_strategy(strategy_id)
    
    # 7. 再次获取策略详情验证更新
    test_get_strategy_by_id(strategy_id)
    
    # 8. 测试删除策略
    test_delete_strategy(strategy_id)
    
    # 9. 最终检查策略列表
    test_get_strategies()
    
    print("\n=== API测试完成 ===")

if __name__ == "__main__":
    main()