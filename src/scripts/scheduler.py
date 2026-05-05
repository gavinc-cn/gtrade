#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import time
import logging
from apscheduler.schedulers.blocking import BlockingScheduler
from apscheduler.triggers.cron import CronTrigger
import subprocess
from datetime import datetime
from utils.log_mgr import init_logging
import yaml
import signal
from data_recorder import DataRecorderConfig, DataRecorder
from csv2mysql import Csv2MySqlConfig, Csv2MySQL


class TaskScheduler:
    def __init__(self):
        self.script_dir = os.path.dirname(os.path.abspath(__file__))
        
        # # 读取调度器配置
        # with open(config_path, encoding='utf-8') as fin:
        #     config = yaml.load(fin, Loader=yaml.SafeLoader)
        #     self.scheduler_config = config.get('scheduler', {})
        
        # 确保日志目录存在
        # log_dir = os.path.join(os.path.dirname(self.script_dir), '..', 'log')
        # os.makedirs(log_dir, exist_ok=True)
        
        logging.info("TaskScheduler initialized")
        # logging.info(f"Config loaded from: {config_path}")
    
    def run_data_recorder(self):
        """执行数据记录任务"""
        try:
            logging.info("Starting data_recorder task...")
            with open('../../config/data_config.yml', encoding='utf-8') as fin:
                args = DataRecorderConfig(yaml.load(fin, Loader=yaml.SafeLoader))
            DataRecorder(args).run()
            logging.info("data_recorder task completed successfully")
            return True
        except Exception as e:
            logging.exception(f"Error running data_recorder: {str(e)}")
        return False

    def run_csv2mysql(self):
        """执行CSV到MySQL导入任务"""
        try:
            logging.info("Starting csv2mysql task...")
            with open('../../config/data_config.yml', encoding='utf-8') as fin:
                args = Csv2MySqlConfig(yaml.load(fin, Loader=yaml.SafeLoader))
            Csv2MySQL(args).run()
            logging.info("csv2mysql task completed successfully")
            return True
        except Exception as e:
            logging.exception(f"Error running csv2mysql: {str(e)}")
            return False
    
    def start_scheduler(self):
        """启动定时调度器"""
        scheduler = BlockingScheduler()
        
        # 设置每天早上8点执行任务
        scheduler.add_job(
            func=self.run_data_recorder,
            trigger=CronTrigger(hour=8, minute=10),
            id='data_recorder',
            name='data_recorder'
        )
        scheduler.add_job(
            func=self.run_csv2mysql,
            trigger=CronTrigger(hour=8, minute=30),
            id='csv2mysql',
            name='csv2mysql'
        )
        
        logging.info("Scheduler started, waiting for scheduled tasks...")

        # 设置信号处理器以优雅关闭
        def signal_handler(signum, frame):
            logging.info("Received shutdown signal, stopping scheduler...")
            scheduler.shutdown()
            
        signal.signal(signal.SIGINT, signal_handler)
        signal.signal(signal.SIGTERM, signal_handler)
        
        try:
            # 启动调度器
            scheduler.start()
        except (KeyboardInterrupt, SystemExit):
            logging.info("Scheduler stopped.")
    
    def run_once(self):
        self.run_data_recorder()
        self.run_csv2mysql()


class SchedulerConfig:
    def __init__(self, yml_dict):
        scheduler_config = yml_dict['scheduler']
        self.run_once = scheduler_config['run_once']


if __name__ == '__main__':
    with open('../../config/data_config.yml', encoding='utf-8') as fin:
        args = SchedulerConfig(yaml.load(fin, Loader=yaml.SafeLoader))

    init_logging("log/scheduler.log", file_levels=[logging.DEBUG, logging.INFO, logging.ERROR])
    
    app = TaskScheduler()
    if args.run_once:
        app.run_once()
    else:
        app.start_scheduler()