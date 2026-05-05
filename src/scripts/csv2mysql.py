import os
import logging
from datetime import datetime, timedelta, timezone
from utils.kronos import get_date_lst
from utils.mysql_client import MySQLPandasClient
import pandas as pd
from pathlib import Path
from utils.log_mgr import init_logging
from utils.misc import set_pandas_display
import yaml


class Csv2MySQL:
    def __init__(self, args):
        self.data_cfg = args.data_repo_cfg
        self.args = args
        self.mysql_client = MySQLPandasClient(
            host=args.host,
            port=args.port,
            user=args.user,
            password=args.pwd,
            database=args.database
        )

    def get_date_lst(self):
        now = datetime.now(timezone.utc)
        if self.args.start_day and self.args.end_day:
            return get_date_lst(self.args.start_day, self.args.end_day)
        else:
            return get_date_lst(now - timedelta(days=self.args.n_days), now - timedelta(days=1))

    def run(self):
        date_lst = self.get_date_lst()
        try:
            for repo_name, repo_info in self.data_cfg.items():
                for market, inst_lst in repo_info.items():
                    inst_lst = inst_lst or []
                    for inst in inst_lst:
                        for date in date_lst:
                            fpath = os.path.join(self.args.csv_dir, repo_name, date, f'{inst}.{market}.{date}.csv')
                            if not os.path.isfile(fpath):
                                logging.info(f'{fpath} not found')
                                continue
                            logging.info(f'process {fpath}')
                            df = pd.read_csv(fpath)
                            if 'ex_time_h' in df.columns:
                                df['ex_time_h'] = pd.to_datetime(df['ex_time_h'], errors='coerce', utc=True)
                            # 确保 market 和 instrument 列存在并位于前两列
                            if 'market' not in df.columns:
                                df['market'] = market
                            if 'instrument' not in df.columns:
                                df['instrument'] = inst
                            
                            # 重新排列列顺序，将 market 和 instrument 放到前面
                            cols = df.columns.tolist()
                            cols.remove('market')
                            cols.remove('instrument')
                            df = df[['market', 'instrument'] + cols]
                            self.mysql_client.write_dataframe(
                                df=df, 
                                table_name=f'{repo_name}_{market}_{inst}',
                                primary_keys=['market', 'instrument', 'ex_time']
                            )
                            logging.info(f'{fpath} written to mysql')
        finally:
            # 确保连接被正确关闭
            self.mysql_client.close()


class Csv2MySqlConfig:
    def __init__(self, yml_dict):
        csv2mysql_cfg = yml_dict['csv2mysql']
        self.csv_dir = csv2mysql_cfg['csv_dir']
        self.n_days = csv2mysql_cfg['n_days']
        self.start_day = csv2mysql_cfg.get('start_day')
        self.end_day = csv2mysql_cfg.get('end_day')
        self.database = csv2mysql_cfg['database']

        # 从db_config指向的密钥文件中读取MySQL连接配置
        with open(yml_dict['db_config'], encoding='utf-8') as fin:
            mysql_cfg = yaml.load(fin, Loader=yaml.SafeLoader)['mysql']
        self.host = mysql_cfg['host']
        self.port = mysql_cfg['port']
        self.user = mysql_cfg['user']
        self.pwd = mysql_cfg['pwd']

        self.data_repo_cfg = yml_dict['data_repo']


if __name__ == '__main__':
    with open('../../config/data_config.yml', encoding='utf-8') as fin:
        cfg = Csv2MySqlConfig(yaml.load(fin, Loader=yaml.SafeLoader))

    init_logging("log/csv2mysql.log", file_levels=[logging.DEBUG, logging.INFO, logging.ERROR])
    set_pandas_display()

    Csv2MySQL(cfg).run()