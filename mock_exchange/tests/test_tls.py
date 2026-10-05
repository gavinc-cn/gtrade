"""TLS 自签证书与启动装配测试（TDD：先红后绿）。

两块内容：

1. `mockex.protocol.tls` 的单元契约 —— 空目录生成一对可被 `ssl.SSLContext.load_cert_chain`
   加载的证书、目录自动创建、半套文件（只有证书或只有私钥）整对重签、已有一对则原样复用、
   openssl 失败/缺失把 stderr 带出来响亮报错且不留半套文件；
2. **启动装配端到端** —— `MockService` 按配置起三个站点（REST 明文 / WS TLS / 管理面仅本机），
   端口写 0 由内核随机分配；客户端连 `wss://` 发 ping→pong、走公共订阅回执与 bbo 推送、
   私有 login/order ack 与 `orders` 推送、REST 查询与 WS 推送同源、管理面注入盘口 ——
   T1–T9 的装配接缝在一次真实启动里全走到（T6/T7 的用例把 handler 挂在 `TestServer` 上，
   这里起的是 T10 的装配体本身）。

驱动方式同前序任务：同步测试函数 + `asyncio.run`，不引 pytest-asyncio。
"""

import asyncio
import json
import logging
import socket
import ssl
import subprocess
from dataclasses import replace
from pathlib import Path

import pytest
from aiohttp import ClientSession, WSMsgType

import run
from mockex.config import (DEFAULT_CONFIG_PATH, FeedConfig, MockConfig, QuotesConfig, ServerConfig,
                           TlsConfig, load_config)
from mockex.protocol.tls import CERT_FILE, KEY_FILE, ensure_cert, make_ssl_context
from mockex.wiring import ADMIN_HOST, MockService

BTC, BBO = "BTC-USDT", "bbo-tbt"
WS_PUBLIC, WS_PRIVATE = "/ws/v5/public", "/ws/v5/private"
REST_INSTRUMENTS = "/api/v5/account/instruments?instType=SPOT"
# T8 的 rest.py 交出的七条路由（路径必须与引擎实际发出的完全一致）
ENGINE_REST_ROUTES = [
    ("GET", "/api/v5/account/instruments"), ("GET", "/api/v5/market/books"),
    ("GET", "/api/v5/market/candles"), ("GET", "/api/v5/account/balance"),
    ("GET", "/api/v5/trade/order"), ("GET", "/api/v5/trade/orders-pending"),
    ("GET", "/api/v5/trade/orders-history")]
DEPTH = {"instId": BTC, "bids": [[60000, 1]], "asks": [[60001, 1]]}
# 引擎实际发的下单报文（px/sz 是 JSON number，多带 instIdCode/tdMode/posSide）
ENGINE_ORDER = {"instId": BTC, "tdMode": "cash", "clOrdId": "T10-1", "side": "buy", "posSide": "net",
                "ordType": "limit", "sz": 0.1, "px": 60001}


def _openssl(*args: str) -> str:
    """跑一条只读的 openssl 查询（生成路径走被测代码，这里只做核对）。"""
    done = subprocess.run(["openssl", *args], check=True, capture_output=True, text=True)
    return done.stdout


def _sub(*args) -> str:
    """订阅报文：`(channel, instId)` 或 `(channel, None)`（私有频道不吃 instId 约束）。"""
    return json.dumps({"op": "subscribe", "args": [
        {"channel": channel} if inst_id is None else {"channel": channel, "instId": inst_id}
        for channel, inst_id in args]})


async def _json(ctx):
    """请求（`session.get/post` 的上下文管理器）→ HTTP 200 的 JSON 体。"""
    async with ctx as response:
        assert response.status == 200, f"HTTP {response.status}: {await response.text()}"
        return await response.json()


async def _frame(ws):
    """读一帧并解析 JSON（超时即失败：测试挂死比断言失败难查一个数量级）。"""
    message = await asyncio.wait_for(ws.receive(), 5.0)
    assert message.type is WSMsgType.TEXT, f"期望 TEXT 帧，实得 {message.type}: {message.data!r}"
    return json.loads(message.data)


async def _text(ws):
    message = await asyncio.wait_for(ws.receive(), 5.0)
    assert message.type is WSMsgType.TEXT, f"期望 TEXT 帧，实得 {message.type}: {message.data!r}"
    return message.data


# ── ensure_cert：生成 / 复用 / 报错 ──────────────────────────────────────────

def test_ensure_cert_generates_pair_a_server_context_can_load(tmp_path):
    """空目录 → `cert.pem` / `key.pem`：CN 是 127.0.0.1、有效期 ≈10 年、能被 SSLContext 加载。"""
    cert, key = ensure_cert(tmp_path)
    assert Path(cert).read_text().startswith("-----BEGIN CERTIFICATE-----")
    assert "PRIVATE KEY" in Path(key).read_text()       # 私钥头各版本不同（PKCS#8/PKCS#1），只认"是私钥"
    assert "127.0.0.1" in _openssl("x509", "-noout", "-subject", "-in", cert)
    # -checkend 秒数：退出码 0 = 到期晚于 now+315187200s（10 年减 2 天，容忍生成耗时）
    assert _openssl("x509", "-noout", "-checkend", "315187200", "-in", cert)
    ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER).load_cert_chain(cert, key)   # 不抛 = 引擎侧能加载


def test_ensure_cert_reuses_existing_pair_without_regenerating(tmp_path, monkeypatch):
    """已有一对证书 → 原样复用（重启不重签），连 openssl 都不再调。"""
    cert, key = ensure_cert(tmp_path)
    before = (Path(cert).read_bytes(), Path(key).read_bytes())

    def _boom(*args, **kwargs):
        raise AssertionError("证书已存在，不应重新生成")

    monkeypatch.setattr(subprocess, "run", _boom)
    assert ensure_cert(tmp_path) == (cert, key)
    assert (Path(cert).read_bytes(), Path(key).read_bytes()) == before


@pytest.mark.parametrize("missing", [CERT_FILE, KEY_FILE])
def test_ensure_cert_regenerates_when_half_pair_missing(tmp_path, missing):
    """半套文件（只有证书或只有私钥）是坏状态：整对重签，不拿旧文件凑。"""
    (tmp_path / missing).write_text("stale")
    cert, key = ensure_cert(tmp_path)
    assert Path(cert).is_file() and Path(key).is_file()
    assert Path(cert).read_text() != "stale" and Path(key).read_text() != "stale"
    ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER).load_cert_chain(cert, key)


def test_ensure_cert_creates_missing_directory(tmp_path):
    """目录不存在 → 建出来（`certs/` 是生成物，不入库，首次启动就该自愈）。"""
    cert, key = ensure_cert(tmp_path / "a" / "b")
    assert Path(cert).parent == tmp_path / "a" / "b" and Path(key).is_file()


def test_ensure_cert_reports_openssl_failure_with_stderr(tmp_path, monkeypatch):
    """openssl 退出码非 0 → RuntimeError 带 stderr 原文，且不留下半套文件。"""
    def fake_run(cmd, **kwargs):
        (tmp_path / KEY_FILE).write_text("半个私钥")      # 模拟 openssl 写了一半就失败
        return subprocess.CompletedProcess(cmd, 1, "", "爆: 无法生成证书")

    monkeypatch.setattr(subprocess, "run", fake_run)
    with pytest.raises(RuntimeError, match="爆: 无法生成证书"):
        ensure_cert(tmp_path)
    assert not (tmp_path / KEY_FILE).exists()            # 残留会让下次启动误判"已存在"


def test_ensure_cert_reports_missing_openssl(tmp_path, monkeypatch):
    """openssl 不在 PATH → RuntimeError 点明，而不是 FileNotFoundError 冒到顶层。"""
    def fake_run(cmd, **kwargs):
        raise FileNotFoundError(2, "No such file or directory", "openssl")

    monkeypatch.setattr(subprocess, "run", fake_run)
    with pytest.raises(RuntimeError, match="openssl"):
        ensure_cert(tmp_path)


def test_make_ssl_context_loads_the_cert_pair(tmp_path):
    """WS 站点用的服务端上下文：由 `cert_dir` 生成并加载，落地就是那两个文件。"""
    context = make_ssl_context(tmp_path)
    assert isinstance(context, ssl.SSLContext)
    assert (tmp_path / CERT_FILE).is_file() and (tmp_path / KEY_FILE).is_file()


# ── 装配：绑定目标与未实现的配置项 ──────────────────────────────────────────

def test_bindings_follow_config_and_keep_admin_on_loopback():
    """三个站点：REST/WS 对外（0.0.0.0），管理面只绑 127.0.0.1（`/admin/reset` 有重置能力）。"""
    assert MockService(MockConfig()).bindings == [
        ("rest", "0.0.0.0", 18080), ("ws", "0.0.0.0", 18443), ("admin", ADMIN_HOST, 18081)]


def test_prepare_config_anchors_relative_cert_dir_to_package_root(tmp_path):
    """相对 `cert_dir` 锚到 `mock_exchange/`（run.py 所在处）：任意 CWD 启动都落同一处。"""
    config_path = tmp_path / "mock.yml"
    config_path.write_text("tls:\n  cert_dir: certs\n")
    root = Path(run.__file__).resolve().parent
    assert run.prepare_config(config_path).tls.cert_dir == str(root / "certs")
    absolute = tmp_path / "elsewhere"
    config_path.write_text(f"tls:\n  cert_dir: {absolute}\n")
    assert run.prepare_config(config_path).tls.cert_dir == str(absolute)     # 绝对路径原样用


def test_feed_source_synthetic_is_rejected_at_assembly():
    """`feed.source: synthetic`（M3）装配即报错：静默放着会让操作者以为合成行情在跑。"""
    with pytest.raises(ValueError, match="synthetic"):
        MockService(replace(MockConfig(), feed=FeedConfig(source="synthetic")))


def test_unknown_feed_source_is_rejected_at_assembly():
    """写错的值（哪怕只多个空格）同样报错，并列出合法取值。"""
    with pytest.raises(ValueError, match="feed.source"):
        MockService(replace(MockConfig(), feed=FeedConfig(source="none ")))


def test_nonzero_quotes_throttle_only_warns(caplog):
    """`quotes.throttle_ms` 非 0 目前不生效（M1 恒定每次变化都推）：记 warning，不拦启动。"""
    with caplog.at_level(logging.WARNING, logger="mockex.wiring"):
        MockService(replace(MockConfig(), quotes=QuotesConfig(throttle_ms=50)))
    assert "throttle_ms" in caplog.text


def test_start_rolls_back_when_a_port_is_taken(tmp_path):
    """一个端口起不来就不留半装：已起的站点回滚、端口还回去，挪开占用后能重新起。"""
    async def scenario():
        blocker = socket.socket()
        blocker.bind(("0.0.0.0", 0))
        blocker.listen(1)
        taken = blocker.getsockname()[1]
        config = replace(MockConfig(), server=ServerConfig(rest_port=0, ws_port=taken, admin_port=0),
                         tls=TlsConfig(cert_dir=str(tmp_path / "certs")))
        service = MockService(config)
        with pytest.raises(OSError):
            await service.start()
        blocker.close()          # 回滚过才重新起得来（否则撞"服务已启动"守卫）
        await service.start()
        try:
            assert service.ports["ws"] == taken     # 配置写死的端口就绑它（0 才是随机）
        finally:
            await service.stop()

    asyncio.run(scenario())


# ── 端到端：三站点起服务 → wss 心跳/订阅 → 私有下单 → REST 与管理面 ──────────

def test_service_end_to_end_rest_ws_tls_admin(tmp_path):
    """端到端装配：三站点起在随机端口 → wss ping/pong → 公共订阅 → 私有下单 → REST 同源。"""
    async def scenario():
        config = load_config(DEFAULT_CONFIG_PATH)               # 出厂配置必须可启动
        config = replace(config, server=ServerConfig(rest_port=0, ws_port=0, admin_port=0),
                         tls=TlsConfig(cert_dir=str(tmp_path / "certs")))
        service = MockService(config)
        assert [route[:2] for route in service.adapter.rest_routes()] == ENGINE_REST_ROUTES
        await service.start()
        try:
            ports = service.ports
            rest = f"http://127.0.0.1:{ports['rest']}"
            admin = f"http://127.0.0.1:{ports['admin']}"
            ws_base = f"wss://127.0.0.1:{ports['ws']}"
            async with ClientSession() as session:
                instruments = await _json(session.get(rest + REST_INSTRUMENTS))
                assert instruments["code"] == "0"
                # 清单来自 data/okx_instruments.csv（1000+ 条，按 instId 排序），
                # 断言 BTC-USDT 在其中而不是断言行序
                btc_spot = next(r for r in instruments["data"]
                                if r["instId"] == BTC and r["instType"] == "SPOT")
                assert btc_spot["instIdCode"] == 3                   # JSON number（引擎 GetInt64）
                assert (await _json(session.post(admin + "/admin/depth", json=DEPTH)))["ok"] is True

                async with session.ws_connect(ws_base + WS_PUBLIC, ssl=False) as public:
                    await public.send_str("ping")
                    assert await _text(public) == "pong"             # 心跳是引擎的保命线
                    await public.send_str(_sub((BBO, BTC)))
                    assert await _frame(public) == {"event": "subscribe",
                                                    "arg": {"channel": BBO, "instId": BTC}}
                    snapshot = await _frame(public)          # 订阅后补推当前盘口（重连不用等下一次变化）
                    assert snapshot["arg"] == {"channel": BBO, "instId": BTC}
                    assert (snapshot["data"][0]["bids"], snapshot["data"][0]["asks"]) == \
                        ([["60000", "1"]], [["60001", "1"]])          # 上面 /admin/depth 注进去的那档
                    assert isinstance(snapshot["data"][0]["ts"], str)  # 引擎 GetString：必须是字符串
                    async with session.ws_connect(ws_base + WS_PRIVATE, ssl=False) as private:
                        await private.send_str(json.dumps({"op": "login",
                                                           "args": [{"apiKey": "mock-okx-key"}]}))
                        assert await _frame(private) == {"event": "login", "code": "0"}
                        await private.send_str(_sub(("orders", None), ("balance_and_position", None),
                                                    ("positions", None)))
                        assert len([await _frame(private) for _ in range(3)]) == 3   # 逐 arg 一帧
                        await private.send_str(json.dumps({"id": "1", "op": "order",
                                                           "args": [ENGINE_ORDER]}))
                        ack = await _frame(private)
                        assert ack["code"] == "0" and ack["data"][0]["sCode"] == "0"
                        pushed = {frame["arg"]["channel"]: frame
                                  for frame in [await _frame(private) for _ in range(3)]}
                        assert pushed["orders"]["data"][0]["state"] == "filled"
                        assert all(isinstance(value, str)
                                   for value in pushed["orders"]["data"][0].values())
                    # orderbook（出厂配置 2026-10-03 起）：买单吃掉卖一的一部分 → 档位量变化，
                    # 公共频道必须收到盘口推送（immediate 不动盘口、不发帧，见 ws_private 单测）
                    bbo = await _frame(public)
                    assert bbo["arg"] == {"channel": BBO, "instId": BTC}
                    assert bbo["data"][0]["bids"] == [["60000", "1"]]
                    assert bbo["data"][0]["asks"] == [["60001", "0.9"]]   # 被吃掉 0.1

                books = await _json(session.get(f"{rest}/api/v5/market/books?instId={BTC}"))
                assert books["data"][0]["asks"][0] == ["60001", "0.9"]   # 引擎成交吃掉了对手量
                state = await _json(session.get(admin + "/admin/state"))
                assert state["fill_mode"] == "orderbook"
                orders = {order["clOrdId"]: order for order in state["orders"]}
                assert orders["T10-1"]["owner"] == "mock-okx-key"        # 引擎委托落在配置账户下
                assert orders["T10-1"]["accFillSz"] == "0.1"             # 吃满对手那 0.1，整单成交
                assert sum(1 for one in state["orders"] if one["owner"] == "mock_mm") == 2
                held = {row["ccy"]: row["cashBal"]
                        for row in state["accounts"]["mock-okx-key"]["balData"]}
                # 结算认合约：AccountManager 收到 instruments 才有这两个数（漏传首笔成交即 ValueError）
                assert held == {"BTC": "100.1", "USDT": "993999.9"}
        finally:
            await service.stop()

    asyncio.run(scenario())
