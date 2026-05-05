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

pd.set_option('display.max_rows', 20)
pd.set_option('display.max_columns', None)
pd.set_option('display.max_colwidth', 1024)
pd.set_option('display.width', None)

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
        config_path = os.path.join(os.path.dirname(__file__), '../../config/config.yml')
        with open(config_path, 'r', encoding='utf-8') as f:
            mysql_config_path = yaml.safe_load(f)['db_config']
        with open(mysql_config_path, 'r', encoding='utf-8') as f:
            return yaml.safe_load(f)['mysql']

    def parse_done_fname(self):
        pattern = r'^(\d{8})_(\d{4})_(\d{8})_(\d{4})_done_(\d{8})_(\d{4})\.csv$'
        match = regex.search(pattern, self.done_fname)
        if match:
            return match.group(1), match.group(3)


    def get_file_lst(self):
        filtered_file_lst = []
        for i in os.walk(self.depth1_dir):
            for file_name in i[2]:
                pattern = f'^{self.instrument}.{self.market}.(\d{{8}})$'
                # print(pattern, file_name)
                match = regex.match(pattern, file_name)
                if not match:
                    continue
                date = match.group(1)
                print(date, self.start_date, self.end_date)
                if date < self.start_date or date > self.end_date:
                    continue
                filtered_file_lst.append(os.path.join(i[0], file_name))
        return filtered_file_lst

    def read_depth1(self, file_path):
        depth1_df = pd.read_csv(file_path)
        print(depth1_df)

    # 计算每笔交易的仓位变化
    def calculate_position(self, row):
        # if row["bs_side"] == "b" and row["pos_side"] == "l":
        #     return row["done_amt"]
        # elif row["bs_side"] == "s" and row["pos_side"] == "l":
        #     return -row["done_amt"]

        if row["bs_side"] == "b":
            return row["done_amt"]
        elif row["bs_side"] == "s":
            return -row["done_amt"]
        else:
            return 0

    def process_done(self, fname):
        fpath = os.path.join(self.bt_dir, fname)
        done_df = pd.read_csv(fpath)
        done_df = done_df[['inst_id', 'bs_side', 'pos_side', 'done_px', 'done_amt', 'filled_time']]
        done_df["filled_time"] = pd.to_datetime(done_df["filled_time"], unit="ns")
        done_df["agg_time"] = done_df["filled_time"].dt.floor(self.agg_interval)
        
        if self.ignore_volume:
            # 忽略成交量模式：使用最大成交量
            print("使用最大成交量模式处理成交数据")
            # 需要跟踪当前仓位和可用资金来计算最大可能的成交量
            current_position = 0.0  # 当前仓位
            current_capital = self.initial_capital  # 当前可用资金
            
            # 按时间顺序处理每笔交易
            done_df_sorted = done_df.sort_values('filled_time').copy()
            max_volumes = []
            
            for idx, row in done_df_sorted.iterrows():
                if row['bs_side'] == 'b':  # 买入：消耗所有可用资金
                    max_volume = current_capital / row['done_px']
                    current_position += max_volume
                    current_capital = 0.0
                else:  # 卖出：消耗所有仓位
                    max_volume = current_position
                    current_capital += max_volume * row['done_px']
                    current_position = 0.0
                
                max_volumes.append(max_volume)
            
            done_df_sorted['max_done_amt'] = max_volumes
            done_df = done_df_sorted.copy()
            done_df["pos_change"] = done_df.apply(lambda row: row["max_done_amt"] if row["bs_side"] == "b" else -row["max_done_amt"], axis=1)
        else:
            # 正常模式：使用实际成交量
            done_df["pos_change"] = done_df.apply(self.calculate_position, axis=1)
        
        done_df["cap_change"] = done_df['pos_change'] * done_df['done_px']
        print('\ndone info:\n', done_df)

        sum_df = done_df.groupby('agg_time', as_index=False).agg(
            pos_change=('pos_change', 'sum'),  # 每小时仓位变动总和
            cap_change=('cap_change', 'sum')  # 每小时资金变动总和
        )
        # sum_df['bs_side'] = np.where(sum_df['pos_change'] >= 0, 'b', 's')
        sum_df['buy'] = np.where(sum_df['pos_change'] > 0, sum_df['cap_change'] / sum_df['pos_change'], np.nan)
        sum_df['sell'] = np.where(sum_df['pos_change'] < 0, sum_df['cap_change'] / sum_df['pos_change'], np.nan)


        sum_df.set_index('agg_time', inplace=True, drop=True)
        print('\nsum info:\n', sum_df)
        return sum_df

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

    def process_depth(self, file_path):
        df = pd.read_csv(file_path)
        df['price'] = (df['ask1_px'] + df['bid1_px']) / 2
        df.set_index('ex_time', inplace=True)
        df.index = pd.to_datetime(df.index, unit='ms')
        df = df[['price']]
        df = df.resample(self.agg_interval).ohlc()['price']['close']
        df = df.to_frame(name='close')
        # df['close_ret'] = df['close'].pct_change()
        print(df)
        return df

    def generate_report(self, merged_df):
        qs.extend_pandas()
        
        # 清理数据：处理NaN和无穷值
        strat_returns = merged_df['cap_ret'].fillna(0)  # 策略收益率 Series
        benchmark_returns = merged_df['close_cum_ret'].fillna(0)  # 基准收益率 Series
        
        # 替换无穷值
        strat_returns = strat_returns.replace([np.inf, -np.inf], 0)
        benchmark_returns = benchmark_returns.replace([np.inf, -np.inf], 0)
        
        # 确保数据是有限的
        strat_returns = strat_returns[np.isfinite(strat_returns)]
        benchmark_returns = benchmark_returns[np.isfinite(benchmark_returns)]
        
        print(f"策略收益率数据点数: {len(strat_returns)}")
        print(f"基准收益率数据点数: {len(benchmark_returns)}")
        
        if len(strat_returns) == 0:
            print("警告: 策略收益率数据为空，跳过报告生成")
            return

        qs.reports.html(
            strat_returns,
            benchmark=benchmark_returns,  # 可选参数
            output=os.path.join(self.bt_dir, f'{Path(self.done_fname).stem}.html')
            # compounded=False,
            # download_filename='backtest_report.html'
        )

    def _plot_results(self, merged_df):
        fig, ax1 = plt.subplots(figsize=(12, 6))

        # 左轴绘制close和buy
        ax1.plot(merged_df.index, merged_df['close'], '--', label='Close Price')
        ax1.scatter(merged_df.index, merged_df['buy'], c='r', marker='o', label='Buy Signals')
        ax1.scatter(merged_df.index, merged_df['sell'], c='g', marker='o', label='Sell Signals')
        ax1.set_xlabel('Index')
        ax1.set_ylabel('Price', color='b')
        ax1.tick_params(axis='y', labelcolor='b')

        # 创建右轴
        ax2 = ax1.twinx()
        ax2.plot(merged_df.index, merged_df['close_cum_ret'], label='Benchmark Return')
        # ax2.plot(merged_df.index, merged_df['hold_ret'], label='Hold Return')
        ax2.plot(merged_df.index, merged_df['cap_ret'], label='Strategy Return')
        ax2.set_ylabel('Returns', color='k')
        ax2.tick_params(axis='y', labelcolor='k')

        # 合并图例
        lines1, labels1 = ax1.get_legend_handles_labels()
        lines2, labels2 = ax2.get_legend_handles_labels()
        ax1.legend(lines1 + lines2, labels1 + labels2, loc='upper left')

        plt.title('Trading Performance Analysis')
        # plt.tight_layout()

        fig.savefig(os.path.join(self.bt_dir, f'{Path(self.done_fname).stem}.png'), dpi=300, bbox_inches='tight')
        plt.show()

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

    def show_dtale(self, df):
        self.last_dtale_port += 1
        print(dtale.show(df, host=self.dtale_host, port=self.last_dtale_port).main_url())
        # webbrowser.open(url)

    def run(self):
        try:
            # 获取基准数据（depth、kline或hourly_open）
            price_df = self.get_return_data()
            print('\nbenchmark:\n', price_df)
            self.show_dtale(price_df)

            # 计算累积返回率
            price_df['close_cum_ret'] = price_df['close'] / price_df['close'].iloc[0] - 1

            done_df = self.process_done(self.done_fname)

            merged_df = price_df.merge(done_df, left_index=True, right_index=True, how='outer')
            # 策略持仓
            merged_df['hold'] = merged_df['pos_change'].fillna(0).cumsum()
            # 持仓成本
            merged_df['hold_cost'] = merged_df['cap_change'].fillna(0).cumsum()
            # 持仓价值
            merged_df['hold_val'] = merged_df['hold'] * merged_df['close']
            # 持仓收益
            merged_df['hold_ret'] = merged_df['hold_val'] / merged_df['hold_cost'] - 1
            # 总价值, 剩余资金+持仓价值
            merged_df['cap'] = self.initial_capital - merged_df['hold_cost'] + merged_df['hold_val']
            # 总收益率
            merged_df['cap_ret'] = (merged_df['cap'] / self.initial_capital - 1).fillna(0)
            print('\nmerged table:\n', merged_df)
            self.show_dtale(merged_df)

            print('plotting...')
            self._plot_results(merged_df)
            print('generate report')
            self.generate_report(merged_df)
            print('done')
        except Exception:
            traceback.print_exc()
        finally:
            try:
                while True:
                    time.sleep(1)
            except KeyboardInterrupt:
                print("\n👋 程序已退出，DTale服务已关闭")
                dtale.global_state.cleanup()

if __name__ == '__main__':
    # 示例使用：
    # processor = DoneProcessor(return_source='depth')
    # processor = DoneProcessor(return_source='kline')
    # processor = DoneProcessor(done_fname='20240709_0000_20250709_0000_done_20250728_2341.csv', return_source='h', initial_capital=1000000)
    # processor = DoneProcessor(done_fname='20250609_0000_20250709_0000_done_20250730_2157.csv', return_source='h', initial_capital=100)
    # 正常模式（使用实际成交量）
    # processor = DoneProcessor(done_fname='20250609_0000_20250709_0000_done_20250730_2219.csv', return_source='h', initial_capital=100, ignore_volume=False)
    # 忽略成交量模式（买入时消耗所有资金，卖出时消耗所有仓位）
    processor = DoneProcessor(done_fname='20250925_0000_20251012_0000_done_20251023_2250.csv', return_source='h', initial_capital=1000000, ignore_volume=True)
    processor.run()
