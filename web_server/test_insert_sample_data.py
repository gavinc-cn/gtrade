#!/usr/bin/env python3
"""
测试脚本：插入示例策略数据到数据库
"""
import json
from database import get_db_manager
from datetime import datetime

def insert_sample_strategies():
    """插入示例策略数据"""
    db = get_db_manager()

    # 示例策略数据
    sample_strategies = [
        {
            'strat_name': 'StratSMA_BTC_USDT_01',
            'strat_template': 'StratSMA',
            'param': {
                'short_period': 5,
                'long_period': 20,
                'max_pos': 0.001,
                'min_pos': -0.001,
                'instrument': 'BTC-USDT'
            },
            'indicator': {
                'net_pos': 0.0,
                'short_ma': 0.0,
                'long_ma': 0.0,
                'last_price': 0.0
            },
            'status': 1  # 运行中
        },
        {
            'strat_name': 'StratSMA_ETH_USDT_01',
            'strat_template': 'StratSMA',
            'param': {
                'short_period': 10,
                'long_period': 30,
                'max_pos': 0.01,
                'min_pos': -0.01,
                'instrument': 'ETH-USDT'
            },
            'indicator': {
                'net_pos': 0.005,
                'short_ma': 2500.5,
                'long_ma': 2480.3,
                'last_price': 2510.0
            },
            'status': 0  # 已停止
        },
        {
            'strat_name': 'FutureArbitrage_BTC_01',
            'strat_template': 'FutureArbitrageV3',
            'param': {
                'spread_threshold': 0.005,
                'position_size': 0.1,
                'instrument_spot': 'BTC-USDT',
                'instrument_future': 'BTC-USDT-SWAP'
            },
            'indicator': {
                'spread': 0.003,
                'spot_price': 45000.0,
                'future_price': 45135.0,
                'position': 0.0
            },
            'status': 1  # 运行中
        }
    ]

    now = datetime.now()

    for strat in sample_strategies:
        try:
            # 检查策略是否已存在
            check_query = "SELECT id FROM strat_info WHERE strat_name = %s"
            existing = db.execute_query(check_query, (strat['strat_name'],), fetch_one=True, fetch_all=False)

            if existing:
                print(f"策略 {strat['strat_name']} 已存在，跳过")
                continue

            # 插入策略
            insert_query = """
                INSERT INTO strat_info (strat_name, strat_template, param, indicator, status, create_time, update_time)
                VALUES (%s, %s, %s, %s, %s, %s, %s)
            """

            param_json = json.dumps(strat['param'], ensure_ascii=False)
            indicator_json = json.dumps(strat['indicator'], ensure_ascii=False)

            db.execute_query(
                insert_query,
                (
                    strat['strat_name'],
                    strat['strat_template'],
                    param_json,
                    indicator_json,
                    strat['status'],
                    now,
                    now
                ),
                fetch_one=False,
                fetch_all=False
            )

            print(f"✓ 成功插入策略: {strat['strat_name']}")

        except Exception as e:
            print(f"✗ 插入策略失败 {strat['strat_name']}: {e}")

if __name__ == '__main__':
    print("开始插入示例策略数据...")
    insert_sample_strategies()
    print("\n完成！")
