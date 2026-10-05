#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# =====================================================================
# GTrade 服务管理脚本（唯一实现，Python 标准库版）
#
# 统一管理 gtrade 主进程及其周边服务（repl / web_server / web_client / mcp / mysql），
# 支持整体启停、单个服务独立启停、状态查看。
#
# 入口说明:
#   推荐入口是同目录 svc.sh（薄委托到本脚本，参数与退出码经 exec 透传），
#   bash svc.sh / ./svc.py / python3 svc.py 三种调用方式行为一致。
#   历史: 本文件由 bash 版 svc.sh（700 行）行为等价移植而来（2026-09-29），
#   以便后续叠加更复杂的功能（YAML 服务定义、JSON 状态输出、健康探测等）。
#
# 用法:
#   ./svc.sh [-c <mode>] <命令> [服务名...]
#
# 命令:  start / stop / restart / status / clean
#        restart = 先把目标服务按依赖逆序全部停掉，再按依赖顺序统一启动
#        （不是逐个「停一个起一个」），服务名参数顺序不影响结果
# 配置:  start/restart 需要配置模式，取值优先级：命令行 -c <mode> > 环境变量 GTRADE_MODE，
#        两者都缺才算用法错误。模式 = deploy/config/ 下子目录名，
#        启动前由 deploy/gen_config.py 把 config/ 模板与模式覆盖合并生成完整配置
# 服务:  gtrade | repl | web_server | web_client | mcp | mysql
#        不带服务名 = 对全部服务按依赖顺序操作
#
# 退出码: 0 成功 / 1 有服务失败或未运行 / 2 用法错误
#
# 依赖约束: 只用 Python 3 标准库，不引第三方包——GTRADE_PYTHON 指向的
# 任意解释器（含无 pip 的系统 python3）都必须能直接运行。
# =====================================================================

import glob
import os
import re
import resource
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone

# ---------------------------------------------------------------------
# 基础路径解析
# ---------------------------------------------------------------------

# 脚本自身所在目录（解析软链接）。既可能是源码目录 deploy/，也可能是
# 安装产物目录 <build>/bin/（CMake 会把 svc.sh + svc.py 安装到二进制同级目录）
SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))


def capture(args):
    """运行外部命令并收集 stdout。返回 (退出码, stdout 文本)；
    命令不存在等 OSError 返回 (127, "")。stderr 一律丢弃，不抛异常——
    与 bash 版 `cmd 2>/dev/null` 后用 $() 取值的语义一致。"""
    try:
        r = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    except OSError:
        return 127, ""
    return r.returncode, r.stdout.decode(errors="replace")


def silent(args):
    """只关心退出码地运行外部命令（输出全部丢弃）。命令不存在返回 127。"""
    try:
        return subprocess.run(args, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode
    except OSError:
        return 127


def resolve_project_root():
    """从脚本目录逐级向上查找同时存在 web_server / mcp_server / src 的目录。
    源码布局：deploy/ 的上一级即为项目根；安装布局：<build>/bin/ 的上两级为项目根。"""
    dir_ = SCRIPT_DIR
    for _ in range(4):
        if all(os.path.isdir(os.path.join(dir_, d)) for d in ("web_server", "mcp_server", "src")):
            return dir_
        dir_ = os.path.dirname(dir_)
    return None


PROJECT_ROOT = resolve_project_root()
if not PROJECT_ROOT:
    print("错误: 无法定位 GTrade 项目根目录，请在仓库内或构建产物 bin 目录下运行本脚本",
          file=sys.stderr)
    sys.exit(1)


def resolve_bin_dir():
    """gtrade / gtrade_repl 可执行文件所在目录。
    优先级: GTRADE_BIN_DIR 显式指定 > 脚本同级目录(安装布局) > 自动探测最新构建产物"""
    env_dir = os.environ.get("GTRADE_BIN_DIR", "")
    if env_dir:
        if not os.access(os.path.join(env_dir, "gtrade"), os.X_OK):
            print(f"警告: {env_dir} 下没有可执行的 gtrade", file=sys.stderr)
            return None
        return env_dir
    if os.access(os.path.join(SCRIPT_DIR, "gtrade"), os.X_OK):
        return SCRIPT_DIR
    # 等价 bash: ls -1dt cmake-build-*/bin/gtrade build/bin/gtrade | head -1（按 mtime 新→旧取第一个）
    candidates = []
    for pat in (os.path.join(PROJECT_ROOT, "cmake-build-*", "bin", "gtrade"),
                os.path.join(PROJECT_ROOT, "build", "bin", "gtrade")):
        candidates.extend(p for p in glob.glob(pat) if os.access(p, os.X_OK))
    if candidates:
        newest = max(candidates, key=os.path.getmtime)
        return os.path.dirname(newest)
    return None


# GTRADE_BIN_DIR 显式指定但无效时，resolve_bin_dir 会向 stderr 报警告，这里不吞掉
BIN_DIR = resolve_bin_dir()

# 日志目录：所有服务的 nohup 输出统一落在这里，便于排查
LOG_DIR = os.environ.get("GTRADE_LOG_DIR") or os.path.join(PROJECT_ROOT, "log")


def pick_python():
    """python 解释器：优先 GTRADE_PYTHON，其次本机常用虚拟环境，最后回退系统命令"""
    env = os.environ.get("GTRADE_PYTHON", "")
    if env:
        return env
    for c in ("/opt/miniconda3/envs/my_pyenv/bin/python", "python3", "python"):
        if shutil.which(c):
            return c
    return "python3"


PYTHON = pick_python()

# MCP SSE 监听地址（沿袭原 deploy/start.sh 的取值）
MCP_HOST = os.environ.get("MCP_HOST", "127.0.0.1")
MCP_PORT = os.environ.get("MCP_PORT", "8765")

# 配置模式（-c <mode>）：start/restart 必填，由 main() 解析后写入，
# start_bin_service 启动 gtrade 前用它调用 deploy/gen_config.py 生成完整配置
CONFIG_MODE = None

# 配置模式的环境变量名：没有 -c 时的回退来源（命令行 -c 优先级更高）。
# 用途：容器/CI/发行包把模式固化在环境里（export GTRADE_MODE=prod），
# 调用方不必每次都敲 -c；命名与 GTRADE_BIN_DIR / GTRADE_PYTHON 等保持一致
CONFIG_MODE_ENV = "GTRADE_MODE"

# web_server 运行模式：development 时绑定 0.0.0.0:46011（见 web_server/app.py）。
# bash 版用 export 让子进程继承；Popen 默认继承 os.environ，这里 setdefault 即可
os.environ.setdefault("FLASK_ENV", "development")

# 服务全集的两种顺序：
#   启动 mysql -> gtrade -> repl -> web_server -> web_client -> mcp
#   停止 mcp -> web_client -> web_server -> gtrade -> repl
# 注意：gtrade 必须先于 repl 停止（repl 需要处理主进程遗留的 WAL），
#       启动时 gtrade 先起、等待 1 秒共享内存 WAL 初始化后再起 repl。
SERVICES_START_ORDER = ["mysql", "gtrade", "repl", "web_server", "web_client", "mcp"]
SERVICES_STOP_ORDER = ["mcp", "web_client", "web_server", "gtrade", "repl"]

# restart 停止阶段的顺序：在 SERVICES_STOP_ORDER 之后补 mysql。
# stop 默认不停 mysql（容器/外部实例，见 mysql_stop），但 restart 的服务全集含 mysql，
# 且 mysql 是其余服务的依赖——必须等其他服务全都停完最后才停，
# 与启动顺序（mysql 排第一）互为镜像。
RESTART_STOP_ORDER = SERVICES_STOP_ORDER + ["mysql"]

# ---------------------------------------------------------------------
# 通用工具
# ---------------------------------------------------------------------


def proc_alive(name):
    """判断某进程名是否存在非僵尸进程。name=进程名（Linux comm，≤15 字符）"""
    _, text = capture(["ps", "-eo", "stat,comm"])
    for line in text.splitlines():
        fields = line.split()
        # 与 bash awk 一致：第 1 列状态不以 Z 开头（排除僵尸）、第 2 列完全等于进程名
        if len(fields) >= 2 and not fields[0].startswith("Z") and fields[1] == name:
            return True
    return False


def proc_pids(name):
    """返回某进程名对应的所有 PID 列表（排除僵尸进程），保持 ps 输出顺序"""
    _, text = capture(["ps", "-eo", "pid,stat,comm"])
    pids = []
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 3 and not fields[1].startswith("Z") and fields[2] == name:
            pids.append(fields[0])
    return pids


def port_listening(port):
    """判断端口是否处于 LISTEN 状态"""
    if shutil.which("ss"):
        _, text = capture(["ss", "-ltnH"])
    else:
        _, text = capture(["netstat", "-ltn"])
    for line in text.splitlines():
        fields = line.split()
        # Local Address:Port 列（两种工具都是第 4 列）以 :端口 结尾即视为监听
        if len(fields) >= 4 and fields[3].endswith(f":{port}"):
            return True
    return False


def pid_on_port(port):
    """返回监听某端口的进程 PID（取第一个）；无则返回空串"""
    _, text = capture(["ss", "-ltnpH"])
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 4 and fields[3].endswith(f":{port}"):
            # ss 输出形如 users:(("app",pid=1234,fd=5))，取第一个 pid=
            m = re.search(r"pid=(\d+)", fields[-1])
            if m:
                return m.group(1)
    return ""


# 服务名 -> 进程名。mysql 为容器/外部实例，无进程名
SVC_PROC_NAME = {
    "gtrade": "gtrade_main",
    "repl": "gtrade_repl",
    "web_server": "gtrade_websrv",
    "web_client": "gtrade_webcli",
    # mcp 匹配的是 SSE 实例（server.py --transport sse）；AI 工具/编辑器拉起的
    # stdio 实例进程名是 gtrade_mcp，不在本表内，故 start/stop/status 都不会碰它。
    # 两者必须区分的根因见 mcp_server/server.py 的 PROC_NAME_* 注释
    "mcp": "gtrade_mcpsse",
}

# 服务名别名归一化，容忍常见写法
NORMALIZE_SERVICE = {
    "gtrade": "gtrade", "gtrade_main": "gtrade", "main": "gtrade",
    "repl": "repl", "gtrade_repl": "repl",
    "web_server": "web_server", "webserver": "web_server", "flask": "web_server",
    "web_client": "web_client", "webclient": "web_client", "vite": "web_client",
    "mcp": "mcp", "mcp_server": "mcp", "mcpserver": "mcp",
    "mysql": "mysql", "db": "mysql", "database": "mysql",
}

# 服务名 -> 端口列表（status 展示用）；mcp 的端口由 MCP_PORT 决定，运行时填充
SVC_PORTS = {
    "gtrade": ["46012", "50051"],
    "repl": [],
    "web_server": ["46011"],
    "web_client": ["46010"],
    "mcp": None,
    "mysql": ["3307"],
}


def service_running(svc):
    """判断服务是否在运行。gtrade/repl/web_*/mcp 按进程名匹配，mysql 按容器或端口判断"""
    if svc == "mysql":
        return mysql_running()
    comm = SVC_PROC_NAME.get(svc, "")
    return bool(comm) and proc_alive(comm)


def wait_service_ready(svc, port=None, timeout=15):
    """等待服务就绪：进程存活，且（若指定端口）端口处于 LISTEN 状态。
    注意必须循环等待：进程出现与端口监听之间通常有数秒间隔（如 Vite 启动）"""
    for _ in range(timeout):
        if service_running(svc):
            if not port or port_listening(port):
                return True
        time.sleep(1)
    return False


def wait_service_stable(svc, port=None, timeout=15, stable=2):
    """等待服务就绪并确认稳定存活。
    wait_service_ready 可能在进程刚出现的瞬间就返回，而进程随即因致命错误退出
    （实测：gtrade 读不到 config/config.yml 约 0.3 秒即崩），故返回前再观察一小段。"""
    if not wait_service_ready(svc, port, timeout):
        return False
    time.sleep(stable)
    return service_running(svc)


def tail_lines(path, n=5):
    """等价 tail -n：从文件末尾按块回退读取，返回最后 n 行，避免全量读入大日志"""
    with open(path, "rb") as f:
        f.seek(0, os.SEEK_END)
        pos = f.tell()
        data = b""
        while pos > 0 and data.count(b"\n") <= n:
            step = min(4096, pos)
            pos -= step
            f.seek(pos)
            data = f.read(step) + data
    return data.decode(errors="replace").splitlines()[-n:]


def show_log_tail(svc):
    """打印服务日志尾部，用于启动失败时提示原因"""
    log = os.path.join(LOG_DIR, f"{svc}.out")
    if os.path.isfile(log):
        print(f"  --- {log} 末尾 5 行 ---")
        try:
            for line in tail_lines(log, 5):
                print("  | " + line)
        except OSError:
            pass


def spawn(log, workdir, cmd):
    """后台拉起命令。log=日志文件 workdir=工作目录 cmd=命令及参数列表。
    等价 bash 版: ( ulimit -c unlimited; cd workdir; setsid nohup cmd >>log 2>&1 </dev/null & )
    setsid 脱离当前会话由 start_new_session=True 实现，避免启动脚本退出时被一并回收。"""
    log_dir = os.path.dirname(log)
    if log_dir:
        try:
            os.makedirs(log_dir, exist_ok=True)
        except OSError:
            pass
    if not os.path.isdir(workdir):
        # bash 版在子进程里 cd 失败报此错后自行退出，等待阶段自然判失败；行为对齐
        print(f"错误: 工作目录不存在 {workdir}", file=sys.stderr)
        return

    def _child_limits():
        # ulimit -c unlimited 2>/dev/null：放开 core dump 限制，设置失败不阻断启动
        try:
            resource.setrlimit(resource.RLIMIT_CORE,
                               (resource.RLIM_INFINITY, resource.RLIM_INFINITY))
        except (OSError, ValueError):
            pass

    # >> 追加语义对应 append 模式；fd 已 dup 给子进程，父进程随即关闭
    log_fd = open(log, "ab")
    subprocess.Popen(cmd, cwd=workdir, stdin=subprocess.DEVNULL,
                     stdout=log_fd, stderr=subprocess.STDOUT,
                     start_new_session=True, preexec_fn=_child_limits)
    log_fd.close()


def stop_by_proc_name(svc, comm, wait_secs=10):
    """按进程名停止服务：先 SIGTERM 优雅退出，超时后 SIGKILL"""
    pids = proc_pids(comm)
    if not pids:
        print(f"  - {svc} 未运行")
        return True
    print(f"  正在停止 {svc} (pid: {' '.join(pids)})...")
    for pid in pids:
        try:
            os.kill(int(pid), 15)  # SIGTERM
        except OSError:
            pass
    for _ in range(wait_secs):
        time.sleep(1)
        if not proc_alive(comm):
            print(f"  ✓ {svc} 已停止")
            return True
    print(f"  {svc} 未响应，强制停止...")
    for pid in pids:
        try:
            os.kill(int(pid), 9)  # SIGKILL
        except OSError:
            pass
    time.sleep(1)
    if proc_alive(comm):
        print(f"  ✗ {svc} 仍存活，请手动检查")
        return False
    print(f"  ✓ {svc} 已强制停止")
    return True


# ---------------------------------------------------------------------
# mysql（docker compose / 外部实例）
# ---------------------------------------------------------------------


def docker_compose_cmd():
    """返回可用的 docker compose 命令列表（v2 插件或 v1 独立命令），无则 None"""
    if shutil.which("docker") and silent(["docker", "compose", "version"]) == 0:
        return ["docker", "compose"]
    dc1 = shutil.which("docker-compose")
    return [dc1] if dc1 else None


def compose_file():
    """返回 compose 文件路径（仅当存在），否则 None"""
    path = os.path.join(PROJECT_ROOT, "docker-compose.app.yml")
    return path if os.path.isfile(path) else None


def mysql_running():
    """mysql 判活：优先 docker 容器状态；无 docker 时按 3307 端口是否监听"""
    if shutil.which("docker"):
        rc, out = capture(["docker", "inspect", "-f", "{{.State.Running}}", "gtrade_mysql"])
        return rc == 0 and out.strip() == "true"
    return port_listening(3307)


def mysql_start():
    dc = docker_compose_cmd()
    compose = compose_file()
    if not dc or not compose:
        print("  ! 本机没有 docker/docker-compose 或缺少 docker-compose.app.yml，跳过 mysql")
        return True
    print("  正在启动 mysql 容器...")
    if silent(dc + ["-f", compose, "up", "-d", "mysql"]) == 0:
        print("  ✓ mysql 容器已启动 (gtrade_mysql:3307)")
        return True
    print(f"  ✗ mysql 容器启动失败，请手动执行: {' '.join(dc)} -f {compose} up -d mysql")
    return False


def mysql_stop():
    dc = docker_compose_cmd()
    compose = compose_file()
    if not dc or not compose:
        print("  - 本机没有 docker，mysql 视为外部实例，跳过停止")
        return True
    print("  正在停止 mysql 容器...")
    if silent(dc + ["-f", compose, "stop", "mysql"]) == 0:
        print("  ✓ mysql 容器已停止")
        return True
    print("  ✗ mysql 容器停止失败")
    return False


# ---------------------------------------------------------------------
# 各服务启动
# ---------------------------------------------------------------------


def list_config_modes():
    """列出 deploy/config/ 下全部配置模式（子目录名，字母序），用于 -c 缺失时的报错提示。"""
    modes_dir = os.path.join(PROJECT_ROOT, "deploy", "config")
    if not os.path.isdir(modes_dir):
        return []
    return sorted(e for e in os.listdir(modes_dir)
                  if os.path.isdir(os.path.join(modes_dir, e)) and not e.startswith("."))


def env_config_mode():
    """从环境变量 GTRADE_MODE 读取配置模式，作为命令行 -c 的回退来源。

    未设置、或只写了空白（GTRADE_MODE="" / GTRADE_MODE="   "）时返回 None——
    空串按「未设置」处理，与 GTRADE_BIN_DIR / GTRADE_LOG_DIR 的 `if env_dir:` 语义一致；
    返回值不做「是否为 deploy/config/ 下已有模式」的校验：非法模式交给 gen_config.py
    在生成配置时报错（命令行 -c 同样不预校验，两条路径行为保持一致）。"""
    mode = os.environ.get(CONFIG_MODE_ENV, "").strip()
    return mode or None


def run_config_gen(mode):
    """子进程调用 deploy/gen_config.py，把模板 + 指定模式覆盖合并生成完整配置到 bin/config/。
    svc.py 自身保持纯标准库（spec 约束），PyYAML 逻辑全在 gen_config.py 内，
    经 pick_python() 选出的解释器执行。返回 (退出码, stdout+stderr 合并文本)。"""
    script = os.path.join(PROJECT_ROOT, "deploy", "gen_config.py")
    if not os.path.isfile(script):
        return 1, f"错误: 找不到配置生成器 {script}（配置生成需要仓库布局，安装布局请回仓库执行）\n"
    try:
        r = subprocess.run([PYTHON, script, "-c", mode, "-o", BIN_DIR],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except OSError as e:
        return 127, f"错误: 无法执行配置生成器 ({PYTHON} {script}): {e}\n"
    return r.returncode, r.stdout.decode(errors="replace")


def start_bin_service(svc):
    """启动需要在构建产物目录内运行的服务（gtrade / repl）"""
    if not BIN_DIR:
        print(f"  ✗ {svc} 启动失败: 未找到可执行文件，请先编译或设置 GTRADE_BIN_DIR",
              file=sys.stderr)
        return False
    bin_path = os.path.join(BIN_DIR, "gtrade" if svc == "gtrade" else "gtrade_repl")
    if not os.access(bin_path, os.X_OK):
        print(f"  ! {bin_path} 不存在，跳过 {svc}")
        return True
    print(f"  正在启动 {svc} ({bin_path})...")
    if svc == "gtrade":
        # 启动前把 config/ 模板与 deploy/config/<mode>/ 覆盖合并生成完整配置到 bin/config/
        # （gtrade 以 bin 目录为工作目录、按相对路径读 config/config.yml，src/gtrade.cpp:97）。
        # 替代原先的 config.yml 存在性预检：生成成功即文件就位；校验/生成失败则中止启动，
        # 错误配置到不了程序（gen_config 一次性列出全部问题：未知键/类型错位/未知文件等）
        rc, gen_output = run_config_gen(CONFIG_MODE)
        sys.stdout.write(gen_output)
        if rc != 0:
            print(f"  ✗ 配置生成失败 (-c {CONFIG_MODE})，gtrade 不启动")
            return False
        spawn(os.path.join(LOG_DIR, "gtrade.out"), BIN_DIR, [bin_path])
        if not wait_service_stable("gtrade", None, 15):
            print("  ✗ gtrade 启动失败（疑似启动即退出，见日志）")
            show_log_tail("gtrade")
            return False
        print("  ✓ gtrade 已启动")
        # 引擎起来后再监听 HttpGateway；端口未就绪通常是配置/数据库问题，只提示不判失败
        if not port_listening(46012):
            print(f"  ! 提示: HttpGateway 46012 尚未监听（见 {LOG_DIR}/gtrade.out）")
        return True
    # repl 依赖主进程初始化好的共享内存 WAL
    spawn(os.path.join(LOG_DIR, "repl.out"), BIN_DIR, [bin_path, "--file-wal", "./wal"])
    if not wait_service_stable("repl", None, 10):
        print("  ✗ repl 启动失败")
        show_log_tail("repl")
        return False
    print("  ✓ repl 已启动")
    return True


def start_web_server():
    dir_ = os.path.join(PROJECT_ROOT, "web_server")
    if not os.path.isdir(dir_):
        print(f"  ✗ 目录不存在: {dir_}")
        return False
    print("  正在启动 web_server (Flask :46011)...")
    spawn(os.path.join(LOG_DIR, "web_server.out"), dir_, [PYTHON, "app.py"])
    if wait_service_ready("web_server", "46011", 20):
        print("  ✓ web_server 已启动 (http://127.0.0.1:46011)")
        return True
    print("  ✗ web_server 启动失败或未监听 46011")
    show_log_tail("web_server")
    return False


def start_web_client():
    dir_ = os.path.join(PROJECT_ROOT, "web_client")
    if not os.path.isdir(dir_):
        print(f"  ✗ 目录不存在: {dir_}")
        return False
    if not shutil.which("node"):
        print("  ✗ 未安装 node，无法启动 web_client")
        return False
    node_modules = os.path.join(dir_, "node_modules")
    if not os.path.isdir(node_modules):
        print(f"  ! 提示: {node_modules} 不存在，先在 web_client 下执行 npm install")
    print("  正在启动 web_client (Vite :46010)...")
    spawn(os.path.join(LOG_DIR, "web_client.out"), dir_, ["node", "start-vite.js"])
    if wait_service_ready("web_client", "46010", 20):
        print("  ✓ web_client 已启动 (http://127.0.0.1:46010)")
        return True
    # vite 配置 strictPort:false，端口被占用时会自动换端口，故端口未就绪不等同于启动失败
    if service_running("web_client"):
        print("  ! web_client 进程已在运行，但 46010 未监听（可能已换端口，见日志）")
        return True
    print("  ✗ web_client 启动失败")
    show_log_tail("web_client")
    return False


def start_mcp():
    dir_ = os.path.join(PROJECT_ROOT, "mcp_server")
    if not os.path.isdir(dir_):
        print(f"  ✗ 目录不存在: {dir_}")
        return False
    print(f"  正在启动 mcp_server (SSE {MCP_HOST}:{MCP_PORT})...")
    spawn(os.path.join(LOG_DIR, "mcp.out"), dir_,
          [PYTHON, "server.py", "--transport", "sse", "--host", MCP_HOST, "--port", MCP_PORT])
    if wait_service_ready("mcp", MCP_PORT, 20):
        print(f"  ✓ mcp_server 已启动 (http://{MCP_HOST}:{MCP_PORT}/sse)")
        return True
    print(f"  ✗ mcp_server 启动失败或未监听 {MCP_PORT}")
    show_log_tail("mcp")
    return False


def start_one(svc):
    """启动单个服务（已运行则跳过）。未知服务返回 False（resolve_targets 已拦，正常到不了这里）"""
    if service_running(svc):
        print(f"  - {svc} 已在运行，跳过")
        return True
    if svc == "mysql":
        return mysql_start()
    if svc in ("gtrade", "repl"):
        return start_bin_service(svc)
    if svc == "web_server":
        return start_web_server()
    if svc == "web_client":
        return start_web_client()
    if svc == "mcp":
        return start_mcp()
    print(f"  ✗ 未知服务: {svc}")
    return False


# ---------------------------------------------------------------------
# 各服务停止
# ---------------------------------------------------------------------


def stop_one(svc):
    """停止单个服务"""
    if svc == "mysql":
        if not mysql_running():
            print("  - mysql 未运行")
            return True
        return mysql_stop()
    return stop_by_proc_name(svc, SVC_PROC_NAME[svc], 10)


# ---------------------------------------------------------------------
# 命令实现
# ---------------------------------------------------------------------


def resolve_targets(order, names):
    """解析服务名参数，返回归一化后的服务名列表；未指定时返回该命令的默认顺序。
    含未知服务名时向 stderr 报错并返回 None（调用方以退出码 2 终止）"""
    if not names:
        return list(order)
    targets = []
    bad = False
    for name in names:
        if name == "all":
            # all 展开为该命令顺序下的全部服务
            targets.extend(order)
            continue
        norm = NORMALIZE_SERVICE.get(name, "")
        if not norm:
            print(f"错误: 未知服务 '{name}'", file=sys.stderr)
            bad = True
            continue
        targets.append(norm)
    return None if bad else targets


def order_by_dependency(targets, order):
    """把目标服务按给定依赖顺序重排：order 中越靠前的服务越先处理。
    不在 order 中的服务排在末尾（当前服务表与两个 order 一一对应，属防御分支）；
    用稳定排序，同秩服务保持调用方给定顺序。"""
    rank = {name: i for i, name in enumerate(order)}
    return sorted(targets, key=lambda svc: rank.get(svc, len(order)))


def cmd_start(svcs):
    """按给定顺序串行启动服务，返回退出码（任一失败即 1，不中断后续服务）。
    gtrade 起来后留 1 秒共享内存 WAL 初始化时间，再启动随后的 repl。"""
    rc = 0
    for svc in svcs:
        print(f"[{svc}]")
        if not start_one(svc):
            rc = 1
        # gtrade 起来后留出共享内存 WAL 初始化时间，再启动 repl
        if svc == "gtrade":
            time.sleep(1)
    return rc


def cmd_stop(svcs):
    """按给定顺序串行停止服务，返回退出码（任一失败即 1，不中断后续服务）"""
    rc = 0
    for svc in svcs:
        print(f"[{svc}]")
        if not stop_one(svc):
            rc = 1
    return rc


def cmd_restart(svcs):
    """重启 = 先按依赖逆序把目标服务全部停掉，全停完再按依赖顺序统一启动。
    不能「逐个先停后起」：那样停 mysql 时 gtrade 等依赖方还在运行（动了被依赖方、
    依赖方却还活着），且两阶段的处理顺序完全被服务名参数顺序决定。
    这里两个阶段各自按依赖顺序重排，服务名参数顺序不影响结果。"""
    print("[restart] 第 1 阶段: 停止全部目标服务（依赖逆序）")
    rc = cmd_stop(order_by_dependency(svcs, RESTART_STOP_ORDER))
    # 全部停完再等 1 秒，让端口/共享内存句柄彻底释放后再整体启动
    time.sleep(1)
    print("[restart] 第 2 阶段: 启动全部目标服务（依赖顺序）")
    if cmd_start(order_by_dependency(svcs, SERVICES_START_ORDER)):
        rc = 1
    return rc


def fmt_elapsed(seconds):
    """把秒数格式化为 3d07:49:01 / 07:49:01 形式（与 ps etime 风格一致）"""
    seconds = int(seconds or 0)
    d, seconds = divmod(seconds, 86400)
    h, seconds = divmod(seconds, 3600)
    m, s = divmod(seconds, 60)
    if d > 0:
        return f"{d}d{h:02d}:{m:02d}:{s:02d}"
    return f"{h:02d}:{m:02d}:{s:02d}"


def docker_mysql_info():
    """docker 容器形态的 mysql 状态：返回 (短容器id 或 "", 启动时刻 epoch 或 0)。
    任一项 docker inspect 失败即返回空——与 bash 版 $(...) 失败得空串再回退 '-' 一致。"""
    _, cid = capture(["docker", "inspect", "-f", "{{slice .Id 0 12}}", "gtrade_mysql"])
    _, started = capture(["docker", "inspect", "-f", "{{.State.StartedAt}}", "gtrade_mysql"])
    started = started.strip()
    if not started:
        return cid.strip(), 0
    # docker 的 StartedAt 是 UTC ISO 串且带纳秒精度（bash 版交给 date -d 解析），
    # fromisoformat 只吃微秒，截断到 6 位小数并补时区
    s = started[:-1] + "+00:00" if started.endswith("Z") else started
    s = re.sub(r"\.(\d{6})\d+", r".\1", s)
    try:
        dt = datetime.fromisoformat(s)
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=timezone.utc)  # docker 恒为 UTC
        return cid.strip(), int(dt.timestamp())
    except ValueError:
        return cid.strip(), 0


def status_line(svc):
    """打印单个服务的状态行。
    除「状态」列外各字段均为 ASCII，且 运行中/未运行 均为 3 个等宽字符，
    因此各列在终端中天然对齐（CJK 按字节填充反而会错位，故不填充）。"""
    ports = SVC_PORTS[svc]
    if ports is None:
        ports = [MCP_PORT]

    pids, uptime = "", ""
    if service_running(svc):
        mark, state = "✓", "运行中"
        if svc == "mysql" and shutil.which("docker"):
            pids, started_epoch = docker_mysql_info()
            if started_epoch:
                uptime = fmt_elapsed(time.time() - started_epoch)
        elif svc == "mysql":
            pids = pid_on_port(3307)
        else:
            plist = proc_pids(SVC_PROC_NAME[svc])
            pids = ",".join(plist)
            if plist:
                _, et = capture(["ps", "-o", "etime=", "-p", plist[0]])
                uptime = et.strip()
        pids = pids or "-"
        uptime = uptime or "-"
    else:
        mark, state = "✗", "未运行"
        pids = uptime = "-"

    # 端口字段用 +/- 前缀标注是否处于 LISTEN 状态（保持纯 ASCII，避免对齐错乱）
    detail = " ".join(("+" if port_listening(p) else "-") + p for p in ports)
    if not detail:
        detail = "-"

    print(f"  {mark} {svc:<12} {state}   {'pid=' + pids:<20} {'up=' + uptime:<13} ports={detail}")


def cmd_status(svcs):
    down = 0
    print(f"项目根:   {PROJECT_ROOT}")
    print(f"构建产物: {BIN_DIR or '未找到（请先编译，或设置 GTRADE_BIN_DIR）'}")
    print("")
    for svc in svcs:
        status_line(svc)
        if not service_running(svc):
            down += 1
    print("")
    print(f"日志目录: {LOG_DIR}/<服务名>.out")
    if down == 0:
        print(f"全部 {len(svcs)} 个服务运行中")
        return 0
    print(f"有 {down}/{len(svcs)} 个服务未运行")
    return 1


def cmd_clean():
    """清理 /dev/shm 下的共享内存（承接原 deploy/clean.sh 的职责）。
    gtrade/repl 运行时共享内存里是活着的 WAL 段，此时清理等于毁掉运行状态，
    因此要求全部非 mysql 服务已停止后才执行；mysql 为容器/外部实例，与宿主机 /dev/shm 无关。"""
    running = [s for s in SERVICES_STOP_ORDER if service_running(s)]
    print("[clean]")
    if running:
        print("  ✗ 以下服务仍在运行，先执行 ./svc.sh stop 再清理:")
        for s in running:
            print(f"    - {s}")
        return False
    # 与原 clean.sh 行为一致: /bin/rm /dev/shm/* -f。Python 版逐个删除普通文件/软链接，
    # 单项失败静默跳过（对应 -f 的容错语义）；目录不递归删除
    for path in glob.glob("/dev/shm/*"):
        try:
            if os.path.isfile(path) or os.path.islink(path):
                os.remove(path)
        except OSError:
            pass
    print("  ✓ /dev/shm 已清理")
    return True


# 与 bash 版 usage() heredoc 逐字一致（保持输出契约不变）
USAGE_TEXT = """GTrade 服务管理脚本

用法:
  ./svc.sh [-c <mode>] <命令> [服务名...]

命令:
  start      启动服务（需配置模式；不带服务名 = 按依赖顺序启动全部）
  stop       停止服务（不带服务名 = 按依赖逆序停止全部）
  restart    先按依赖逆序停掉全部目标服务，再按依赖顺序统一启动（需配置模式）
  status     查看状态（不带服务名 = 查看全部）
  clean      清理 /dev/shm 共享内存（要求所有服务已停止，不接受服务名）

配置模式 (-c):
  deploy/config/ 下的子目录名即模式（如 dev / mock / prod / sit）。
  start/restart 必须指定模式：-c <mode> 或环境变量 GTRADE_MODE 二者取其一，
  同时存在时命令行 -c 优先。指定后先把 config/ 模板与 deploy/config/<mode>/ 覆盖配置
  合并生成完整配置到 bin/config/（deploy/gen_config.py），
  校验失败（未知键/类型错位/未知文件等）即中止启动。

服务名:
  gtrade       交易主进程     gtrade_main    HttpGateway :46012 / gRPC :50051
  repl         WAL 复制进程   gtrade_repl
  web_server   Flask 后端     gtrade_websrv  :46011
  web_client   Vue3 前端      gtrade_webcli  :46010
  mcp          MCP Server     gtrade_mcpsse  SSE :8765
  mysql        MySQL 容器     gtrade_mysql   :3307 (docker compose)

示例:
  ./svc.sh -c dev start             # dev 模式启动全部服务
  ./svc.sh -c mock restart gtrade   # 切本地 mock 交易所并重启引擎
  ./svc.sh -c prod start gtrade repl
  GTRADE_MODE=dev ./svc.sh start    # 模式取自环境变量，可省略 -c
  ./svc.sh -c prod start            # 已有 GTRADE_MODE 时，命令行 -c 覆盖环境变量
  ./svc.sh status                   # 状态查看不需要配置模式
  ./svc.sh stop all                 # 停止全部
  ./svc.sh clean                    # 全停后清理共享内存

环境变量:
  GTRADE_MODE      配置模式（start/restart 用；命令行 -c 优先于此变量，空值视为未设置）
  GTRADE_BIN_DIR   gtrade/gtrade_repl 所在目录（默认: 脚本同级目录或自动探测最新构建产物）
  GTRADE_PYTHON    web_server/mcp/配置生成使用的 python（默认: my_pyenv > python3 > python）
  GTRADE_LOG_DIR   日志目录（默认: <项目根>/log，每个服务一个 <服务名>.out）
  MCP_HOST/MCP_PORT  MCP SSE 绑定地址（默认 127.0.0.1:8765）
  FLASK_ENV        web_server 运行模式（默认 development，绑定 0.0.0.0）

退出码: 0 成功 / 1 有服务失败或未运行 / 2 用法错误
"""


# ---------------------------------------------------------------------
# 入口
# ---------------------------------------------------------------------


def main(argv):
    # 全局选项 -c <mode>：start/restart 必填（启动前生成完整配置）；其余命令接受但忽略。
    # 允许出现在命令前后任意位置（svc.sh 薄委托原样透传）
    args = list(argv)
    config_mode = None
    rest = []
    i = 0
    while i < len(args):
        if args[i] == "-c":
            if i + 1 >= len(args):
                print("错误: -c 需要一个模式参数（模式 = deploy/config/ 下子目录名）", file=sys.stderr)
                sys.exit(2)
            config_mode = args[i + 1]
            i += 2
            continue
        rest.append(args[i])
        i += 1

    cmd = rest[0] if rest else ""
    rest = rest[1:]

    if cmd in ("start", "stop", "restart", "status", "clean"):
        pass
    elif cmd in ("", "-h", "--help", "help"):
        print(USAGE_TEXT, end="")
        sys.exit(2 if cmd == "" else 0)
    else:
        print(f"错误: 未知命令 '{cmd}'", file=sys.stderr)
        print("", file=sys.stderr)
        print(USAGE_TEXT, end="", file=sys.stderr)
        sys.exit(2)

    # clean 是全局动作，不涉及具体服务，不需要目标解析
    if cmd == "clean":
        if rest:
            print("错误: clean 清理的是整个 /dev/shm，不接受服务名", file=sys.stderr)
            sys.exit(2)
        sys.exit(0 if cmd_clean() else 1)

    # -c 只对 start/restart 生效：取值优先级 命令行 -c > 环境变量 GTRADE_MODE
    # （命令行走在显式位置，用户当次意图优先于环境里的默认值）；
    # 两者都没有才算缺失（报错时列出可用模式）；其余命令忽略 -c
    global CONFIG_MODE
    if cmd in ("start", "restart"):
        mode = config_mode if config_mode is not None else env_config_mode()
        if mode is None:
            modes = " ".join(list_config_modes()) or "(无)"
            print(f"错误: {cmd} 需要 -c <mode> 或环境变量 {CONFIG_MODE_ENV} 指定配置模式"
                  f"（deploy/config/ 下可用: {modes}）", file=sys.stderr)
            sys.exit(2)
        if config_mode is None:
            # 模式来自环境变量时明确提示落到了哪个值，避免「没传 -c 却生效」的困惑
            print(f"提示: 未指定 -c，使用环境变量 {CONFIG_MODE_ENV}={mode}")
        CONFIG_MODE = mode
    elif config_mode is not None:
        print(f"提示: {cmd} 不需要 -c，已忽略 '{config_mode}'")

    # 未指定服务名时使用该命令的默认顺序
    order = SERVICES_STOP_ORDER if cmd == "stop" else SERVICES_START_ORDER
    targets = resolve_targets(order, rest)
    if targets is None:
        sys.exit(2)
    if not targets:
        print("错误: 没有匹配到任何服务", file=sys.stderr)
        sys.exit(2)

    if cmd == "start":
        sys.exit(cmd_start(targets))
    if cmd == "stop":
        sys.exit(cmd_stop(targets))
    if cmd == "restart":
        sys.exit(cmd_restart(targets))
    if cmd == "status":
        sys.exit(cmd_status(targets))


if __name__ == "__main__":
    main(sys.argv[1:])
