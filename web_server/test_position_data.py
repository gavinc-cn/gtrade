#!/usr/bin/env python3
"""测试持仓数据"""
import pymysql
import os
import sys

# 数据库配置
DB_CONFIG = {
    'host': os.environ.get('DB_HOST', 'localhost'),
    'port': int(os.environ.get('DB_PORT', 3307)),
    'user': os.environ.get('DB_USER', 'gtrade'),
    'password': os.environ.get('DB_PASSWORD', 'gtrade123'),
    'database': 'gtrade',
    'charset': 'utf8mb4'
}

def test_position_data():
    """测试持仓数据"""
    try:
        # 连接数据库
        print(f"连接数据库: {DB_CONFIG['host']}:{DB_CONFIG['port']}")
        connection = pymysql.connect(**DB_CONFIG)
        cursor = connection.cursor(pymysql.cursors.DictCursor)

        # 检查position表
        print("\n=== 检查 position 表 ===")
        cursor.execute("SELECT COUNT(*) as count FROM position")
        result = cursor.fetchone()
        print(f"position表记录数: {result['count']}")

        if result['count'] > 0:
            cursor.execute("SELECT * FROM position LIMIT 5")
            positions = cursor.fetchall()
            print(f"\n前5条记录:")
            for i, pos in enumerate(positions, 1):
                print(f"\n记录 {i}:")
                for key, value in pos.items():
                    print(f"  {key}: {value}")

        # 检查portfolio_position表
        print("\n=== 检查 portfolio_position 表 ===")
        cursor.execute("SELECT COUNT(*) as count FROM portfolio_position")
        result = cursor.fetchone()
        print(f"portfolio_position表记录数: {result['count']}")

        if result['count'] > 0:
            cursor.execute("SELECT * FROM portfolio_position LIMIT 5")
            positions = cursor.fetchall()
            print(f"\n前5条记录:")
            for i, pos in enumerate(positions, 1):
                print(f"\n记录 {i}:")
                for key, value in pos.items():
                    print(f"  {key}: {value}")

        cursor.close()
        connection.close()
        print("\n数据库测试完成")

    except Exception as e:
        print(f"错误: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    test_position_data()
