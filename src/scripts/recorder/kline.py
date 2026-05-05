import logging
import os
import pandas as pd
import yaml
from datetime import datetime, timedelta, timezone
from clients.client_factory import get_client
from utils.log_mgr import init_logging
from utils.kronos import get_date_lst
from pprint import pprint
from utils.misc import ensure_dir


ACC_MAP = {
    'okx': 'okx_account3',
    'okx_dummy': 'okx_account3_dummy',
}


class RecordKLine:

    def __init__(self, cmd_args, data_cfg, time_frame):
        self.cmd_args = cmd_args
        self.data_cfg = data_cfg
        # 直接从data_config.yml获取代理和密钥配置, 不再依赖backtest_config.yml
        self.http_proxy = cmd_args.http_proxy
        self.acc_cfg_path = cmd_args.account_config

        with open(self.acc_cfg_path) as fin:
            self.acc_cfg_dic = yaml.load(fin, Loader=yaml.SafeLoader)

        self.client_map = {}
        self.base_dir = cmd_args.out_dir
        self.time_frame = time_frame


    def get_client(self, market):
        if market not in self.client_map:
            acc_cfg = self.acc_cfg_dic[ACC_MAP[market]]
            self.client_map[market] = get_client(
                market,
                f"{acc_cfg['key']} {acc_cfg['passphrase']}",
                acc_cfg['secret'], self.http_proxy)
        return self.client_map[market]

    def record_history(self, date):
        dt = datetime.strptime(date, '%Y%m%d').replace(tzinfo=timezone.utc)
        start_epoch = int(dt.timestamp() * 1000) - 1
        end_epoch = start_epoch + 86400 * 1000
        for market, inst_lst in self.data_cfg.items():
            inst_lst = inst_lst or []
            for inst in inst_lst:
                logging.debug(f'{market} {inst_lst}')
                fpath = os.path.join(self.base_dir, f'kline_{self.time_frame}', date, f'{inst}.{market}.{date}.csv')
                if os.path.isfile(fpath):
                    logging.info(f'already exist: {fpath}')
                    continue
                ensure_dir(fpath)
                logging.debug(f'req {market} {inst} kline range '
                             f'{datetime.fromtimestamp(start_epoch / 1000, tz=timezone.utc)} '
                             f'{datetime.fromtimestamp(end_epoch / 1000, tz=timezone.utc)}')
                kline = self.get_client(market).get_ohlcv(symbol=inst, timeframe=self.time_frame, start=start_epoch, end=end_epoch)
                logging.debug(f'rsp {market} {inst} kline range '
                             f'{datetime.fromtimestamp(kline[0][0] / 1000, tz=timezone.utc)} '
                             f'{datetime.fromtimestamp(kline[-1][0] / 1000, tz=timezone.utc)} sz={len(kline)}')
                df = pd.DataFrame(kline, columns=['ex_time', 'open', 'high', 'low', 'close', 'volume'])
                df['ex_time'] = df['ex_time'] * 1000000
                df.insert(loc=1, column='ex_time_h', value=pd.to_datetime(df['ex_time'] / 1000000, unit='ms', errors='coerce', utc=True).apply(lambda x: x.strftime('%Y-%m-%dT%H:%M:%S.%f')[:-3] if not pd.isna(x) else ''))

                df.to_csv(fpath, index=False)
                logging.info(f'write {fpath}')

    def run(self):
        now = datetime.now(timezone.utc)
        if self.cmd_args.start_day and self.cmd_args.end_day:
            date_lst = get_date_lst(self.cmd_args.start_day, self.cmd_args.end_day)
        else:
            date_lst = get_date_lst(now - timedelta(days=self.cmd_args.n_days), now - timedelta(days=1))

        for i in date_lst:
            self.record_history(i)


if __name__ == '__main__':
    init_logging("log/kline.log", file_levels=[logging.DEBUG, logging.INFO, logging.ERROR], console_level=logging.INFO)
    class Args:
        config = '../../../config/config.yml'
        out_dir = '/home/datasvc/test'
        start_day = '20230425'
        end_day = '20230425'
        n_days = 10
    cfg = {
        'okx': ['BTC-USDT', 'ETH-USDT'],
    }
    RecordKLine(Args(), cfg).run()