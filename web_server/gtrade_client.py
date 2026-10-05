"""gtrade HTTP Gateway 转发客户端。

统一 web_server 到 C++ HttpGateway（:46012）的转发入口，供策略操作 / 交易 /
标的范围等路由复用：

- forward()：发起请求并把网关响应原样带回 (body, status)，含 400/404/500；
- 引擎不可达 / 超时 / 响应非 JSON 时抛 EngineUnavailable（RuntimeError 子类），
  路由层统一映射为 502 {"success": false, "message": "引擎不可达: ..."}。

网关地址取自 config.GTRADE_HTTP_GATEWAY（环境变量同名覆盖，默认
http://127.0.0.1:46012）。本模块只做透传，不做参数校验与业务语义。
"""
import logging
import os

import requests

from config import config

logger = logging.getLogger(__name__)

DEFAULT_TIMEOUT = 10


class EngineUnavailable(RuntimeError):
    """引擎不可达或网关响应异常（路由层统一映射为 502）。"""


def _gateway():
    """解析当前环境的网关基地址（每次调用重读，便于测试注入与配置变更）。"""
    env = os.environ.get('FLASK_ENV', 'development')
    return config[env]().GTRADE_HTTP_GATEWAY


def forward(method, path, json_body=None, params=None, timeout=DEFAULT_TIMEOUT):
    """转发一次请求到 gtrade HttpGateway。

    :param method: HTTP 方法（GET/POST/DELETE）
    :param path: 网关路由路径（如 /api/trade/depth），不含网关地址
    :param json_body: 请求 JSON 体（None = 不带 body）
    :param params: query 参数字典（None = 不带参数）
    :return: (body: dict, status: int)，网关响应体与状态码原样透传
    :raises EngineUnavailable: 连接失败 / 超时 / 响应体非 JSON
    """
    url = f"{_gateway()}{path}"
    try:
        resp = requests.request(method, url, json=json_body, params=params, timeout=timeout)
    except requests.RequestException as e:
        logger.error(f"gtrade 网关请求失败 {method} {url}: {e}")
        raise EngineUnavailable(f"引擎不可达: {e}")

    try:
        body = resp.json()
    except ValueError:
        logger.error(f"gtrade 网关响应非 JSON {method} {url}: HTTP {resp.status_code}")
        raise EngineUnavailable(f"引擎响应异常（HTTP {resp.status_code}）")

    return body, resp.status_code
