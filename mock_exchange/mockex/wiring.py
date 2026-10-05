"""启动装配：把配置（T1）+ 内核（T2–T4）+ OKX 适配器（T5–T8）+ 管理面（T9）接成可运行的服务。

三条定死的装配语义：

1. **证书先就位、协议面分端口**：`start()` 第一步跑 `ensure_cert`，WS 站点用 `ssl.SSLContext`
   起 TLS —— 引擎硬编码 websocketpp `asio_tls_client`，mock 不提供 TLS 就是"引擎永远连不上"；
   REST 保持明文 http（引擎 httplib 用 `http://`）。WS 的 public/private 是**同端口两条路径**，
   与真实 OKX 一致。`/ws/v5/public` 与 `/ws/v5/private` 分别注公共/私有 handler。
2. **管理面只绑 `127.0.0.1`**：`/admin/reset` 与 `/admin/depth` 有重置与注盘能力，绝不能对外；
   协议面（REST/WS）绑 `0.0.0.0`，供容器外/宿主机的引擎访问。
3. **未实现的配置项要么响要么有据地哑**：`feed.source: synthetic`（M3）装配即 `ValueError`；
   `quotes.throttle_ms` 非 0 只记一条 warning（T6 不支持节流，M1 恒定每次变化都推，引擎不依赖）。

装配顺序逐条对应 T1–T9 的接口约束：`AccountManager` **必须**收到 `instruments`（漏传首笔成交即
`ValueError`）；`bind_trading` 必须在 `AdminApi` / `bind_rest` 之前 —— 两者的委托数据源都是它返回的
`PrivateChannels`；`bind_rest` 必须在 `bind_trading` 之后（`/trade/*` 读同一份订单快照）。
"""

import functools
import logging
import socket

from aiohttp import web

from mockex.config import MockConfig
from mockex.core.account import AccountManager
from mockex.core.book import OrderBook
from mockex.core.events import EventBus
from mockex.core.matching import MatchingEngine
from mockex.exchanges.base import make_json_handler
from mockex.exchanges.okx.adapter import OkxAdapter
from mockex.protocol.admin import AdminApi
from mockex.protocol.tls import make_ssl_context
from mockex.protocol.ws import KIND_PRIVATE, KIND_PUBLIC, WsRegistry, handle_ws

__all__ = ["ADMIN_HOST", "PRIVATE_PATH", "PROTOCOL_HOST", "PUBLIC_PATH", "MockService"]

PROTOCOL_HOST = "0.0.0.0"   # REST/WS：要让容器外（宿主机/别的容器）的引擎连得上
ADMIN_HOST = "127.0.0.1"    # 管理面：只在本机（有重置与注盘能力）
PUBLIC_PATH, PRIVATE_PATH = "/ws/v5/public", "/ws/v5/private"
_SUPPORTED_FEED_SOURCES = ("none",)   # synthetic 属 M3
_LISTEN_BACKLOG = 128

logger = logging.getLogger(__name__)


class MockService:
    """mock_exchange 的单进程装配体：内核 + 适配器 + 管理面 + 三个 aiohttp app。

    构造即完成装配（**不**起端口、**不**生成证书）：`MockService(config)` 可被测试直接检查路由与
    绑定目标；`await start()` 才生成证书、绑端口、进 serve 循环。
    """

    def __init__(self, config: MockConfig) -> None:
        """装配全部组件与路由表。

        Raises:
            ValueError: `accounts` 为空（私有频道与余额查询都无账户可落）、`feed.source` 未实现或
                非法、`fill_mode` 非法（由 `MatchingEngine` 报）、`instruments` 为空（由
                `PrivateChannels` / `AdminApi` 报）。
        """
        _check_supported(config)
        if not config.accounts:
            raise ValueError("accounts 不能为空：私有频道与 REST 余额查询都要有账户可落")
        self.config = config
        self.bus = EventBus()
        self.book = OrderBook()
        self.engine = MatchingEngine(self.book, fill_mode=config.fill_mode)
        self.accounts = AccountManager(config.accounts, instruments=config.instruments)
        self.registry = WsRegistry()
        self.adapter = OkxAdapter()
        self.adapter.bind_market_data(self.bus, self.book, self.registry,
                                      [spec.inst_id for spec in config.instruments])
        account = config.accounts[0].api_key    # 单账户：私有频道的归属与余额查询的账户名
        self.trading = self.adapter.bind_trading(self.engine, self.accounts, self.bus,
                                                 instruments=config.instruments, account=account)
        self.rest = self.adapter.bind_rest(self.book, self.accounts,
                                           instruments=config.instruments, account=account)
        self.admin = AdminApi(self.book, self.accounts, self.bus, self.registry,
                              trading=self.trading, instruments=config.instruments,
                              default_sz=config.injection.default_sz, fill_mode=config.fill_mode)
        self.rest_app = _app(self.adapter.rest_routes())
        self.ws_app = self._ws_app()
        self.admin_app = _app(self.admin.routes())
        self._runners: list[web.AppRunner] = []
        self._ports: dict[str, int] = {}

    @property
    def bindings(self) -> list[tuple[str, str, int]]:
        """三个站点的绑定目标 `(面, 主机, 端口)`；端口来自配置（写 0 即由内核分配）。"""
        return [("rest", PROTOCOL_HOST, self.config.server.rest_port),
                ("ws", PROTOCOL_HOST, self.config.server.ws_port),
                ("admin", ADMIN_HOST, self.config.server.admin_port)]

    @property
    def ports(self) -> dict[str, int]:
        """实际监听端口（配置写 0 时由内核分配）；`start()` 之前取值即报错。"""
        if not self._ports:
            raise RuntimeError("服务未启动：ports 只在 start() 之后有值")
        return dict(self._ports)

    async def start(self) -> None:
        """生成/复用证书 → 起 REST（明文）、WS（TLS）、管理面（仅本机）三个站点。

        半装不留残留：任一站点起不来（端口占用等）时已起的站点全部回滚，端口还回去 ——
        否则下一次 `start()` 撞上"服务已启动"守卫，排障现场反而更难读。

        Raises:
            RuntimeError: 重复 `start`（会再绑一遍端口）或证书生成/加载失败。
            OSError: 端口占用等绑定失败。
        """
        if self._runners:
            raise RuntimeError("服务已启动：重复 start 会再绑一遍端口")
        ssl_context = make_ssl_context(self.config.tls.cert_dir)   # 引擎只能连 wss://
        apps = {"rest": (self.rest_app, None), "ws": (self.ws_app, ssl_context),
                "admin": (self.admin_app, None)}
        try:
            for label, host, port in self.bindings:
                app, context = apps[label]
                await self._listen(label, host, port, app, context)
        except Exception:
            await self.stop()
            raise

    async def stop(self) -> None:
        """关掉全部站点（幂等）；连接在途的请求由 aiohttp 等收尾。"""
        for runner in reversed(self._runners):
            await runner.cleanup()
        self._runners.clear()
        self._ports.clear()

    def _ws_app(self) -> web.Application:
        """WS 站点：public / private 两条路径同端口，各注各的 handler（真实 OKX 的布局）。"""
        app = web.Application()
        for path, private in ((PUBLIC_PATH, False), (PRIVATE_PATH, True)):
            app.router.add_get(path, functools.partial(
                handle_ws, registry=self.registry,
                kind=KIND_PRIVATE if private else KIND_PUBLIC,
                on_json=make_json_handler(self.adapter, self.registry, private=private)))
        return app

    async def _listen(self, label: str, host: str, port: int, app: web.Application,
                      ssl_context) -> None:
        """起一个站点：socket 自己绑 —— 只有拿到 socket 才能在 `port=0` 时读回实际端口。"""
        sock = _listen_socket(host, port)
        runner = web.AppRunner(app)
        try:
            await runner.setup()
            await web.SockSite(runner, sock, ssl_context=ssl_context).start()
        except Exception:
            sock.close()
            await runner.cleanup()
            raise
        self._runners.append(runner)
        self._ports[label] = sock.getsockname()[1]
        logger.info("mock_exchange %s 面已监听 %s:%d%s", label, host, self._ports[label],
                    "（TLS）" if ssl_context is not None else "")


def _app(routes) -> web.Application:
    """把 `(method, path, handler)` 路由表挂进一个 aiohttp app。"""
    app = web.Application()
    for method, path, handler in routes:
        app.router.add_route(method, path, handler)
    return app


def _listen_socket(host: str, port: int) -> socket.socket:
    """建一个已 listen 的 IPv4 TCP socket；`port=0` 由内核分配，调用方 `getsockname()` 读回。"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)   # 重启不被 TIME_WAIT 卡住
    sock.bind((host, port))
    sock.listen(_LISTEN_BACKLOG)
    sock.setblocking(False)
    return sock


def _check_supported(config: MockConfig) -> None:
    """装配前校验 M1 尚未实现的配置项。

    `feed.source` 只认 `none`：synthetic（M3）若静默通过，操作者会以为合成行情在跑，而引擎
    永远等不到帧 —— 报错比留痕好。`quotes.throttle_ms` 非 0 只记 warning：T6 的推送不支持节流，
    M1 恒定每次变化都推（引擎不依赖节流，丢的只是"少发几帧"这个优化）。
    """
    source = config.feed.source
    if source not in _SUPPORTED_FEED_SOURCES:
        detail = "属 M3，尚未实现" if source == "synthetic" else "不是合法取值"
        raise ValueError(f"feed.source={source!r} {detail}"
                         f"（M1 只支持: {', '.join(_SUPPORTED_FEED_SOURCES)}）")
    if int(config.quotes.throttle_ms) != 0:
        logger.warning("quotes.throttle_ms=%s 未生效：M1 恒定每次盘口变化都推送",
                       config.quotes.throttle_ms)
