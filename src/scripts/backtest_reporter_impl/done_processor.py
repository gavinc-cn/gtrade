import os
import time
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import regex
import dtale
import quantstats as qs
import webbrowser
import sqlalchemy
from sqlalchemy import text
import yaml
import traceback
from pathlib import Path

GIGA = 1000 * 1000 * 1000

class DoneProcessor:
    def __init__(self, done_fname, return_source, initial_capital, ignore_volume=False, market='okx'):
        self.depth1_dir = r'D:\DataRepository\depth1'
        self.bt_dir = r'D:\DataRepository\backtest'
        self.done_fname = done_fname
        self.initial_capital = initial_capital
        self.market = market
        self.instrument = 'BTC-USDT'
        self.agg_interval = '1min'
        self.return_source = return_source  # 'depth', 'kline', or 'hourly_open'
        self.ignore_volume = ignore_volume  # 是否忽略成交量，使用最大成交量

        # 从配置文件读取MySQL连接信息
        self.mysql_config = self.load_mysql_config()

        self.start_date, self.end_date = self.parse_done_fname()

        self.dtale_host = 'localhost'
        self.last_dtale_port = 40003

    def load_mysql_config(self):
        """从 data_config.yml 读取MySQL连接配置"""
        with open('../../config/config.yml', 'r', encoding='utf-8') as f:
            mysql_config_path = yaml.safe_load(f)['db_config']
        with open(mysql_config_path, 'r', encoding='utf-8') as f:
            return yaml.safe_load(f)['mysql']

    def parse_done_fname(self):
        pattern = r'^(\d{8})_(\d{4})_(\d{8})_(\d{4})_done_(\d{8})_(\d{4})\.csv$'
        match = regex.search(pattern, self.done_fname)
        if match:
            return match.group(1), match.group(3)

    def read_depth1(self, file_path):
        depth1_df = pd.read_csv(file_path)
        print(depth1_df)

    # 计算每笔交易的仓位变化
    def calculate_position(self, row):
        # if row["td_side"] == "b" and row["pos_side"] == "l":
        #     return row["td_qty"]
        # elif row["td_side"] == "s" and row["pos_side"] == "l":
        #     return -row["td_qty"]

        if row["td_side"] == "b":
            return row["td_qty"]
        elif row["td_side"] == "s":
            return -row["td_qty"]
        else:
            return 0

    def get_mysql_connection(self):
        """创建MySQL连接"""
        connection_string = f"mysql+pymysql://{self.mysql_config['user']}:{self.mysql_config['pwd']}@{self.mysql_config['host']}:{self.mysql_config['port']}/gtrade"
        engine = sqlalchemy.create_engine(connection_string, pool_pre_ping=True, future=True)
        return engine

    def read_kline_from_mysql(self, kline_type='1M', price_field='close'):
        """从MySQL读取K线数据

        Args:
            kline_type (str): K线类型，'1M'(分钟线)或'1H'(小时线)
            price_field (str): 价格字段，'close'(收盘价)或'open'(开盘价)
        """
        engine = self.get_mysql_connection()

        # 转换日期格式为时间戳范围
        start_timestamp = pd.to_datetime(self.start_date, format='%Y%m%d').timestamp() * GIGA
        end_timestamp = (pd.to_datetime(self.end_date, format='%Y%m%d').timestamp() + 24 * 3600) * GIGA  # 加一天

        query = f"""
        SELECT ex_time, {price_field}
        FROM kline_{kline_type}
        WHERE market = '{self.market}'
        AND instrument = '{self.instrument}'
        AND ex_time >= {int(start_timestamp)}
        AND ex_time <= {int(end_timestamp)}
        ORDER BY ex_time
        """
        print(f'sql={query}')
        # 使用连接对象读取数据，使用text()包装SQL字符串
        with engine.begin() as conn:
            df = pd.read_sql(text(query), conn)

        if df.empty:
            raise RuntimeError(f"MySQL中没有找到匹配的{kline_type}K线数据: {self.market}-{self.instrument}")

        df.set_index('ex_time', inplace=True)
        df.index = pd.to_datetime(df.index, unit='ns')

        # 如果是开盘价，重命名为close以保持一致性
        if price_field == 'open':
            df.rename(columns={'open': 'close'}, inplace=True)

        # 按指定间隔重采样
        resample_needed = False
        if kline_type == '1M' and self.agg_interval != '1min':
            resample_needed = True
            resample_method = 'last'
        elif kline_type == '1H' and self.agg_interval not in ['1H', '1h']:
            resample_needed = True
            resample_method = 'first' if price_field == 'open' else 'last'

        if resample_needed:
            df = df.resample(self.agg_interval).agg(resample_method).dropna()

        print(f"从MySQL加载了 {len(df)} 条{kline_type}K线{price_field}数据")

        engine.dispose()
        return df

    def get_return_data(self):
        """根据配置获取收益率计算的基准数据"""
        if self.return_source == 'm':
            print(f"使用分钟K线收盘价作为收益率计算基准")
            return self.read_kline_from_mysql(kline_type='1M', price_field='close')

        elif self.return_source == 'h':
            print(f"使用小时K线收盘价作为收益率计算基准")
            return self.read_kline_from_mysql(kline_type='1H', price_field='close')

        elif self.return_source == 't':
            print(f"使用depth数据作为收益率计算基准")
            depth_df_lst = []
            file_list = self.get_file_lst()
            if not file_list:
                raise RuntimeError(f"没有找到匹配的depth文件: {self.market}-{self.instrument}")

            for file_path in file_list:
                print(file_path)
                depth_df_lst.append(self.process_depth(file_path))
            return pd.concat(depth_df_lst, axis=0)

        else:
            raise ValueError(f"不支持的return_source: {self.return_source}, 只支持'm', 'h'或't'")