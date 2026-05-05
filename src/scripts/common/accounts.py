import configparser
import os

keys = configparser.ConfigParser()
keys.read("/.my_keys/my_keys.ini")

okex_account3 = {
    "exchange": 'okex',
    'api_key': '{} {}'.format(keys['okex_account3_key']['api_key'], keys['okex_account3_key']['pass_phrase']),
    'api_secret': keys['okex_account3_key']['secret_key'],
    'bal_init': {},
}

okex_account4 = {
    "exchange": 'okex',
    'api_key': '{} {}'.format(keys['okex_account4_key']['api_key'], keys['okex_account4_key']['pass_phrase']),
    'api_secret': keys['okex_account4_key']['secret_key'],
    'bal_init': {},
}

okex_account5 = {
    "exchange": 'okex',
    'api_key': '{} {}'.format(keys['okex_account5_key']['api_key'], keys['okex_account5_key']['pass_phrase']),
    'api_secret': keys['okex_account5_key']['secret_key'],
    'bal_init': {},
}

huobi_account2 = {
    "exchange": 'huobi',
    'api_key': keys['huobi_account2_key']['api_key'],
    'api_secret': keys['huobi_account2_key']['secret_key'],
    'bal_init': {},
}

huobi_account4 = {
    "exchange": 'huobi',
    'api_key': keys['huobi_account4_key']['api_key'],
    'api_secret': keys['huobi_account4_key']['secret_key'],
    'bal_init': {},
}

binance_account1 = {
    "exchange": 'binance',
    'api_key': keys['binance_account1_key']['api_key'],
    'api_secret': keys['binance_account1_key']['secret_key'],
    'bal_init': {},
}
if __name__ == '__main__':
    print(okex_account4)
