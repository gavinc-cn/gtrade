"""
Flask web_server HTTP 客户端（同步版本）
负责 JWT token 的获取与自动刷新，所有需要认证的请求通过此模块发出。
"""

import time
import logging
import httpx
from config import FLASK_BASE_URL, FLASK_USERNAME, FLASK_PASSWORD, HTTP_TIMEOUT

logger = logging.getLogger(__name__)

# ── Token 缓存 ──────────────────────────────────────────────────────────────────
_token: str | None = None
_token_ts: float = 0.0
_TOKEN_TTL = 23 * 3600  # 23 小时（JWT 有效期 24h，提前 1h 刷新）


def _login() -> str:
    """向 Flask 获取新 JWT token，失败时抛出异常。"""
    resp = httpx.post(
        f"{FLASK_BASE_URL}/api/login",
        json={"username": FLASK_USERNAME, "password": FLASK_PASSWORD},
        timeout=HTTP_TIMEOUT,
    )
    resp.raise_for_status()
    data = resp.json()
    if not data.get("success"):
        raise RuntimeError(f"Flask 登录失败: {data.get('message', '未知错误')}")
    return data["token"]


def _get_token() -> str:
    """返回有效的 JWT token，必要时自动重新登录。"""
    global _token, _token_ts
    now = time.time()
    if _token is None or (now - _token_ts) > _TOKEN_TTL:
        logger.info("正在获取 Flask JWT token ...")
        _token = _login()
        _token_ts = now
    return _token


def _headers() -> dict:
    return {"Authorization": f"Bearer {_get_token()}"}


def get(path: str, **kwargs) -> dict:
    """发送带认证的 GET 请求，自动处理 401 重新登录（最多重试一次）。"""
    global _token
    for attempt in range(2):
        try:
            resp = httpx.get(
                f"{FLASK_BASE_URL}{path}",
                headers=_headers(),
                timeout=HTTP_TIMEOUT,
                **kwargs,
            )
            if resp.status_code == 401 and attempt == 0:
                _token = None
                continue
            resp.raise_for_status()
            return resp.json()
        except httpx.HTTPStatusError as e:
            raise RuntimeError(f"Flask GET {path} 失败: {e.response.status_code} {e.response.text}") from e


def post(path: str, json_body: dict | None = None, **kwargs) -> dict:
    """发送带认证的 POST 请求，自动处理 401 重新登录（最多重试一次）。"""
    global _token
    for attempt in range(2):
        try:
            resp = httpx.post(
                f"{FLASK_BASE_URL}{path}",
                headers=_headers(),
                json=json_body,
                timeout=HTTP_TIMEOUT,
                **kwargs,
            )
            if resp.status_code == 401 and attempt == 0:
                _token = None
                continue
            resp.raise_for_status()
            return resp.json()
        except httpx.HTTPStatusError as e:
            raise RuntimeError(f"Flask POST {path} 失败: {e.response.status_code} {e.response.text}") from e


def put(path: str, json_body: dict | None = None, **kwargs) -> dict:
    """发送带认证的 PUT 请求。"""
    global _token
    for attempt in range(2):
        try:
            resp = httpx.put(
                f"{FLASK_BASE_URL}{path}",
                headers=_headers(),
                json=json_body,
                timeout=HTTP_TIMEOUT,
                **kwargs,
            )
            if resp.status_code == 401 and attempt == 0:
                _token = None
                continue
            resp.raise_for_status()
            return resp.json()
        except httpx.HTTPStatusError as e:
            raise RuntimeError(f"Flask PUT {path} 失败: {e.response.status_code} {e.response.text}") from e


def delete(path: str, **kwargs) -> dict:
    """发送带认证的 DELETE 请求。"""
    global _token
    for attempt in range(2):
        try:
            resp = httpx.delete(
                f"{FLASK_BASE_URL}{path}",
                headers=_headers(),
                timeout=HTTP_TIMEOUT,
                **kwargs,
            )
            if resp.status_code == 401 and attempt == 0:
                _token = None
                continue
            resp.raise_for_status()
            return resp.json()
        except httpx.HTTPStatusError as e:
            raise RuntimeError(f"Flask DELETE {path} 失败: {e.response.status_code} {e.response.text}") from e
