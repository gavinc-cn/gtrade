"""TLS：WS 必须走 `wss://`（引擎硬编码 websocketpp `asio_tls_client`），证书用 openssl 自签。

引擎侧 TLS 配置是 `verify_none`（`OkxWs` 只建 `asio_tls_client`，不校验对端），故 CN/SAN 不参与
校验；本模块仍按 `-subj "/CN=127.0.0.1"` 生成，便于人工用 curl 核对连的是哪台机器。证书是
**生成物**（`certs/` 不入库），首次启动自动就位。

四条定死的语义：

1. **只认一套文件**：`cert.pem`（证书）+ `key.pem`（私钥）。两个都在 ⇒ 原样复用（重启不重签，
   否则每次启动都换证书）；缺任意一个 ⇒ 整对重签 —— 半套是坏状态，拿旧文件凑只会更难查。
2. **目录不存在就建**（`mkdir(parents=True)`），失败按 `OSError` 冒泡。
3. **失败要响亮**：openssl 退出码非 0 时把 stderr 原文带进 `RuntimeError`；找不到 openssl 可执行
   文件同样报 `RuntimeError`（环境问题不该伪装成"证书写坏了"）。失败后清掉可能写了一半的文件，
   否则下次启动会把半套当成"已存在"。
4. **私钥不外泄**：日志里只出现路径，不打印内容。
"""

import logging
import ssl
import subprocess
from pathlib import Path

__all__ = ["CERT_FILE", "KEY_FILE", "ensure_cert", "make_ssl_context"]

CERT_FILE = "cert.pem"
KEY_FILE = "key.pem"
_DAYS = 3650        # 10 年：mock 是本地工具，证书过期只会白白打断引擎
_CN = "127.0.0.1"   # 引擎 verify_none，CN 只为人工辨认
_TIMEOUT = 60       # 生成 2048 位密钥通常 <1s；卡住要报错，不能让启动挂死

logger = logging.getLogger(__name__)


def ensure_cert(cert_dir) -> tuple[str, str]:
    """确保 `cert_dir` 下有一对可用的自签证书，返回 `(证书路径, 私钥路径)`。

    Args:
        cert_dir: 证书目录（str / Path），不存在则创建。相对路径按调用方 CWD 解析 ——
            `run.py` 已把配置里的相对 `cert_dir` 锚到配置文件所在目录。

    Returns:
        tuple[str, str]: `(cert.pem 的绝对路径, key.pem 的绝对路径)`。

    Raises:
        RuntimeError: openssl 不可用、超时或退出码非 0（消息里带 stderr 原文）。
        OSError: 目录建不出来 / 文件写不了（权限等）。
    """
    directory = Path(cert_dir).expanduser().resolve()
    cert_path, key_path = directory / CERT_FILE, directory / KEY_FILE
    if cert_path.is_file() and key_path.is_file():
        logger.debug("复用已有自签证书: %s", cert_path)
        return str(cert_path), str(key_path)
    directory.mkdir(parents=True, exist_ok=True)
    try:
        _generate(cert_path, key_path)
    except RuntimeError:
        for path in (cert_path, key_path):      # 半套文件会让下次启动误判"已存在"
            path.unlink(missing_ok=True)
        raise
    logger.info("已生成自签证书（有效期 %d 天）: %s / %s", _DAYS, cert_path, key_path)
    return str(cert_path), str(key_path)


def make_ssl_context(cert_dir) -> ssl.SSLContext:
    """造 WS 站点的服务端 TLS 上下文：`ensure_cert` → `load_cert_chain`。

    Args:
        cert_dir: 同 `ensure_cert`。

    Returns:
        ssl.SSLContext: 已加载证书/私钥的服务端上下文（aiohttp `SockSite(ssl_context=...)`）。

    Raises:
        RuntimeError: 证书生成失败（`ensure_cert`）或加载失败（文件坏 / 不可读）。
    """
    cert, key = ensure_cert(cert_dir)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    try:
        context.load_cert_chain(cert, key)
    except OSError as exc:      # ssl.SSLError 也是 OSError 的子类
        raise RuntimeError(f"证书加载失败（{cert}）: {exc}") from exc
    return context


def _generate(cert_path: Path, key_path: Path) -> None:
    """调 openssl 生成一对自签证书（`-nodes`：私钥不加密，启动不需要交互输口令）。

    Raises:
        RuntimeError: 找不到 openssl、超时或退出码非 0。
    """
    cmd = ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", str(_DAYS),
           "-subj", f"/CN={_CN}", "-keyout", str(key_path), "-out", str(cert_path)]
    try:
        done = subprocess.run(cmd, capture_output=True, text=True, timeout=_TIMEOUT, check=False)
    except FileNotFoundError as exc:
        raise RuntimeError(f"未找到 openssl 可执行文件，无法生成自签证书（{cert_path.parent}）") from exc
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(f"openssl 生成证书超时（>{_TIMEOUT}s）: {' '.join(cmd)}") from exc
    if done.returncode != 0:
        raise RuntimeError(f"openssl 生成证书失败（退出码 {done.returncode}）: {done.stderr.strip()}")
