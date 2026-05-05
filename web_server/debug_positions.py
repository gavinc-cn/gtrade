#!/usr/bin/env python3
"""调试持仓数据"""
import sys
import os

# 添加项目路径
sys.path.insert(0, os.path.dirname(__file__))

from database import get_db_manager
from strategy_service import get_strategy_service

def debug_positions():
    """调试持仓数据"""
    print("=== 调试持仓数据 ===\n")

    db = get_db_manager()
    service = get_strategy_service()

    # 测试数据库连接
    print("1. 测试数据库连接...")
    if db.test_connection():
        print("✓ 数据库连接成功\n")
    else:
        print("✗ 数据库连接失败\n")
        return

    # 查询position表
    print("2. 查询position表...")
    try:
        query = "SELECT COUNT(*) as count FROM position"
        result = db.execute_query(query, fetch_one=True, fetch_all=False)
        count = result.get('count', 0) if result else 0
        print(f"   position表记录数: {count}")

        if count > 0:
            query = "SELECT * FROM position LIMIT 3"
            positions = db.execute_query(query)
            print(f"\n   前3条记录:")
            for i, pos in enumerate(positions, 1):
                print(f"\n   记录 {i}:")
                print(f"     market: {pos.get('market')}")
                print(f"     account_id: {pos.get('account_id')}")
                print(f"     instrument: {pos.get('instrument')}")
                print(f"     pos_side: {pos.get('pos_side')}")
                print(f"     available: {pos.get('available')}")
                print(f"     avg_px: {pos.get('avg_px')}")
                print(f"     upl: {pos.get('upl')}")
    except Exception as e:
        print(f"   ✗ 查询失败: {e}\n")

    # 查询portfolio_position表
    print("\n3. 查询portfolio_position表...")
    try:
        query = "SELECT COUNT(*) as count FROM portfolio_position"
        result = db.execute_query(query, fetch_one=True, fetch_all=False)
        count = result.get('count', 0) if result else 0
        print(f"   portfolio_position表记录数: {count}")

        if count > 0:
            query = "SELECT * FROM portfolio_position LIMIT 3"
            positions = db.execute_query(query)
            print(f"\n   前3条记录:")
            for i, pos in enumerate(positions, 1):
                print(f"\n   记录 {i}:")
                print(f"     market: {pos.get('market')}")
                print(f"     account_id: {pos.get('account_id')}")
                print(f"     portfolio: {pos.get('portfolio')}")
                print(f"     instrument: {pos.get('instrument')}")
                print(f"     pos_side: {pos.get('pos_side')}")
                print(f"     available: {pos.get('available')}")
                print(f"     avg_px: {pos.get('avg_px')}")
                print(f"     upl: {pos.get('upl')}")
    except Exception as e:
        print(f"   ✗ 查询失败: {e}\n")

    # 测试API方法
    print("\n4. 测试API方法...")
    try:
        positions = service.get_all_positions(limit=5, offset=0)
        print(f"   get_all_positions返回: {len(positions)} 条记录")
        if positions:
            print(f"   第一条记录的字段: {list(positions[0].keys())}")

        portfolio_positions = service.get_all_portfolio_positions(limit=5, offset=0)
        print(f"   get_all_portfolio_positions返回: {len(portfolio_positions)} 条记录")
        if portfolio_positions:
            print(f"   第一条记录的字段: {list(portfolio_positions[0].keys())}")
    except Exception as e:
        print(f"   ✗ API方法调用失败: {e}\n")

    print("\n=== 调试完成 ===")

if __name__ == '__main__':
    debug_positions()
