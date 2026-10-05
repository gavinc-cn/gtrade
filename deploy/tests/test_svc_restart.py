#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""deploy/svc.py 的命令编排单元测试（restart 两阶段顺序）。

只验证命令编排：把 stop_one / start_one 换成记录调用的假实现，
不触碰真实进程（不会真的停/起任何服务），也不依赖 docker / 端口 / 二进制。
运行: <python> -m pytest deploy/tests/ -v
"""

import os
import sys
import types

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.realpath(__file__))))

import svc  # noqa: E402


def install_recorder(monkeypatch, stop_fail=(), start_fail=()):
    """把 svc 的停/起动作替换为记录器，返回记录列表 [(动作, 服务名), ...]。
    stop_fail / start_fail 中的服务名返回失败，用于验证退出码聚合。"""
    calls = []

    def fake_stop(name):
        calls.append(("stop", name))
        return name not in stop_fail

    def fake_start(name):
        calls.append(("start", name))
        return name not in start_fail

    monkeypatch.setattr(svc, "stop_one", fake_stop)
    monkeypatch.setattr(svc, "start_one", fake_start)
    # sleep 只用于节流，测试里跳过；替换为独立命名空间，避免动到全局 time.sleep
    monkeypatch.setattr(svc, "time", types.SimpleNamespace(sleep=lambda *_: None))
    return calls


def phases(calls):
    """把记录拆成 (停止序列, 启动序列)"""
    return ([n for a, n in calls if a == "stop"], [n for a, n in calls if a == "start"])


def test_restart_all_stops_first_then_starts_in_dependency_order(monkeypatch):
    """全量 restart: 先按依赖逆序全停（mysql 最后），再按依赖顺序全起（mysql 最先）"""
    calls = install_recorder(monkeypatch)
    assert svc.cmd_restart(list(svc.SERVICES_START_ORDER)) == 0

    stops, starts = phases(calls)
    assert stops == ["mcp", "web_client", "web_server", "gtrade", "repl", "mysql"]
    assert starts == svc.SERVICES_START_ORDER

    # 关键性质：所有停止动作都排在第一个启动动作之前（不再逐个先停后起）
    first_start = next(i for i, (action, _) in enumerate(calls) if action == "start")
    assert all(action == "stop" for action, _ in calls[:first_start])


def test_restart_argument_order_does_not_matter(monkeypatch):
    """服务名参数顺序被打乱时，两个阶段仍各自按依赖顺序处理"""
    calls = install_recorder(monkeypatch)
    assert svc.cmd_restart(["repl", "mysql", "mcp", "gtrade"]) == 0

    stops, starts = phases(calls)
    assert stops == ["mcp", "gtrade", "repl", "mysql"]  # gtrade 先于 repl 停
    assert starts == ["mysql", "gtrade", "repl", "mcp"]  # gtrade 先于 repl 起


def test_restart_gtrade_stops_before_repl(monkeypatch):
    """restart 含 gtrade+repl 时，停止阶段 gtrade 必须排在 repl 前（repl 处理遗留 WAL）"""
    calls = install_recorder(monkeypatch)
    assert svc.cmd_restart(["repl", "gtrade"]) == 0

    stops, starts = phases(calls)
    assert stops == ["gtrade", "repl"]
    assert starts == ["gtrade", "repl"]


def test_restart_single_service(monkeypatch):
    """单服务 restart: 停一次起一次，不夹带其他服务"""
    calls = install_recorder(monkeypatch)
    assert svc.cmd_restart(["gtrade"]) == 0
    assert phases(calls) == (["gtrade"], ["gtrade"])


def test_restart_exit_code_aggregates_both_phases(monkeypatch):
    """停止阶段与启动阶段任一失败，退出码都应为 1"""
    install_recorder(monkeypatch, stop_fail=("web_client",), start_fail=("mcp",))
    assert svc.cmd_restart(list(svc.SERVICES_START_ORDER)) == 1

    install_recorder(monkeypatch)
    assert svc.cmd_restart(list(svc.SERVICES_START_ORDER)) == 0


def test_order_by_dependency_unknown_last_and_stable():
    """order 中不存在的服务排最后；同秩服务保持调用方给定顺序"""
    assert svc.order_by_dependency(["mcp", "gtrade", "web_server"],
                                   svc.SERVICES_START_ORDER) == ["gtrade", "web_server", "mcp"]
    assert svc.order_by_dependency(["b", "a"], ["a"]) == ["a", "b"]


def test_main_restart_wires_config_mode(monkeypatch):
    """CLI 入口: main(['-c','dev','restart','gtrade']) 走两阶段重启并置 CONFIG_MODE"""
    calls = install_recorder(monkeypatch)
    monkeypatch.setattr(svc, "CONFIG_MODE", None)
    with pytest.raises(SystemExit) as exc:
        svc.main(["-c", "dev", "restart", "gtrade"])
    assert exc.value.code == 0
    assert phases(calls) == (["gtrade"], ["gtrade"])
    assert svc.CONFIG_MODE == "dev"


def test_main_restart_all_without_service_names(monkeypatch):
    """`svc.sh -c dev restart`（不带服务名）= 先全停（mysql 最后）再全起（mysql 第一）"""
    calls = install_recorder(monkeypatch)
    monkeypatch.setattr(svc, "CONFIG_MODE", None)
    with pytest.raises(SystemExit) as exc:
        svc.main(["-c", "dev", "restart"])
    assert exc.value.code == 0

    stops, starts = phases(calls)
    assert stops == ["mcp", "web_client", "web_server", "gtrade", "repl", "mysql"]
    assert starts == svc.SERVICES_START_ORDER
