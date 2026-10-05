#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# =====================================================================
# GTrade 配置生成器：模板层 + 模式覆盖层 → 完整运行配置
#
# 「模板层」是 config/*.yml（完整默认值，可直接运行的开发默认配置）；
# 「模式覆盖层」是 deploy/config/<mode>/ 下的稀疏覆盖配置（键必须 ⊆ 模板）。
# 本工具把两层深度合并，生成完整配置写入构建产物目录 <bin>/config/，
# gtrade / gtrade_bt 按相对路径消费（src/gtrade.cpp:97），C++ 侧零改动。
#
# 校验规则（失败一次性列出全部问题，非零码退出，错误配置到不了程序）：
#   1. 覆盖配置任意层级出现模板没有的键 → 报错（含完整键路径）
#   2. 覆盖值与模板值结构类型错位（map/list/标量）→ 报错
#   3. 模式目录出现模板集合之外的未知文件/目录 → 报错（防拼写错位）
#   4. 模式名不存在 → 报错并列出 deploy/config/ 下全部可用模式
#
# 合并语义：dict 递归合并；list 与标量整体替换（如 strategy_config 列表由
# 覆盖层整体决定）；模板值为 null 视为占位，可被覆盖层的任意类型替换。
#
# 用法:
#   gen_config.py -c <mode> [-o <bin_dir>] [--dry-run] [--list]
#     -c <mode>    模式名 = deploy/config/ 下子目录名（动态发现，不写死）
#     -o <bin_dir> 输出目录（默认 GTRADE_BIN_DIR > 自动探测最新构建产物，同 svc.py）
#     --dry-run    只校验与预览，不写任何文件
#     --list       列出全部可用模式
# 退出码: 0 成功 / 1 校验或生成失败 / 2 用法错误
#
# 依赖: PyYAML（my_pyenv 已装）。svc.py 经子进程调用本脚本（svc 自身保持纯标准库）。
# =====================================================================

import argparse
import glob
import os
import shutil
import sys

import yaml

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))

# 参与合并机制的模板文件固定集合（config/ 顶层 yml）。
# 不动态扫描 config/ 目录：ctp_account_example.yml 是示例文档、不参与部署。
TEMPLATE_NAMES = ("config.yml", "data_config.yml", "slack_config.yml", "backtest_config.yml")
# 模式目录下除模板同名文件外允许出现的目录：原样复制到输出（不参与合并）
COPY_THROUGH_DIRS = ("strategy_config",)
# 模式目录下允许共存、但既不合并也不复制的随迁部署文件（如 prod 的 docker-compose.yml）
TOLERATED_ENTRIES = ("docker-compose.yml",)


def resolve_project_root():
    """从脚本目录逐级向上查找同时存在 web_server / mcp_server / src 的目录（与 svc.py 同规则）。
    源码布局：deploy/ 的上一级即为项目根；安装布局：<build>/bin/ 的上两级为项目根。"""
    dir_ = SCRIPT_DIR
    for _ in range(4):
        if all(os.path.isdir(os.path.join(dir_, d)) for d in ("web_server", "mcp_server", "src")):
            return dir_
        dir_ = os.path.dirname(dir_)
    return None


def resolve_bin_dir(project_root):
    """输出目录默认值：GTRADE_BIN_DIR 显式指定 > 脚本同级目录(安装布局) > 自动探测最新构建产物。
    找不到返回 None（调用方提示用 -o 显式指定）。"""
    env_dir = os.environ.get("GTRADE_BIN_DIR", "")
    if env_dir:
        return env_dir
    if os.path.isfile(os.path.join(SCRIPT_DIR, "gtrade")):
        return SCRIPT_DIR
    candidates = []
    for pat in (os.path.join(project_root, "cmake-build-*", "bin"),
                os.path.join(project_root, "build", "bin")):
        candidates.extend(p for p in glob.glob(pat) if os.path.isfile(os.path.join(p, "gtrade")))
    if candidates:
        return os.path.dirname(max(candidates, key=os.path.getmtime))
    return None


def discover_modes(project_root):
    """列出 deploy/config/ 下全部模式（子目录名，字母序）。目录不存在视为无模式。"""
    modes_dir = os.path.join(project_root, "deploy", "config")
    if not os.path.isdir(modes_dir):
        return []
    return sorted(e for e in os.listdir(modes_dir)
                  if os.path.isdir(os.path.join(modes_dir, e)) and not e.startswith("."))


def load_yaml(path):
    """读取 YAML 文件，返回 (ok, 数据, 错误描述)。空文件返回 (True, None, "")。"""
    try:
        with open(path, "r", encoding="utf-8") as f:
            return True, yaml.safe_load(f), ""
    except yaml.YAMLError as e:
        return False, None, f"{path}: YAML 解析失败: {e}"
    except OSError as e:
        return False, None, f"{path}: 读取失败: {e}"


def _kind(node):
    """YAML 值的结构类别：map / seq / scalar。None（null）单独由调用方处理。"""
    if isinstance(node, dict):
        return "map"
    if isinstance(node, list):
        return "seq"
    return "scalar"


def merge_and_validate(template, actual, path, problems):
    """深度合并模板与覆盖配置，把发现的问题追加进 problems（含完整键路径）。

    规则：dict 递归合并；list 与标量整体替换；模板为 null 视为占位（覆盖层可给
    任意类型）；其余情况覆盖层与模板结构类别不一致即记为类型错位。
    有问题时返回值无意义（调用方在 problems 非空时不落盘）。"""
    if template is None:
        # 模板空值占位（如 config.yml 中被整体注释掉的 strategy_config:），覆盖层任意类型均可
        return actual
    tkind = _kind(template)
    akind = _kind(actual)
    if tkind != akind:
        problems.append(f"{path or '<根>'}: 模板为 {tkind}，覆盖为 {akind}（类型错位）")
        return actual
    if tkind == "map":
        merged = dict(template)
        for key, aval in actual.items():
            kpath = f"{path}.{key}" if path else str(key)
            if key not in template:
                problems.append(f"{kpath}: 模板中不存在")
                continue
            merged[key] = merge_and_validate(template[key], aval, kpath, problems)
        return merged
    return actual


def generate(mode, out_dir, project_root, dry_run=False):
    """生成指定模式的完整配置。返回 (ok, problems, summary_lines)。

    ok=False 时 problems 含全部问题（已保证一次报全）；ok=True 时 summary_lines
    是给用户看的结果摘要（写盘或 dry-run 预览）。"""
    modes_dir = os.path.join(project_root, "deploy", "config")
    mode_dir = os.path.join(modes_dir, mode)
    if not os.path.isdir(mode_dir):
        available = " ".join(discover_modes(project_root)) or "(无)"
        return False, [f"模式 '{mode}' 不存在（{modes_dir} 下可用模式: {available}）"], []

    problems = []
    merged_map = {}
    summary = []

    # 逐模板文件：有同名覆盖则校验+合并，否则模板直出
    for name in TEMPLATE_NAMES:
        template_path = os.path.join(project_root, "config", name)
        if not os.path.isfile(template_path):
            problems.append(f"模板缺失: config/{name}")
            continue
        ok, template, err = load_yaml(template_path)
        if not ok:
            problems.append(err)
            continue
        override_path = os.path.join(mode_dir, name)
        if os.path.isfile(override_path):
            ok, actual, err = load_yaml(override_path)
            if not ok:
                problems.append(err)
                continue
            merged_map[name] = merge_and_validate(template, actual, name[: -len(".yml")], problems)
            summary.append(f"  config/{name}   模板+模式覆盖")
        else:
            merged_map[name] = template
            summary.append(f"  config/{name}   模板直出")

    # 模式目录中模板集合之外的未知文件/目录 → 报错（防拼写错位）
    allowed = set(TEMPLATE_NAMES) | set(COPY_THROUGH_DIRS) | set(TOLERATED_ENTRIES)
    for entry in sorted(os.listdir(mode_dir)):
        if entry not in allowed:
            problems.append(
                f"deploy/config/{mode}/{entry}: 模板集合之外的未知文件/目录"
                f"（允许: {', '.join(TEMPLATE_NAMES)}, {', '.join(COPY_THROUGH_DIRS)}"
                f"{'（容忍不处理）: ' + ', '.join(TOLERATED_ENTRIES) if TOLERATED_ENTRIES else ''}）")

    # strategy_config/ 原样复制（不参与合并）
    strat_src = os.path.join(mode_dir, "strategy_config")
    strat_summary = None
    if os.path.isdir(strat_src):
        count = sum(len(files) for _, _, files in os.walk(strat_src))
        strat_summary = f"  strategy_config/   原样复制（{count} 个文件）"

    if problems:
        return False, problems, []

    if not dry_run:
        cfg_out = os.path.join(out_dir, "config")
        os.makedirs(cfg_out, exist_ok=True)
        header = f"# 本文件由 deploy/gen_config.py 生成，请勿手改（源: deploy/config/{mode}）\n"
        for name, data in merged_map.items():
            with open(os.path.join(cfg_out, name), "w", encoding="utf-8") as f:
                f.write(header)
                # sort_keys=False 保持模板键序；allow_unicode 保留中文
                yaml.safe_dump(data, f, allow_unicode=True, default_flow_style=False, sort_keys=False)
        if strat_src and os.path.isdir(strat_src):
            # dirs_exist_ok：覆盖同名文件、保留目标已有其他文件（策略文件由 config.yml 路径引用）
            shutil.copytree(strat_src, os.path.join(out_dir, "strategy_config"), dirs_exist_ok=True)

    if strat_summary:
        summary.append(strat_summary)
    return True, [], summary


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="gen_config.py", description="GTrade 配置生成器：模板 + 模式覆盖 → 完整运行配置")
    parser.add_argument("-c", dest="mode", metavar="<mode>",
                        help="模式名 = deploy/config/ 下子目录名")
    parser.add_argument("-o", dest="out_dir", metavar="<bin_dir>", default="",
                        help="输出目录（默认 GTRADE_BIN_DIR > 自动探测最新构建产物）")
    parser.add_argument("--dry-run", action="store_true", help="只校验与预览，不写盘")
    parser.add_argument("--list", action="store_true", help="列出全部可用模式")
    args = parser.parse_args(argv)

    project_root = resolve_project_root()
    if not project_root:
        print("错误: 无法定位 GTrade 项目根目录", file=sys.stderr)
        return 1

    if args.list:
        for m in discover_modes(project_root):
            print(m)
        return 0

    if not args.mode:
        parser.error("缺少 -c <mode>（可用 --list 查看 deploy/config/ 下的全部模式）")

    out_dir = args.out_dir or resolve_bin_dir(project_root)
    if not out_dir:
        print("错误: 无法确定输出目录（未找到构建产物），请用 -o <bin_dir> 显式指定", file=sys.stderr)
        return 1

    ok, problems, summary = generate(args.mode, out_dir, project_root, dry_run=args.dry_run)
    if ok:
        print(f"模式: {args.mode} → 输出: {out_dir}" if not args.dry_run
              else f"模式: {args.mode} → [dry-run] 输出目标: {out_dir}")
        for line in summary:
            print(line)
        print("[dry-run] 校验通过，未写盘" if args.dry_run else "配置生成完成")
        return 0
    print(f"模式 '{args.mode}' 配置校验失败，共 {len(problems)} 个问题:", file=sys.stderr)
    for p in problems:
        print(f"  - {p}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
