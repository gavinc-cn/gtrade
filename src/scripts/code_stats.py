#!/usr/bin/env python3
"""
代码统计脚本
统计每月代码修改行数、平均每月修改行数、当前代码总行数
"""
import subprocess
import os
from collections import defaultdict


# 需要统计的文件扩展名
CODE_EXTENSIONS = {
    '.cpp', '.hpp', '.h', '.c', '.cc', '.cxx',  # C/C++
    '.py',                                        # Python
    '.js', '.ts', '.vue', '.jsx', '.tsx',        # JavaScript/TypeScript
    '.yml', '.yaml',                              # YAML
    '.cmake',                                     # CMake
}

# 排除的目录 (依赖库、构建产物、IDE配置等)
EXCLUDE_DIRS = {
    # 构建目录
    'build', 'build_new', 'cmake-build-debug', 'cmake-build-release',
    'cmake-build-debug-dockerubuntu24', 'cmake-build-release-visual-studio',
    'CMakeFiles',
    # 依赖库
    'node_modules', '3rd', 'conan_out', 'conan_lib', 'vcpkg_installed',
    # 生成的代码
    'proto', 'generated',
    # Python 缓存
    '__pycache__', 'dist', '.cache', 'venv', '.venv',
    # IDE 和工具配置
    '.git', '.idea', '.vscode', '.obsidian', '.trae', '.devcontainer', '.vercel', '.claude',
    # 其他
    'bak', 'release', 'screen', 'doc', 'doc_ai',
}

# Git 统计时排除的文件模式 (自动生成的代码、文档等)
EXCLUDE_FILE_PATTERNS = [
    # Protobuf 生成的文件
    '.pb.cc', '.pb.h', '.grpc.pb.cc', '.grpc.pb.h',
    # 文档文件
    '.md',
    # 包管理器锁文件
    'package-lock.json', 'yarn.lock', 'pnpm-lock.yaml',
]

# Git 统计时排除的目录模式
EXCLUDE_PATH_PATTERNS = [
    'conan_out/', 'conan_lib/', 'generated/', 'proto/',
    'node_modules/', '3rd/', 'vcpkg_installed/',
    'build/', 'cmake-build-', 'CMakeFiles/',
    'doc/', 'doc_ai/', 'docs/',  # 文档目录
]


def should_exclude_file(filepath):
    """
    判断文件是否应该被排除在 git 统计之外
    """
    # 检查文件名模式
    for pattern in EXCLUDE_FILE_PATTERNS:
        if filepath.endswith(pattern):
            return True

    # 检查路径模式
    for pattern in EXCLUDE_PATH_PATTERNS:
        if pattern in filepath:
            return True

    return False


def git_stats_by_month(author=None, path=".", exclude_generated=True):
    """
    统计每月代码修改行数
    参数:
        author: 按作者过滤
        path: 仓库路径
        exclude_generated: 是否排除自动生成的代码 (protobuf, conan 等)
    返回: stats字典, 包含每月的added和deleted
    """
    cmd = ["git", "log", "--pretty=format:%ad", "--date=format:%Y-%m", "--numstat"]
    if author:
        cmd += ["--author", author]

    result = subprocess.run(cmd, cwd=path, capture_output=True, text=True)

    stats = defaultdict(lambda: {"added": 0, "deleted": 0})
    excluded_stats = defaultdict(lambda: {"added": 0, "deleted": 0})
    current_month = None

    for line in result.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        if line[:4].isdigit() and "-" in line and len(line) == 7:
            current_month = line
        elif "\t" in line and current_month:
            parts = line.split("\t")
            if len(parts) >= 3 and parts[0].isdigit() and parts[1].isdigit():
                filepath = parts[2]
                added = int(parts[0])
                deleted = int(parts[1])

                if exclude_generated and should_exclude_file(filepath):
                    excluded_stats[current_month]["added"] += added
                    excluded_stats[current_month]["deleted"] += deleted
                else:
                    stats[current_month]["added"] += added
                    stats[current_month]["deleted"] += deleted

    return stats, excluded_stats


def count_current_lines(path="."):
    """
    统计当前代码总行数
    返回: (总行数, 文件数, 按扩展名分类的行数字典)
    """
    total_lines = 0
    total_files = 0
    lines_by_ext = defaultdict(int)
    files_by_ext = defaultdict(int)

    for root, dirs, files in os.walk(path):
        # 排除指定目录
        dirs[:] = [d for d in dirs if d not in EXCLUDE_DIRS]

        for filename in files:
            ext = os.path.splitext(filename)[1].lower()
            if ext not in CODE_EXTENSIONS:
                continue

            filepath = os.path.join(root, filename)
            try:
                with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                    line_count = sum(1 for _ in f)
                    total_lines += line_count
                    total_files += 1
                    lines_by_ext[ext] += line_count
                    files_by_ext[ext] += 1
            except (IOError, OSError):
                continue

    return total_lines, total_files, lines_by_ext, files_by_ext


def print_monthly_stats(stats, excluded_stats=None):
    """打印每月统计数据"""
    print("\n" + "=" * 50)
    print("每月代码修改统计" + (" (已过滤自动生成代码)" if excluded_stats else ""))
    print("=" * 50)
    print(f"{'Month':<10} {'Added':>10} {'Deleted':>10} {'Changed':>10}")
    print("-" * 50)

    total_added = 0
    total_deleted = 0

    # 合并所有月份
    all_months = set(stats.keys())
    if excluded_stats:
        all_months |= set(excluded_stats.keys())

    for month in sorted(all_months):
        a = stats.get(month, {}).get("added", 0)
        d = stats.get(month, {}).get("deleted", 0)
        total_added += a
        total_deleted += d
        print(f"{month:<10} {a:>+10} {d:>10} {a+d:>10}")

    print("-" * 50)
    print(f"{'Total':<10} {total_added:>+10} {total_deleted:>10} {total_added+total_deleted:>10}")

    # 显示被过滤的统计
    if excluded_stats:
        excluded_added = sum(s["added"] for s in excluded_stats.values())
        excluded_deleted = sum(s["deleted"] for s in excluded_stats.values())
        if excluded_added > 0 or excluded_deleted > 0:
            print(f"\n已过滤 (自动生成代码): +{excluded_added:,} / -{excluded_deleted:,} 行")

    return total_added, total_deleted


def print_average_stats(stats, total_added, total_deleted):
    """打印平均每月修改行数"""
    month_count = len(stats)
    if month_count == 0:
        print("\n无可用的月度数据")
        return

    avg_added = total_added / month_count
    avg_deleted = total_deleted / month_count
    avg_changed = (total_added + total_deleted) / month_count

    print("\n" + "=" * 50)
    print("平均每月代码修改统计")
    print("=" * 50)
    print(f"统计月份数:     {month_count}")
    print(f"平均每月新增:   {avg_added:,.0f} 行")
    print(f"平均每月删除:   {avg_deleted:,.0f} 行")
    print(f"平均每月修改:   {avg_changed:,.0f} 行 (新增+删除)")


def print_current_lines(total_lines, total_files, lines_by_ext, files_by_ext):
    """打印当前代码总行数"""
    print("\n" + "=" * 50)
    print("当前代码总行数统计")
    print("=" * 50)
    print(f"{'Extension':<12} {'Files':>8} {'Lines':>12} {'Percent':>10}")
    print("-" * 50)

    # 按行数排序
    sorted_exts = sorted(lines_by_ext.items(), key=lambda x: x[1], reverse=True)
    for ext, lines in sorted_exts:
        files = files_by_ext[ext]
        percent = (lines / total_lines * 100) if total_lines > 0 else 0
        print(f"{ext:<12} {files:>8} {lines:>12,} {percent:>9.1f}%")

    print("-" * 50)
    print(f"{'Total':<12} {total_files:>8} {total_lines:>12,} {'100.0':>9}%")


def get_project_root():
    """
    自动获取项目根目录
    通过查找 .git 目录来定位（最可靠的标识）
    """
    # 从脚本所在目录开始向上查找
    script_dir = os.path.dirname(os.path.abspath(__file__))
    current = script_dir

    while current != '/':
        git_dir = os.path.join(current, '.git')
        if os.path.exists(git_dir):
            return current
        current = os.path.dirname(current)

    # 如果找不到，返回当前工作目录
    return os.getcwd()


def main():
    """主函数"""
    import argparse

    # 获取默认项目路径
    default_path = get_project_root()

    parser = argparse.ArgumentParser(description='代码统计脚本')
    parser.add_argument('--author', '-a', help='按作者过滤git提交')
    parser.add_argument('--path', '-p', default=default_path,
                        help=f'项目路径 (默认: {default_path})')
    parser.add_argument('--no-filter', action='store_true',
                        help='不过滤自动生成的代码 (protobuf, conan 等)')
    args = parser.parse_args()

    print(f"统计路径: {os.path.abspath(args.path)}")

    # 1. 统计每月代码修改
    exclude_generated = not args.no_filter
    stats, excluded_stats = git_stats_by_month(
        author=args.author,
        path=args.path,
        exclude_generated=exclude_generated
    )
    total_added, total_deleted = print_monthly_stats(
        stats,
        excluded_stats if exclude_generated else None
    )

    # 2. 打印平均每月修改行数
    print_average_stats(stats, total_added, total_deleted)

    # 3. 统计当前代码总行数
    total_lines, total_files, lines_by_ext, files_by_ext = count_current_lines(args.path)
    print_current_lines(total_lines, total_files, lines_by_ext, files_by_ext)


if __name__ == "__main__":
    main()