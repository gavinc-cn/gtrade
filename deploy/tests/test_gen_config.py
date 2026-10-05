#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""deploy/gen_config.py 的单元测试。

用临时目录构造迷你「模板 + 模式覆盖」仓库（不依赖仓库真实配置），
直接调用 gen_config.generate()/discover_modes() 断言校验与合并行为。
运行: <python> -m pytest deploy/tests/ -v （需 PyYAML，my_pyenv 已装）
"""

import os
import sys

import yaml

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.realpath(__file__))))

import gen_config  # noqa: E402

# 迷你模板集合：结构与真实 config/ 同构（4 文件名），内容最小化
TEMPLATE_FILES = {
    "config.yml": """\
strategy_config:
  - strategy_config/a.yml
logger:
  log_file: "log/gtrade.log"
  async: false
url:
  okx:
    rest: "https://tpl.example"
    ws_public: "wss://tpl.example/pub"
""",
    "data_config.yml": "scheduler:\n  tick_ms: 100\n",
    "slack_config.yml": "default_channel: dev\n",
    "backtest_config.yml": "start_date: 20250101_0000\n",
}

MOCK_OVERRIDE = """\
strategy_config:
  - strategy_config/b.yml
logger:
  async: true
url:
  okx:
    rest: "http://127.0.0.1:18080"
"""


def make_repo(tmp_path, mode="dev", mode_files=None, extra_entries=()):
    """构造迷你仓库: <root>/config(模板) + <root>/deploy/config/<mode>(覆盖)。
    返回 (root, mode_dir, out_dir)。"""
    root = tmp_path / "repo"
    (root / "config").mkdir(parents=True)
    mode_dir = root / "deploy" / "config" / mode
    mode_dir.mkdir(parents=True)
    for name, text in TEMPLATE_FILES.items():
        (root / "config" / name).write_text(text, encoding="utf-8")
    for name, text in (mode_files or {}).items():
        (mode_dir / name).write_text(text, encoding="utf-8")
    for entry in extra_entries:
        (mode_dir / entry).write_text("x: 1\n", encoding="utf-8")
    out_dir = tmp_path / "out"
    out_dir.mkdir()
    return root, mode_dir, out_dir


def read_out(out_dir, name):
    with open(out_dir / "config" / name, encoding="utf-8") as f:
        text = f.read()
    assert text.startswith("# 本文件由 deploy/gen_config.py 生成"), "产物缺生成头注释"
    return yaml.safe_load(text)


def test_unknown_key_reports_full_path(tmp_path):
    """覆盖配置出现模板没有的键：报错且键路径完整（深层嵌套）。"""
    _, _, out_dir = make_repo(
        tmp_path, mode_files={"config.yml": "url:\n  okx:\n    rest: x\n    ws_typo: y\n"})
    ok, problems, _ = gen_config.generate("dev", str(out_dir), str(tmp_path / "repo"))
    assert not ok
    assert any("url.okx.ws_typo" in p and "模板中不存在" in p for p in problems)
    assert not (out_dir / "config" / "config.yml").exists(), "校验失败不允许落盘"


def test_type_mismatch_reports_error(tmp_path):
    """覆盖值与模板结构类型错位（map 换标量 / 标量换 list）要报错。"""
    _, _, out_dir = make_repo(tmp_path, mode_files={"config.yml": "logger: oops\n"})
    ok, problems, _ = gen_config.generate("dev", str(out_dir), str(tmp_path / "repo"))
    assert not ok
    assert any("logger" in p and "类型错位" in p for p in problems)


def test_unknown_entry_rejected(tmp_path):
    """模式目录出现模板集合之外的未知文件/目录 → 报错（防拼写错位）。"""
    _, _, out_dir = make_repo(tmp_path, extra_entries=("cfg.yml", "strategy_confog"))
    ok, problems, _ = gen_config.generate("dev", str(out_dir), str(tmp_path / "repo"))
    assert not ok
    for entry in ("cfg.yml", "strategy_confog"):
        assert any(entry in p and "未知" in p for p in problems)


def test_tolreated_entry_allowed(tmp_path):
    """随迁部署文件（docker-compose.yml）允许共存：不合并、不复制、不报错。"""
    _, _, out_dir = make_repo(tmp_path, extra_entries=("docker-compose.yml",))
    ok, problems, _ = gen_config.generate("dev", str(out_dir), str(tmp_path / "repo"))
    assert ok, problems
    assert not (out_dir / "docker-compose.yml").exists(), "容忍条目不应被复制到输出"


def test_deep_merge_semantics(tmp_path):
    """深合并：dict 递归覆盖、list 整体替换、模板 null 占位被覆盖、无覆盖文件直出模板。"""
    # 模板 strategy_config 键全注释 → 解析为 null 占位
    root = tmp_path / "repo"
    (root / "config").mkdir(parents=True)
    (root / "deploy" / "config" / "dev").mkdir(parents=True)
    (root / "config" / "config.yml").write_text(
        "strategy_config:\n#  - strategy_config/commented.yml\n"
        + TEMPLATE_FILES["config.yml"].split("strategy_config:", 1)[1], encoding="utf-8")
    for name, text in TEMPLATE_FILES.items():
        if name != "config.yml":
            (root / "config" / name).write_text(text, encoding="utf-8")
    (root / "deploy" / "config" / "dev" / "config.yml").write_text(
        MOCK_OVERRIDE, encoding="utf-8")
    out_dir = tmp_path / "out"
    out_dir.mkdir()

    ok, problems, _ = gen_config.generate("dev", str(out_dir), str(root))
    assert ok, problems

    cfg = read_out(out_dir, "config.yml")
    # dict 递归：仅覆盖的键变化，兄弟键保留模板值
    assert cfg["logger"] == {"log_file": "log/gtrade.log", "async": True}
    # 深层覆盖，未提及的兄弟键保留
    assert cfg["url"]["okx"]["rest"] == "http://127.0.0.1:18080"
    assert cfg["url"]["okx"]["ws_public"] == "wss://tpl.example/pub"
    # list 整体替换（而非逐元素合并）
    assert cfg["strategy_config"] == ["strategy_config/b.yml"]

    # 无覆盖文件 → 模板直出
    assert read_out(out_dir, "slack_config.yml") == {"default_channel": "dev"}


def test_null_template_placeholder_accepts_any_type(tmp_path):
    """模板值为 null（占位）时，覆盖层可给任意类型（含标量）。"""
    root = tmp_path / "repo"
    (root / "config").mkdir(parents=True)
    (root / "deploy" / "config" / "dev").mkdir(parents=True)
    (root / "config" / "config.yml").write_text("strategy_config:\nlogger:\n  async: false\n",
                                                encoding="utf-8")
    for name, text in TEMPLATE_FILES.items():
        if name != "config.yml":
            (root / "config" / name).write_text(text, encoding="utf-8")
    (root / "deploy" / "config" / "dev" / "config.yml").write_text(
        "strategy_config: strategy_config/single.yml\n", encoding="utf-8")
    ok, problems, _ = gen_config.generate("dev", str(tmp_path / "out"), str(root))
    assert ok, problems


def test_strategy_config_dir_copied(tmp_path):
    """模式目录下的 strategy_config/ 原样复制到输出（不参与合并）。"""
    _, mode_dir, out_dir = make_repo(
        tmp_path, mode_files={"config.yml": "logger:\n  async: true\n"})
    strat = mode_dir / "strategy_config"
    strat.mkdir()
    (strat / "my_strat.yml").write_text("params:\n  n: 1\n", encoding="utf-8")
    ok, problems, _ = gen_config.generate("dev", str(out_dir), str(tmp_path / "repo"))
    assert ok, problems
    assert (out_dir / "strategy_config" / "my_strat.yml").read_text(encoding="utf-8") \
        == "params:\n  n: 1\n"


def test_mode_not_found_and_list(tmp_path):
    """模式不存在 → 报错并列出 deploy/config/ 下可用模式；--list 走 discover_modes。"""
    root, _, out_dir = make_repo(tmp_path, mode="dev")
    (root / "deploy" / "config" / "prod").mkdir()
    ok, problems, _ = gen_config.generate("nope", str(out_dir), str(root))
    assert not ok
    assert any("nope" in p and "dev" in p and "prod" in p for p in problems)
    assert gen_config.discover_modes(str(root)) == ["dev", "prod"]


def test_dry_run_writes_nothing(tmp_path):
    """--dry-run 语义：generate(dry_run=True) 校验通过但不写任何文件。"""
    _, _, out_dir = make_repo(tmp_path, mode_files={"config.yml": "logger:\n  async: true\n"})
    ok, problems, _ = gen_config.generate("dev", str(out_dir), str(tmp_path / "repo"),
                                          dry_run=True)
    assert ok, problems
    assert not (out_dir / "config").exists()
