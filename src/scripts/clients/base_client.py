from datetime import datetime
import traceback
import logging
from collections import Counter
from dateutil.parser import parse as dtparse
from utils.client_utils import dec_retry

logger = logging.getLogger()


class BaseClient(object):

    ORDER_TYPES = ['limit', 'market', 'limit-maker', 'stop-limit']
    TIME_MAP = {
        '1m': 60,
        '5m': 60 * 5,
        '15m': 60 * 15,
        '30m': 60 * 30,
        '1h': 3600,
        '4h': 3600 * 4,
        '12h': 3600 * 12,
        '1d': 3600 * 24,
        '1w': 3600 * 24 * 7,
    }
    API_CLOSE_TIME = []

    def __init__(self, api_key: str, api_secret: str, http_proxy: str) -> None:
        self.logger = logging.getLogger('trade')
        self.counter = Counter()
        self.http_proxy = http_proxy

    @dec_retry
    def get_ohlcv(self, symbol, timeframe, start, end):
        raise NotImplementedError()

    @dec_retry
    def get_markets(self, symbol=None):
        """
        :param symbol: 'EOS/USDT'
        :return:
        {
            'EOS/USDT': {
                'percentage': True, 'taker': 0.0015, 'maker': 0.001, 'precision': {'amount': 4, 'price': 3}, 'limits': {'amount': {'min': 0.1, 'max': None}, 'price': {'min': 0.001, 'max': None}, 'cost': {'min': 0.0001, 'max': None}}, 'id': 'EOS-USDT', 'symbol': 'EOS/USDT', 'base': 'EOS', 'quote': 'USDT', 'baseId': 'EOS', 'quoteId': 'USDT', 'info': {'base_currency': 'EOS', 'instrument_id': 'EOS-USDT', 'min_size': '0.1', 'quote_currency': 'USDT', 'size_increment': '0.0001', 'tick_size': '0.001'}, 'type': 'spot', 'spot': True, 'futures': False, 'swap': False, 'active': True
            },
            ...
        }
        """
        raise NotImplementedError()

    @dec_retry
    def get_ticker(self, symbol: str):
        raise NotImplementedError()

    @dec_retry
    def get_depth(self, symbol: str, limit: int = 10):
        raise NotImplementedError()

    @dec_retry
    def get_balance(self):
        """
        :return:
        {
            'info':  { ... },    // the original untouched non-parsed reply with details

            //-------------------------------------------------------------------------
            // indexed by availability of funds first, then by currency

            'free':  {           // money, available for trading, by currency
                'BTC': 321.00,   // floats...
                'USD': 123.00,
                ...
            },

            'used':  { ... },    // money on hold, locked, frozen, or pending, by currency

            'total': { ... },    // total (free + used), by currency

            //-------------------------------------------------------------------------
            // indexed by currency first, then by availability of funds

            'BTC':   {           // string, three-letter currency code, uppercase
                'free': 321.00   // float, money available for trading
                'used': 234.00,  // float, money on hold, locked, frozen or pending
                'total': 555.00, // float, total balance (free + used)
            },

            'USD':   {           // ...
                'free': 123.00   // ...
                'used': 456.00,
                'total': 579.00,
            },

            ...
        }
        """
        raise NotImplementedError()

    def create_order(self, symbol, type, side, amount, price=None, test_mode=True, **kwargs):
        raise NotImplementedError()

    @dec_retry
    def get_order(self, symbol, order_id):
        """
        :param symbol:
        :param order_id:
        :return: {
            'id': str(data['orderId']),
            'timestamp': timestamp,
            'datetime': self.iso8601(timestamp),
            'symbol': kwargs['symbol'],
            'type': self.type_trans[data['typeStr']],
            'side': self.side_trans[data['directionStr']],
            'price': utils.safe_float(data['price']),
            'amount': utils.safe_float(data['amount']),
            'filled': utils.safe_float(data['tradedAmount']),
            'remaining': utils.safe_float(data['amount']) - utils.safe_float(data['tradedAmount']),
            'status': self.status_trans[data['statusStr']],
            'fee': utils.safe_float(data['fee']),
            'info': data
        }
        """
        raise NotImplementedError()

    @dec_retry
    def cancel_order(self, symbol, order_id):
        raise NotImplementedError()

    @dec_retry
    def get_open_orders(self, symbol):
        raise NotImplementedError()

    @dec_retry
    def cancel_open_orders(self, symbol):
        raise NotImplementedError()

    def iso8601(self, timestamp=None):

        if timestamp is None or not isinstance(timestamp, int) or int(timestamp) < 0:
            return None

        try:
            utc = datetime.utcfromtimestamp(timestamp // 1000)
            return utc.strftime('%Y-%m-%dT%H:%M:%S.%f')[:-6] + "{:03d}".format(int(timestamp) % 1000) + 'Z'

        except (TypeError, OverflowError, OSError):
            return None

    def get_mid_px(self, symbol):
        try:
            depth = self.get_depth(symbol=symbol, limit=10)
            if 'mid' in depth:
                return depth['mid']
            return (depth['asks'][0][0] + depth['bids'][0][0]) / 2
        except Exception as e:
            logger.info(e)
            logger.debug(traceback.format_exc())

    def get_depth_extend(self, symbol, limit=10):
        depth = self.get_depth(symbol=symbol, limit=limit)
        depth['ask1'] = depth['asks'][0][0]
        depth['bid1'] = depth['bids'][0][0]
        depth['mid'] = depth['mid'] if 'mid' in depth else (
            depth['ask1'] + depth['bid1']) / 2
        return depth

    def get_24h_vol(self, symbol):
        ohlcv = self.get_ohlcv(symbol, timeframe='1h', since=None, limit=24)
        volume = sum([i[-1] for i in ohlcv])
        return volume

    def in_api_close_time(self):
        for time_range in self.API_CLOSE_TIME:
            if dtparse(time_range[0]).time() <= datetime.utcnow().time() <= dtparse(time_range[1]).time():
                return True
