"""
交易操作工具（MCP Phase 5）

工具列表：
  - place_order   下单（通过 C++ HttpGateway）
  - cancel_order  撤单（通过 C++ HttpGateway）
  - get_depth     查询最新行情快照（引擎缓存，fallback OKX 公开 REST）
  - get_orders    查询当前委托列表（Flask web_server）
  - get_positions 查询持仓（Flask web_server）
  - get_balances  查询余额（Flask web_server）
  - get_trades    查询成交（Flask web_server）
"""

import json
import logging

import httpx
from mcp.server.fastmcp import FastMCP

import gw_client
import flask_client

logger = logging.getLogger(__name__)

# OKX 公开行情 REST（无需认证，用于 get_depth fallback）
_OKX_MARKET_BOOKS = "https://www.okx.com/api/v5/market/books"


def register_trade_tools(mcp: FastMCP) -> None:
    """将所有交易工具注册到 MCP server 实例。"""

    # ── place_order ──────────────────────────────────────────────────────────────

    @mcp.tool()
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
    ) -> str:
        """
        向 OKX 下单。返回本地 order_id（不等待交易所确认）。

        参数说明：
          account_id  账户ID（在 gtrade 配置文件中定义）
          inst_id     标的，如 BTC-USDT（现货）/ BTC-USDT-SWAP（永续合约）
          td_mode     交易模式: cash（现货）/ cross（全仓）/ isolated（逐仓）
          side        方向: buy / sell
          ord_type    订单类型: limit（限价）/ market（市价）/ post_only / fok / ioc
          sz          下单数量（币或张）
          px          限价单价格（market 单留空）
          market      市场（默认 okx）
          portfolio   组合，可选；留空则归入 mcp_trade

        下单后通过 get_orders() 轮询确认成交状态。
        """
        try:
            result = gw_client.place_order(
                account_id=account_id,
                inst_id=inst_id,
                td_mode=td_mode,
                side=side,
                ord_type=ord_type,
                sz=sz,
                px=px,
                market=market,
                portfolio=portfolio,
            )
            # order_id 是 19 位本地委托号（> 2^53），统一按字符串回吐：客户端（LLM/JS）按
            # JSON number 解析会被 double 静默舍入，再拿它撤单必然查不到委托
            if isinstance(result, dict) and result.get('order_id') is not None:
                result['order_id'] = str(result['order_id'])
            return json.dumps(result, ensure_ascii=False)
        except Exception as e:
            logger.error(f"place_order error: {e}")
            return json.dumps({"success": False, "error": str(e)}, ensure_ascii=False)

    # ── cancel_order ─────────────────────────────────────────────────────────────

    @mcp.tool()
    def cancel_order(account_id: str, order_id: str) -> str:
        """
        撤销委托单。

        参数说明：
          account_id  账户ID
          order_id    本地订单号（字符串）：place_order 返回的 order_id，
                      或 get_orders 返回记录中的 entno 字段

        订单号是 19 位大整数（> 2^53），必须按字符串传递：JSON number 在 JS/LLM 客户端会被
        双精度静默舍入（…000008 → …000000），引擎按错误的号查不到委托。
        本工具内部转成精确整数后经网关发往引擎。
        """
        try:
            try:
                entno = int(str(order_id).strip())
            except (TypeError, ValueError):
                return json.dumps(
                    {"success": False, "error": f"invalid order_id: {order_id!r} (expect decimal string)"},
                    ensure_ascii=False,
                )
            result = gw_client.cancel_order(account_id=account_id, order_id=entno)
            return json.dumps(result, ensure_ascii=False)
        except Exception as e:
            logger.error(f"cancel_order error: {e}")
            return json.dumps({"success": False, "error": str(e)}, ensure_ascii=False)

    # ── get_depth ────────────────────────────────────────────────────────────────

    @mcp.tool()
    def get_depth(inst_id: str, market: str = "okx") -> str:
        """
        查询最新行情快照（买一/卖一及多档）。

        优先从引擎内部缓存读取（已有策略订阅该标的时可用）。
        若未订阅，自动 fallback 到 OKX 公开 REST 接口查询。

        返回格式：
          {
            "success": true,
            "source": "internal" | "okx_rest",
            "inst_id": "BTC-USDT",
            "asks": [[price, amount], ...],   // 卖盘，price 升序
            "bids": [[price, amount], ...],   // 买盘，price 降序
            "timestamp": 1716000000000000000  // 纳秒
          }
        """
        try:
            try:
                result = gw_client.get_depth(inst_id=inst_id, market=market)
            except RuntimeError as e:
                # 引擎未订阅该标的时网关返回 404（gw_client 对 4xx/5xx 抛异常），
                # 视为"内部缓存未命中"，继续走 OKX REST fallback
                if "404" not in str(e):
                    raise
                result = {"success": False}
            if result.get("success"):
                result["source"] = "internal"
                return json.dumps(result, ensure_ascii=False)

            # fallback: OKX 公开 REST
            resp = httpx.get(
                _OKX_MARKET_BOOKS,
                params={"instId": inst_id, "sz": "5"},
                timeout=10,
            )
            resp.raise_for_status()
            data = resp.json()
            if data.get("code") != "0" or not data.get("data"):
                return json.dumps(
                    {"success": False, "error": f"OKX REST error: {data.get('msg', 'unknown')}"},
                    ensure_ascii=False,
                )
            book = data["data"][0]
            return json.dumps(
                {
                    "success":   True,
                    "source":    "okx_rest",
                    "inst_id":   inst_id,
                    "asks":      [[float(r[0]), float(r[1])] for r in book.get("asks", [])],
                    "bids":      [[float(r[0]), float(r[1])] for r in book.get("bids", [])],
                    "timestamp": int(book.get("ts", 0)) * 1_000_000,  # ms → ns
                },
                ensure_ascii=False,
            )
        except Exception as e:
            logger.error(f"get_depth error: {e}")
            return json.dumps({"success": False, "error": str(e)}, ensure_ascii=False)

    # ── get_orders ───────────────────────────────────────────────────────────────

    @mcp.tool()
    def get_orders(
        page_size: int = 50,
        page: int = 1,
        inst_id: str = "",
        status: str = "",
        policy_no: str = "",
    ) -> str:
        """
        查询委托列表（来自数据库，按委托时间倒序，含历史委托）。

        参数说明：
          page_size  每页条数（默认 50）
          page       页码（默认 1）
          inst_id    按标的模糊过滤（如 BTC-USDT）
          status     按状态过滤（1=正报, 2=已报, 3=部成, 4=全成, 9=废单；
                     1/2/3 为活跃委托，4/9 为终态）
          policy_no  按策略编号过滤（place_order 下单固定为 mcp_trade）

        返回记录中的 entno 为本地订单号（字符串形式大整数），
        撤单时将其作为 cancel_order 的 order_id 传入。
        """
        try:
            # 参数名与 Flask /api/orders 路由对齐（page/page_size/policy_no/inst_id/status）
            params: dict = {"page": page, "page_size": page_size}
            if inst_id:
                params["inst_id"] = inst_id
            if status:
                params["status"] = status
            if policy_no:
                params["policy_no"] = policy_no
            result = flask_client.get("/api/orders", params=params)
            return json.dumps(result, ensure_ascii=False)
        except Exception as e:
            logger.error(f"get_orders error: {e}")
            return json.dumps({"error": str(e)}, ensure_ascii=False)

    # ── get_positions ────────────────────────────────────────────────────────────

    @mcp.tool()
    def get_positions() -> str:
        """查询当前持仓列表。"""
        try:
            result = flask_client.get("/api/positions")
            return json.dumps(result, ensure_ascii=False)
        except Exception as e:
            logger.error(f"get_positions error: {e}")
            return json.dumps({"error": str(e)}, ensure_ascii=False)

    # ── get_balances ─────────────────────────────────────────────────────────────

    @mcp.tool()
    def get_balances() -> str:
        """查询账户余额。下单前建议先调用确认可用资金。"""
        try:
            result = flask_client.get("/api/balances")
            return json.dumps(result, ensure_ascii=False)
        except Exception as e:
            logger.error(f"get_balances error: {e}")
            return json.dumps({"error": str(e)}, ensure_ascii=False)

    # ── get_trades ───────────────────────────────────────────────────────────────

    @mcp.tool()
    def get_trades(limit: int = 50) -> str:
        """查询最近成交记录。"""
        try:
            result = flask_client.get(f"/api/trades?limit={limit}")
            return json.dumps(result, ensure_ascii=False)
        except Exception as e:
            logger.error(f"get_trades error: {e}")
            return json.dumps({"error": str(e)}, ensure_ascii=False)
