"""
把交易机上的深度数据拷贝到本地
由本地的计划任务执行该脚本
"""

import os
import sys
import subprocess
import yaml
import logging
from pyzrt.log_mgr import init_logging, ensure_dir
from pyzrt.kronos import get_date_lst
from datetime import datetime, timedelta
from pathlib import PurePosixPath

def main():
    yesterday = (datetime.now() - timedelta(days=1)).strftime('%Y%m%d')
    start_day = cfg.start_day or yesterday
    end_day = cfg.end_day or yesterday

    for day in get_date_lst(start_day, end_day):
        logging.info(f"date={day}")
        src_path = str(PurePosixPath(cfg.src_dir) / day)
        dst_path = os.path.join(cfg.dst_dir, day)

        os.makedirs(cfg.dst_dir, exist_ok=True)

        copy_cmd = f"scp -i {cfg.identity_file} -P {cfg.port} -r {cfg.user}@{cfg.host}:{src_path} {dst_path}"
        logging.info(copy_cmd)
        subprocess.check_call(copy_cmd, shell=True)

        # del_cmd = f"ssh -i {cfg.identity_file} {cfg.user}@{cfg.host} -p {cfg.port} 'rm -rf {src_path}'"

        del_cmd = [
            "ssh",
            "-i", cfg.identity_file,
            "-p", str(cfg.port),
            f"{cfg.user}@{cfg.host}",
            f"rm -rf {src_path}"
        ]

        logging.info(del_cmd)
        subprocess.check_call(del_cmd)


if __name__ == "__main__":
    class CommandLineArgs:
        def __init__(self):
            import argparse
            parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
            parser.add_argument('--config', default=r'C:\MyApps\copy_depth_data.yml')
            cmd_args = parser.parse_args()

            with open(cmd_args.config, encoding='utf-8') as fin:
                cfg = yaml.load(fin, Loader=yaml.SafeLoader)

            self.user = cfg['user']
            self.host = cfg['host']
            self.port = cfg['port']
            self.identity_file = cfg['identity_file']
            self.src_dir = cfg['src_dir']
            self.dst_dir = cfg['dst_dir']
            self.start_day = cfg['start_day']
            self.end_day = cfg['end_day']

    cfg = CommandLineArgs()

    init_logging("log/copy_depth_data.log", file_levels=[logging.DEBUG, logging.INFO, logging.ERROR])

    try:
        main()
    except Exception() as e:
        logging.exception(e)
