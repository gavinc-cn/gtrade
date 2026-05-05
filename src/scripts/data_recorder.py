import logging
import os.path
from pathlib import Path
from utils.log_mgr import init_logging
from recorder.kline import RecordKLine
from datetime import datetime
import yaml


class DataRecorder:

    def __init__(self, args):
        self.args = args
        self.data_cfg = args.data_repo_cfg

    def run(self):
        if 'kline_1M' in self.data_cfg:
            RecordKLine(self.args, self.data_cfg['kline_1M'], '1M').run()
        if 'kline_1H' in self.data_cfg:
            RecordKLine(self.args, self.data_cfg['kline_1H'], '1H').run()



class DataRecorderConfig:
    def __init__(self, yml_dict):
        data_recorder_cfg = yml_dict['data_recoder']
        # 账户密钥配置文件路径
        self.account_config = yml_dict['account_config']
        # 代理配置
        self.http_proxy = yml_dict.get('proxy', {}).get('http')
        # 数据输出目录
        self.out_dir = data_recorder_cfg['out_dir']
        self.start_day = data_recorder_cfg.get('start_day')
        self.end_day = data_recorder_cfg.get('end_day')
        self.n_days = data_recorder_cfg['n_days']
        self.data_repo_cfg = yml_dict['data_repo']

if __name__ == '__main__':
    with open('../../config/data_config.yml', encoding='utf-8') as fin:
        args = DataRecorderConfig(yaml.load(fin, Loader=yaml.SafeLoader))

    init_logging("log/data_recorder.log", file_levels=[logging.DEBUG, logging.INFO, logging.ERROR])

    DataRecorder(args).run()

