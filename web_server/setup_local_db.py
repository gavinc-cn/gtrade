#!/usr/bin/env python3
"""
本地数据库设置脚本
1. 启动MySQL容器
2. 初始化数据库表结构
3. 插入示例数据
"""
import os
import sys
import time
import subprocess
import json
from datetime import datetime

def run_command(cmd, check=True, shell=False):
    """执行命令"""
    print(f"执行: {cmd if isinstance(cmd, str) else ' '.join(cmd)}")
    result = subprocess.run(
        cmd,
        shell=shell,
        capture_output=True,
        text=True
    )
    if check and result.returncode != 0:
        print(f"错误: {result.stderr}")
        sys.exit(1)
    return result

def start_mysql_container():
    """启动MySQL容器"""
    print("\n=== 步骤 1: 启动MySQL容器 ===")

    # 检查容器是否已运行
    result = run_command(
        ["docker", "ps", "--filter", "name=gtrade_mysql", "--format", "{{.Names}}"],
        check=False
    )

    if "gtrade_mysql" in result.stdout:
        print("✓ MySQL容器已在运行")
        return True

    # 检查容器是否存在但未运行
    result = run_command(
        ["docker", "ps", "-a", "--filter", "name=gtrade_mysql", "--format", "{{.Names}}"],
        check=False
    )

    if "gtrade_mysql" in result.stdout:
        print("启动现有MySQL容器...")
        run_command(["docker", "start", "gtrade_mysql"])
    else:
        print("使用docker-compose启动MySQL容器...")
        os.chdir("/opt/gtrade")
        run_command(["docker-compose", "-f", "docker-compose.app.yml", "up", "-d", "mysql"])

    # 等待MySQL就绪
    print("等待MySQL启动...")
    for i in range(30):
        result = run_command(
            ["docker", "exec", "gtrade_mysql", "mysqladmin", "ping", "-h", "localhost", "-uroot", "-pgtrade123"],
            check=False
        )
        if result.returncode == 0:
            print("✓ MySQL已就绪")
            time.sleep(2)  # 额外等待确保完全就绪
            return True
        time.sleep(1)

    print("✗ MySQL启动超时")
    return False

def init_database():
    """初始化数据库表结构"""
    print("\n=== 步骤 2: 初始化数据库表结构 ===")

    sql_file = "/opt/gtrade/deploy/sql/strat_info.sql"
    if not os.path.exists(sql_file):
        print(f"✗ SQL文件不存在: {sql_file}")
        return False

    print(f"执行SQL文件: {sql_file}")
    with open(sql_file, 'r', encoding='utf-8') as f:
        sql_content = f.read()

    # 使用docker exec执行SQL
    result = run_command(
        ["docker", "exec", "-i", "gtrade_mysql", "mysql", "-ugtrade", "-pgtrade123", "gtrade"],
        check=False
    )

    # 通过stdin传递SQL
    result = subprocess.run(
        ["docker", "exec", "-i", "gtrade_mysql", "mysql", "-ugtrade", "-pgtrade123", "gtrade"],
        input=sql_content,
        capture_output=True,
        text=True
    )

    if result.returncode == 0:
        print("✓ 数据库表结构初始化成功")
        return True
    else:
        # 忽略表已存在的错误
        if "already exists" in result.stderr or "Table" in result.stderr:
            print("✓ 数据库表已存在")
            return True
        print(f"✗ 初始化失败: {result.stderr}")
        return False

def insert_sample_data():
    """插入示例数据"""
    print("\n=== 步骤 3: 插入示例策略数据 ===")

    # 使用Python直接连接数据库
    try:
        import pymysql
    except ImportError:
        print("✗ pymysql未安装，跳过数据插入")
        print("  请运行: pip3 install --break-system-packages pymysql")
        return False

    try:
        # 连接数据库
        conn = pymysql.connect(
            host='localhost',
            port=3307,
            user='gtrade',
            password='gtrade123',
            database='gtrade',
            charset='utf8mb4'
        )
        cursor = conn.cursor()

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
                    'short_ma': 45250.5,
                    'long_ma': 45180.3,
                    'last_price': 45300.0
                },
                'status': 1
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
                'status': 0
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
                    'position': 0.05
                },
                'status': 1
            }
        ]

        now = datetime.now()
        inserted_count = 0

        for strat in sample_strategies:
            # 检查是否已存在
            cursor.execute(
                "SELECT id FROM strat_info WHERE strat_name = %s",
                (strat['strat_name'],)
            )
            if cursor.fetchone():
                print(f"  - 策略已存在: {strat['strat_name']}")
                continue

            # 插入策略
            cursor.execute(
                """
                INSERT INTO strat_info (strat_name, strat_template, param, indicator, status, create_time, update_time)
                VALUES (%s, %s, %s, %s, %s, %s, %s)
                """,
                (
                    strat['strat_name'],
                    strat['strat_template'],
                    json.dumps(strat['param'], ensure_ascii=False),
                    json.dumps(strat['indicator'], ensure_ascii=False),
                    strat['status'],
                    now,
                    now
                )
            )
            inserted_count += 1
            print(f"  ✓ 插入策略: {strat['strat_name']}")

        conn.commit()
        cursor.close()
        conn.close()

        print(f"\n✓ 成功插入 {inserted_count} 条策略数据")
        return True

    except Exception as e:
        print(f"✗ 插入数据失败: {e}")
        return False

def main():
    """主函数"""
    print("====================================")
    print("  GTrade 本地数据库设置")
    print("====================================")

    # 步骤1: 启动MySQL容器
    if not start_mysql_container():
        print("\n✗ MySQL容器启动失败")
        sys.exit(1)

    # 步骤2: 初始化数据库
    if not init_database():
        print("\n✗ 数据库初始化失败")
        sys.exit(1)

    # 步骤3: 插入示例数据
    insert_sample_data()

    print("\n====================================")
    print("  ✓ 数据库设置完成!")
    print("====================================")
    print("\n下一步:")
    print("1. 启动Web服务器:")
    print("   cd /opt/gtrade/web_server")
    print("   python3 app.py")
    print("")
    print("2. 启动前端:")
    print("   cd /opt/gtrade/web_client")
    print("   npm install")
    print("   npm run dev")
    print("")
    print("3. 访问: http://localhost:8080")

if __name__ == '__main__':
    main()
