import logging
import yaml

from clients.okex_v5_client import OkexV5Client
from utils.client_utils import get_ac, rm0
from utils.log_mgr import init_logging
# from tools.do_not_delete.secret import account
from pprint import pprint

with open('../../../config/config.yml') as fin:
    gtrade_cfg = yaml.load(fin, Loader=yaml.SafeLoader)
http_proxy = gtrade_cfg['proxy']['http']
acc_cfg = gtrade_cfg['account_config']
with open(acc_cfg) as fin:
    acc_dic = yaml.load(fin, Loader=yaml.SafeLoader)
okx_account3_dummy = acc_dic['okx_account3_dummy']

init_logging("okex_client_test", level=logging.DEBUG)

# print(get_ac(account.okex_account3.test))
client = OkexV5Client(f"{okx_account3_dummy['key']} {okx_account3_dummy['passphrase']}", okx_account3_dummy['secret'], http_proxy)

symbol = 'BTC/USDT'

# print(client.get_markets('BTC/USDT'))
# print(client.get_depth(symbol))
res = client.get_ohlcv(symbol)
print(len(res))
print(res)
# print(client.get_balance())
# print(client.get_position(symbol))
print('>>>')
# print(client.create_limit_buy_order(symbol=symbol, price=0.1, amount=0.00001, test_mode=False))
# pprint(client.get_open_orders(symbol))
# pprint(client.get_order(symbol, '689513475818807296'))
# pprint(client.cancel_order(symbol, '689513475818807296'))
# pprint(client.cancel_open_orders(symbol))
# pprint(client.clean_position(symbol))


