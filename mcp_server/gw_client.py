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
