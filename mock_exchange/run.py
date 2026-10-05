"""mock_exchange 入口：读配置 → 装配 → 起 REST（明文）/ WS（TLS）/ 管理面（仅本机）三个站点。

用法（**任意 CWD 都能起**：`python3 <脚本路径>` 会把脚本所在目录放进 `sys.path`）：

    python3 mock_exchange/run.py                      # 出厂配置 mock_exchange/config/mock.yml
    python3 mock_exchange/run.py --config /path/mock.yml

两条刻意的取舍：

1. **相对 `cert_dir` 锚到 `mock_exchange/`（本文件所在目录）**，绝对路径原样用：`certs/` 是生成物，
   锚在包目录才不随"在哪个目录敲命令"漂移，且与 spec 目录树（`mock_exchange/certs/`）一致。
2. **起不来时只回一行说明 + 退出码 2**（配置读不到/写坏、`feed.source: synthetic`、端口占用、
   openssl 缺失）：运维要的是原因，不是 traceback。
"""

import argparse
import asyncio
import logging
from dataclasses import replace
from pathlib import Path

import yaml

from mockex.config import DEFAULT_CONFIG_PATH, MockConfig, load_config
from mockex.wiring import ADMIN_HOST, PRIVATE_PATH, PROTOCOL_HOST, PUBLIC_PATH, MockService

__all__ = ["main", "parse_args", "prepare_config"]

logger = logging.getLogger("mockex.run")

_ROOT = Path(__file__).resolve().parent   # mock_exchange/：相对 cert_dir 的锚点


def parse_args(argv=None) -> argparse.Namespace:
    """命令行参数：只有 `--config`（缺省 `mock_exchange/config/mock.yml`）。"""
    parser = argparse.ArgumentParser(prog="mock_exchange",
                                     description="本地假 OKX 交易所：REST + WS(TLS) + 管理面")
    parser.add_argument("--config", default=str(DEFAULT_CONFIG_PATH),
                        help="配置文件路径（缺省 %(default)s）")
    return parser.parse_args(argv)


def prepare_config(path) -> MockConfig:
    """读配置，并把相对的 `tls.cert_dir` 锚到 `mock_exchange/`（绝对路径原样保留）。

    Raises:
        FileNotFoundError: 配置文件不存在（缺键才回落默认值，文件缺失是错误）。
        ValueError / yaml.YAMLError: 配置写坏（`load_config` 报）。
    """
    config_path = Path(path).expanduser().resolve()
    config = load_config(config_path)
    cert_dir = Path(config.tls.cert_dir).expanduser()
    if not cert_dir.is_absolute():
        config = replace(config, tls=replace(config.tls, cert_dir=str(_ROOT / cert_dir)))
    return config


async def _serve(service: MockService) -> None:
    """起服务并常驻，直到被 Ctrl-C 打断（`finally` 里关站点）。

    日志里打印的是**实际绑定主机**（来自 `wiring.PROTOCOL_HOST` / `ADMIN_HOST`）：REST/WS 是
    `0.0.0.0`（对外，容器外/宿主机的引擎要连），管理面是 `127.0.0.1`。写死 `127.0.0.1` 会低估
    对外暴露面，排障时也看不出"外部连不上是不是没绑对网卡"。
    """
    await service.start()
    ports = service.ports
    logger.info("REST      http://%s:%d（对外）", PROTOCOL_HOST, ports["rest"])
    logger.info("WS (TLS)  wss://%s:%d%s（对外）", PROTOCOL_HOST, ports["ws"], PUBLIC_PATH)
    logger.info("WS (TLS)  wss://%s:%d%s（对外）", PROTOCOL_HOST, ports["ws"], PRIVATE_PATH)
    logger.info("管理面    http://%s:%d（只绑本机）", ADMIN_HOST, ports["admin"])
    logger.info("按 Ctrl-C 停止")
    try:
        await asyncio.Event().wait()    # 常驻：站点各自在自己的任务里跑
    finally:
        await service.stop()
        logger.info("mock_exchange 已停止")


def main(argv=None) -> int:
    """CLI 入口：0 = 正常退出，2 = 起不来（配置 / 装配 / 端口 / 证书）。"""
    args = parse_args(argv)
    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    try:
        config = prepare_config(args.config)
        service = MockService(config)   # 装配期校验：feed.source / fill_mode / instruments…
    except (OSError, ValueError, yaml.YAMLError) as exc:
        logger.error("启动失败: %s", exc)
        return 2
    logger.info("配置: %s（证书目录 %s）", Path(args.config).expanduser(), config.tls.cert_dir)
    try:
        asyncio.run(_serve(service))
    except KeyboardInterrupt:
        logger.info("收到中断，退出")
    except (OSError, RuntimeError) as exc:      # 端口占用 / 证书生成失败：同样是起不来
        logger.error("启动失败: %s", exc)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
