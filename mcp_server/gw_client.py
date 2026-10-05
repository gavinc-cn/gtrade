"""
C++ HttpGateway 客户端（同步版本，无需认证）
负责实际引擎操作：启动/停止/重启/添加/删除策略。
端口 46012，接口见 src/http_server/http_gateway.cpp。
"""

import logging
import httpx
from config import HTTP_GW_BASE_URL, HTTP_TIMEOUT

logger = logging.getLogger(__name__)


def _post(path: str, json_body: dict | None = None) -> dict:
    """向 C++ HttpGateway 发送 POST 请求。"""
    try:
        resp = httpx.post(
            f"{HTTP_GW_BASE_URL}{path}",
            json=json_body,
            timeout=HTTP_TIMEOUT,
        )
        resp.raise_for_status()
        return resp.json()
    except httpx.ConnectError:
        raise RuntimeError("C++ HttpGateway 未启动或连接被拒绝（:46012）")
    except httpx.HTTPStatusError as e:
        raise RuntimeError(
            f"GW POST {path} 失败: {e.response.status_code} {e.response.text}"
        ) from e


def add_strategy(config_path: str) -> dict:
    """
    加载并启动策略配置文件。
    config_path: YAML 配置文件绝对路径，引擎进程必须可访问。
    """
    return _post("/api/strategy/add", {"config_path": config_path})


def start_strategy(strat_id: str) -> dict:
    """启动已加载的策略。"""
    return _post(f"/api/strategy/start/{strat_id}")


def stop_strategy(strat_id: str) -> dict:
    """停止运行中的策略。"""
    return _post(f"/api/strategy/stop/{strat_id}")


def restart_strategy(strat_id: str) -> dict:
    """重启策略（通常在修改参数后调用）。"""
    return _post(f"/api/strategy/restart/{strat_id}")


def delete_strategy(strat_id: str) -> dict:
    """从引擎中卸载策略。"""
    # C++ 侧的删除接口路径与 Flask 侧一致
    return _post(f"/api/strategy/delete/{strat_id}")


def _get(path: str, params: dict | None = None) -> dict:
    """向 C++ HttpGateway 发送 GET 请求。"""
    try:
        resp = httpx.get(
            f"{HTTP_GW_BASE_URL}{path}",
            params=params,
            timeout=HTTP_TIMEOUT,
        )
        resp.raise_for_status()
        return resp.json()
    except httpx.ConnectError:
        raise RuntimeError("C++ HttpGateway 未启动或连接被拒绝（:46012）")
    except httpx.HTTPStatusError as e:
        raise RuntimeError(
            f"GW GET {path} 失败: {e.response.status_code} {e.response.text}"
        ) from e


def place_order(
    account_id: str,
    inst_id: str,
    td_mode: str,
    side: str,
    ord_type: str,
    sz: str,
    px: str = "",
    market: str = "okx",
    portfolio: str = "",
) -> dict:
    """
    下单。market 单 px 留空，limit 单必须填 px。
    td_mode: cash / cross / isolated
    side: buy / sell
    ord_type: limit / market / post_only / fok / ioc
    portfolio: 组合，可选；空则引擎回落为 policy_no(mcp_trade)
    """
    payload = {
        "account_id": account_id,
        "market":     market,
        "inst_id":    inst_id,
        "td_mode":    td_mode,
        "side":       side,
        "ord_type":   ord_type,
        "px":         px,
        "sz":         sz,
    }
    # 组合可选：仅在显式指定时携带，保持与旧引擎版本的兼容
    if portfolio:
        payload["portfolio"] = portfolio
    return _post("/api/trade/place_order", payload)


def cancel_order(account_id: str, order_id: int) -> dict:
    """撤单。order_id 为 place_order 返回的本地 order_id。

    必须是精确的 Python int（可任意精度，json 序列化为精确十进制数字）；调用方若从
    字符串/JSON 文本拿号，务必先 int() 转换，不要经过 float（19 位 entno 会被 double 舍入）。
    """
    return _post("/api/trade/cancel_order", {
        "account_id": account_id,
        "order_id":   order_id,
    })


def get_depth(inst_id: str, market: str = "okx") -> dict:
    """查询最新行情快照（来自引擎内部缓存）。"""
    return _get("/api/trade/depth", {"inst_id": inst_id, "market": market})
