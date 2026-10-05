#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""deploy/svc.py 配置模式取值测试（命令行 -c 与环境变量 GTRADE_MODE 的优先级）。

被验证的契约：start/restart 的配置模式来源优先级为「命令行 -c > 环境变量 GTRADE_MODE」，
两者都缺才算用法错误（退出码 2）；空值（"" / 全空白）按未设置处理；
stop/status/clean 不受该环境变量影响（不会因其存在而报错或改变行为）。

只验证参数解析与优先级：把 stop_one / start_one 换成记录调用的假实现，
不触碰真实进程（不会真的停/起任何服务），也不依赖 docker / 端口 / 二进制。
运行: <python> -m pytest deploy/tests/ -v
"""

import os
import sys
import types

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.realpath(__file__))))

import svc  # noqa: E402


def run_main(monkeypatch, argv):
    """在假进程动作下执行 svc.main(argv)。

    返回 (退出码, 调用记录 [(动作, 服务名), ...])；main 恒以 SystemExit 结束，
    故用 pytest.raises 捕获其退出码。sleep 只用于节流，测试里跳过。
    """
    calls = []

    def fake_stop(name):
        calls.append(("stop", name))
        return True

    def fake_start(name):
        calls.append(("start", name))
        return True

    monkeypatch.setattr(svc, "stop_one", fake_stop)
    monkeypatch.setattr(svc, "start_one", fake_start)
    # 替换为独立命名空间，避免动到全局 time.sleep
    monkeypatch.setattr(svc, "time", types.SimpleNamespace(sleep=lambda *_: None))
    monkeypatch.setattr(svc, "CONFIG_MODE", None)

    with pytest.raises(SystemExit) as exc:
        svc.main(argv)
    return exc.value.code, calls


def test_cli_mode_overrides_env(monkeypatch):
    """命令行 -c prod 与 GTRADE_MODE=dev 同时存在时，取命令行值"""
    monkeypatch.setenv("GTRADE_MODE", "dev")
    code, calls = run_main(monkeypatch, ["-c", "prod", "start", "gtrade"])

    assert code == 0
    assert svc.CONFIG_MODE == "prod"
    assert calls == [("start", "gtrade")]


def test_env_mode_used_without_cli(monkeypatch, capsys):
    """只给 GTRADE_MODE 时不再强制要求 -c，且提示模式来源便于排查"""
    monkeypatch.setenv("GTRADE_MODE", "mock")
    code, calls = run_main(monkeypatch, ["start", "gtrade"])

    assert code == 0
    assert svc.CONFIG_MODE == "mock"
    assert calls == [("start", "gtrade")]
    assert "GTRADE_MODE=mock" in capsys.readouterr().out


def test_env_mode_used_for_restart(monkeypatch):
    """restart 同样支持环境变量模式（停一次起一次）"""
    monkeypatch.setenv("GTRADE_MODE", "sit")
    code, calls = run_main(monkeypatch, ["restart", "gtrade"])

    assert code == 0
    assert svc.CONFIG_MODE == "sit"
    assert calls == [("stop", "gtrade"), ("start", "gtrade")]


def test_missing_cli_and_env_is_usage_error(monkeypatch, capsys):
    """-c 与环境变量都缺 -> 用法错误（退出码 2），不启动任何服务"""
    monkeypatch.delenv("GTRADE_MODE", raising=False)
    code, calls = run_main(monkeypatch, ["start", "gtrade"])

    assert code == 2
    assert svc.CONFIG_MODE is None
    assert calls == []
    # 报错信息要同时给出两条途径与可用模式
    err = capsys.readouterr().err
    assert "-c <mode>" in err and "GTRADE_MODE" in err and "dev" in err


def test_blank_env_treated_as_unset(monkeypatch):
    """GTRADE_MODE 只写空白等于未设置（不把空模式透传给 gen_config.py）"""
    monkeypatch.setenv("GTRADE_MODE", "   ")
    code, calls = run_main(monkeypatch, ["start", "gtrade"])

    assert code == 2
    assert svc.CONFIG_MODE is None
    assert calls == []


def test_env_mode_ignored_by_stop(monkeypatch):
    """stop 不需要模式：环境变量在场也照常执行，且不会写 CONFIG_MODE"""
    monkeypatch.setenv("GTRADE_MODE", "dev")
    code, calls = run_main(monkeypatch, ["stop", "gtrade"])

    assert code == 0
    assert svc.CONFIG_MODE is None
    assert calls == [("stop", "gtrade")]


def test_env_config_mode_helper(monkeypatch):
    """env_config_mode: 未设置/空串 -> None；有值 -> 去掉首尾空白后的模式名"""
    monkeypatch.delenv("GTRADE_MODE", raising=False)
    assert svc.env_config_mode() is None

    monkeypatch.setenv("GTRADE_MODE", "")
    assert svc.env_config_mode() is None

    monkeypatch.setenv("GTRADE_MODE", "  prod  ")
    assert svc.env_config_mode() == "prod"
