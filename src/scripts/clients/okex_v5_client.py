import requests
import time
import hashlib
import hmac
import json
import base64
import logging
import traceback
import copy

from datetime import datetime, timezone
from utils.client_utils import dec_retry, get_timestamp13, get_timestamp19, safe_float, float2str, dec_create_check, dec_depth_check, dec_order_check, dec_retry_new, assert_eq
from clients.base_client import BaseClient
from utils import exceptions
from common.constants import BUY, SELL, OPEN_LONG, OPEN_SHORT, CLOSE_LONG, CLOSE_SHORT, LIMIT, LIMIT_MAKER, STOP_LIMIT, EXTENDED_SIDE_VALUES, OPEN, CLOSED, CANCELED, OrderSideValidValues
import urllib.parse
from pprint import pprint


EPSILON = 1e-6


class OkexV5Client(BaseClient):

    ORDER_TYPE_MAP = {
        'limit': 'limit',
        'limit-maker': 'post_only',
        'post_only': 'limit-maker'
    }
    STATUS_MAP = {
        'canceled': 'canceled',
        'live': 'open',
        'partially_filled': 'open',
        'filled': 'closed',
        'mmp_canceled': 'canceled',
    }
    TIMEFRAME_MAP = {
        'M': 'm',
        'H': 'H',
        'd': 'D',
        'w': 'W',
        'm': 'M',
        'Y': 'Y',
    }
    TIMEFRAME_VALUE = {
        '1M': 60,
        '3M': 60 * 3,
        '5M': 60 * 5,
        '15M': 60 * 15,
        '30M': 60 * 30,
        '1H': 3600,
        '2H': 3600 * 2,
        '4H': 3600 * 4,
        '6H': 3600 * 6,
        '12H': 3600 * 12,
        '1d': 3600 * 24,
        '1w': 3600 * 24 * 7,
    }
    API_CLOSE_TIME = [
        ['00:00:00', '00:03:00'],
        ['08:00:00', '08:03:00'],
        ['16:00:00', '16:03:00'],
    ]

    def __init__(self, api_key: str, api_secret: str, http_proxy: str) -> None:
        super().__init__(api_key, api_secret, http_proxy)
        self.api_key, self.pass_phrase = api_key.split() if api_key else ('', '')
        self.api_secret = api_secret
        self.timeout = 5
        self.host = 'https://www.okx.com'
        self.logger = logging.getLogger('root')
        self.last_position = {}
        self.last_balance = {}

    @staticmethod
    def get_ex_symbol(symbol):
        return '-'.join(symbol.upper().split('/'))

    @staticmethod
    def get_symbol(symbol):
        return '/'.join(symbol.upper().split('-'))

    def get_ex_time_frame(self, timeframe):
        return timeframe[:-1] + self.TIMEFRAME_MAP[timeframe[-1]]

    def get_time_range(self, timeframe, since, to, limit):
        now = int(time.time())
        jump_seconds = limit * self.TIMEFRAME_VALUE[timeframe]
        if since and to:
            start = since
            end = to
        elif since and limit:
            start = since
            end = since + jump_seconds
        elif to and limit:
            start = to - jump_seconds
            end = to
        elif limit:
            start = now - jump_seconds
            end = now
        else:
            start = end = None
        return start, end

    @dec_retry
    def get_markets(self, symbol=None):
        endpoint = '/api/v5/public/instruments'
        params = {
            'instType': 'SPOT'
        }
        response = self.http_request(endpoint, params=params)
        data = {self.get_symbol(i['instId']): i for i in response['data']}
        if not symbol:
            symbol_lst = data.keys()
        elif isinstance(symbol, str):
            symbol_lst = [symbol]
        else:
            symbol_lst = symbol

        result = {}
        for i in symbol_lst:
            content = {
                'px_sig': data[i]['tickSz'],
                'qty_sig': data[i]['lotSz'],
                'qty_min_base': data[i]['minSz'],
                'qty_min_quote': 0,
            }
            result[i] = content
        return result

    @dec_retry
    def get_ticker(self, symbol):
        return

    @dec_retry
    def get_depth(self, symbol, limit=10, type=''):
        ex_symbol = self.get_ex_symbol(symbol)
        endpoint = f'/api/v5/market/books?instId={ex_symbol}'
        params = {
            'sz': limit,
        }
        result = self.http_request(endpoint, params)
        data = result['data'][0]
        timestamp = get_timestamp19()
        asks = [[float(i[0]), float(i[1])]
                for i in data['asks']]
        bids = [[float(i[0]), float(i[1])]
                for i in data['bids']]
        return {
            'asks': sorted(asks, key=lambda x: x[0], reverse=False)[:int(limit)],
            'bids': sorted(bids, key=lambda x: x[0], reverse=True)[:int(limit)],
            'timestamp': timestamp,
            'datetime': self.iso8601(timestamp)
        }

    @dec_retry
    def get_ohlcv(self, symbol, timeframe, start, end):
        endpoint = f'/api/v5/market/history-candles'
        limit = 100
        params = {
            'instId': self.get_ex_symbol(symbol),
            'bar': self.get_ex_time_frame(timeframe),
            'limit': limit,
        }
        ret_len = limit
        all_kline = []
        retry_interval = 0
        while ret_len == limit:
            cur_end = min(start + limit * self.TIMEFRAME_VALUE[timeframe] * 1000, end)
            params['before'] = start
            params['after'] = cur_end
            try:
                response = self.http_request(endpoint, params)
                data = response['data']
            except Exception as e:
                retry_interval += 1
                logging.warning(f'{e}, retry after {retry_interval}s')
                time.sleep(min(retry_interval, 60))
                continue
            try:
                data.reverse()
                # print(data)
                # print(datetime.fromtimestamp(start / 1000, tz=timezone.utc),
                #       datetime.fromtimestamp(cur_end / 1000, tz=timezone.utc),
                #       datetime.fromtimestamp(int(data[0][0]) / 1000, tz=timezone.utc),
                #       datetime.fromtimestamp(int(data[-1][0]) / 1000, tz=timezone.utc), len(data))
                ret_len = len(data)
                start = cur_end
                all_kline.extend(data)
            except Exception as e:
                raise type(e)(f'{e}\n{response}')
        return sorted([[int(i[0]), float(i[1]), float(i[2]), float(
                i[3]), float(i[4]), float(i[5])] for i in all_kline], key=lambda x: x[0])

    @dec_retry
    def get_balance(self, symbol=None):
        endpoint = f'/api/v5/account/balance'
        params = {}
        if symbol:
            params['ccy'] = symbol

        try:
            response = self.signed_request(endpoint, params=params, method='GET')
        except exceptions.ApiClosedError as e:
            if self.in_api_close_time():
                self.logger.warning(f'api closed, use last balance: {self.last_balance}')
                return self.last_balance
            else:
                raise e
        try:
            data = response['data'][0]['details']
            result = {}
            for i in data:
                symbol = i['ccy']
                used = safe_float(i['frozenBal'])
                total = safe_float(i['cashBal'])
                free = total - used

                result[symbol] = {'free': free, 'used': used, 'total': total}
                result.setdefault('free', {})[symbol] = free
                result.setdefault('used', {})[symbol] = used
                result.setdefault('total', {})[symbol] = total
            self.last_balance = copy.deepcopy(result)
            return result
        except Exception as e:
            raise type(e)(f'{e}\n{response}')

    @dec_retry
    def get_position(self, symbol):
        ex_symbol = self.get_ex_symbol(symbol)
        endpoint = '/api/v5/account/positions'
        params = {
            'instId': ex_symbol,
        }
        body = {}

        try:
            response = self.signed_request(endpoint, params=params, body=body, method='GET')
        except exceptions.ApiClosedError as e:
            if self.in_api_close_time():
                self.logger.warning(f'api closed, use last position: {self.last_position}')
                return self.last_position
            else:
                raise e

        if not isinstance(response, dict):
            raise exceptions.NetWorkError(response)

        try:
            data_long = next(filter(
                lambda x: x['contract_code'] == ex_symbol and x['direction'] == 'buy', response['data']), {})
            data_short = next(filter(
                lambda x: x['contract_code'] == ex_symbol and x['direction'] == 'sell', response['data']), {})
            if not self.last_position:
                position = {'long': {'free': 0, 'used': 0, 'total': 0},
                            'short': {'free': 0, 'used': 0, 'total': 0}}
            else:
                position = copy.deepcopy(self.last_position)
            if data_long:
                total = safe_float(data_long['volume'])
                free = safe_float(data_long['available'])
                used = total - free
                position['long']['free'] = abs(free)
                position['long']['used'] = abs(used)
                position['long']['total'] = abs(total)
            if data_short:
                total = safe_float(data_short['volume'])
                free = safe_float(data_short['available'])
                used = total - free
                position['short']['free'] = abs(free)
                position['short']['used'] = abs(used)
                position['short']['total'] = abs(total)
            self.last_position = copy.deepcopy(position)
            return position
        except Exception as e:
            raise type(e)(f'{e}\n{response}')

    def clean_position(self, symbol, test_mode=True):
        pos = self.get_position(symbol)
        depth = self.get_depth_extend(symbol)
        ask1, bid1 = depth['ask1'], depth['bid1']
        if pos['long']['free'] > 0:
            self.create_limit_sell_order(
                symbol=symbol,
                amount=int(abs(pos['long']['free'])),
                price=bid1,
                test_mode=test_mode
            )
        if pos['short']['free'] > 0:
            self.create_limit_buy_order(
                symbol=symbol,
                amount=int(abs(pos['short']['free'])),
                price=ask1,
                test_mode=test_mode
            )

    @dec_retry_new(max_retries=1)
    def create_order(self, symbol, type, side, amount, price=None, test_mode=True, **kwargs):
        if test_mode:
            return None
        if type not in ['limit', 'limit-maker']:
            raise exceptions.OrderPutError(f'type {type} is not supported')
        if type == 'stop-limit' and 'stop_price' not in kwargs:
            raise exceptions.OrderPutError(f'param stop_price is required')

        endpoint = '/api/v5/trade/order'
        body = {
            'instId': self.get_ex_symbol(symbol),
            'tdMode': 'cash',
            'side': side,
            'ordType': self.ORDER_TYPE_MAP[type],
            'sz': float2str(amount),
        }
        if price:
            body['px'] = float2str(price)
        # if kwargs.get('stop_price'):
        #     body['stopPrice'] = float2str(kwargs['stop_price'])

        assert side in OrderSideValidValues
        response = self.signed_request(endpoint, body=body, method='POST')
        try:
            data = response['data'][0]
            timestamp = get_timestamp19()
            return {
                'id': data['ordId'],
                'timestamp': int(timestamp),
                'datetime': self.iso8601(timestamp),
                'symbol': symbol,
                'type': type,
                'side': side,
                'price': safe_float(price),
                'amount': safe_float(amount),
                'filled': 0,
                'remaining': safe_float(amount),
                'status': 'open',
                'fee': None,
            }
        except Exception as e:
            raise type(e)(f'{e}\n{response}')

    def create_limit_buy_order(self, symbol, price, amount, test_mode=True):
        return self.create_order(symbol=symbol, price=price, amount=amount, side='buy', type='limit', test_mode=test_mode)

    def create_limit_sell_order(self, symbol, price, amount, test_mode=True):
        return self.create_order(symbol=symbol, price=price, amount=amount, side='sell', type='limit', test_mode=test_mode)

    @dec_retry
    def get_order(self, symbol, order_id):
        endpoint = '/api/v5/trade/order'
        params = {
            'instId': self.get_ex_symbol(symbol),
            'ordId': order_id
        }
        response = self.signed_request(endpoint, params=params, method='GET')
        if not response:
            return
        if not response['data']:
            raise exceptions.OrderNotFoundError(response)
        try:
            data = response['data'][0]
            order = self._parse_order(data)
            assert_eq(order['symbol'], symbol)
            assert_eq(order['id'], order_id)
            return order
        except Exception as e:
            raise type(e)(f'{e}\n{response}')

    def _parse_order(self, data, **kwargs):
        timestamp = get_timestamp19()
        amount = safe_float(data['sz'])
        filled = safe_float(data['fillSz'])
        if data['avgPx']:
            avgprice = safe_float(data['avgPx'])
        else:
            avgprice = safe_float(data['px'])
        return {
            'id': str(data['ordId']),
            'timestamp': timestamp,
            'datetime': self.iso8601(timestamp),
            'symbol': self.get_symbol(data['instId']),
            'type': self.ORDER_TYPE_MAP[data['ordType']],
            'side': data['side'],
            'price': safe_float(data['px']),
            'avgprice': avgprice,
            'amount': amount,
            'filled': filled,
            'remaining': amount - filled,
            'status': self.STATUS_MAP[data['state']],
            'fee': None,
        }

    @dec_retry_new(max_retries=1)
    def cancel_order(self, symbol, order_id):
        body = {
            'instId': self.get_ex_symbol(symbol),
            'ordId': order_id
        }
        request_path = '/api/v5/trade/cancel-order'
        ret = self.signed_request(request_path, body=body, method='POST')
        if ret['code'] == '0':
            return True
        else:
            return False

    @dec_retry
    def get_open_orders(self, symbol):
        endpoint = '/api/v5/trade/orders-pending'
        params = {
            'instType': 'SPOT',
            'limit': 1,
        }
        last_id = ''
        result = []
        while 1:
            if last_id:
                params['after'] = last_id
            response = self.signed_request(endpoint, params=params, method='GET')
            try:
                data = response['data']
                for i in data:
                    order = self._parse_order(i)
                    assert_eq(order['symbol'], symbol)
                    result.append(order)
                    last_id = order['id']
                if not len(data):
                    break
            except Exception as e:
                raise type(e)(f'{e}\n{response}')
        return result

    @dec_retry
    def cancel_open_orders(self, symbol):
        request_path = '/api/v5/trade/cancel-batch-orders'
        open_orders = self.get_open_orders(symbol)
        body = []
        count = 0
        for i in open_orders:
            body.append({
                'instId': self.get_ex_symbol(symbol),
                'ordId': i['id'],
            })
            count += 1
            if len(body) >= 20:
                self.signed_request(request_path, body=body, method='POST')
                body.clear()
                count = 0
        if body:
            self.signed_request(request_path, body=body, method='POST')

    def http_request(self, endpoint, params, method='GET'):
        url = self.host + endpoint
        req_info = f'URL: {url}, METHOD: {method}, PARAM: {params}'
        self.logger.debug(req_info)
        proxies = {}
        if self.http_proxy:
            proxies['https'] = f'{self.http_proxy}'
        response = requests.request(
            method, url, params=params, timeout=self.timeout, proxies=proxies)
        rep_info = f'REP_URL: {response.url}, REP_TEXT: {response.text}'
        self.logger.debug(rep_info)
        return self.handle_error(response)

    def signed_request(self, endpoint, params=None, body=None, method='POST'):
        timestamp = datetime.utcnow().strftime('%Y-%m-%dT%H:%M:%SZ')
        params = params if params else {}
        body = body if body else {}
        signature = self.sign(params, body, method, endpoint, timestamp)

        headers = {
            'Content-Type': 'application/json',
            'OK-ACCESS-KEY': self.api_key,
            'OK-ACCESS-SIGN': signature,
            'OK-ACCESS-TIMESTAMP': timestamp,
            'OK-ACCESS-PASSPHRASE': self.pass_phrase,
        }

        url = self.host + endpoint if not endpoint.startswith('https') else endpoint
        req_info = f'{method} {url} {params} {body}'
        self.logger.info(f'(REQ) {req_info}')
        proxies = {}
        if self.http_proxy:
            proxies['https'] = f'{self.http_proxy}'
        response = requests.request(method, url, headers=headers, params=params,
                                    json=body, timeout=self.timeout, proxies=proxies)
        rep_info = f'(RSP) {response.url} {response.text}'
        self.logger.info(rep_info)
        return self.handle_error(response)

    def handle_error(self, response):
        """
        exceptions.QtyMinError
        exceptions.PxSigError
        exceptions.QtySigError
        exceptions.InsufficientFundError
        exceptions.InsufficientPositionError
        exceptions.DDoSProtection
        exceptions.OrderNotFoundError
        """
        json_res = response.json()
        if isinstance(json_res, dict) and json_res.get('status') != 'ok':
            if json_res.get('err_code') == 1040:
                raise exceptions.QtyMinError(json_res)
            elif json_res.get('err_code') == 1038:
                raise exceptions.PxSigError(json_res)
            elif json_res.get('err_code') == 1067:
                raise exceptions.QtySigError(json_res)
            elif json_res.get('err_code') == 1047:
                raise exceptions.InsufficientFundError(json_res)
            elif json_res.get('err_code') == 1048:
                raise exceptions.InsufficientPositionError(json_res)
            elif json_res.get('err_code') == 1032:
                raise exceptions.DDoSProtection(json_res)
            elif json_res.get('err_code') == 1061:
                raise exceptions.OrderNotFoundError(json_res)
            elif json_res.get('err_code') in [1056, 1057, 1058, 1059, 1060, 1078, 1079]:
                raise exceptions.ApiClosedError(json_res)
        return json_res

    def sign(self, params, body, method, request_path, timestamp):
        if params:
            # sorted_params = dict(sorted(params.items()))
            encode_params = urllib.parse.urlencode(params)
            request_path = f'{request_path}?{encode_params}'
        # sorted_body = dict(sorted(body.items()))
        body_string = json.dumps(body)
        payload = [timestamp, method, request_path, body_string]
        payload = ''.join(payload)
        # self.logger.info(f'payload={payload}')
        signature = hmac.new(self.api_secret.encode(), payload.encode(),
                             digestmod=hashlib.sha256).digest()
        signature = base64.b64encode(signature)
        # signature = signature.decode()
        return signature
