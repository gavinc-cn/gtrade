"""
Phase 4 - Level 2 代码生成工具（有副作用：写源码、修改 CMakeLists.txt、触发编译）

工具列表：
  - read_strategy_reference   读取现有策略源码作为参考
  - write_strategy_code       将 AI 生成的策略代码写入文件系统
  - build_strategy_plugin     编译单个策略插件 .so

注意：
  - 编译后的 .so 通过 create_strategy(so_path=...) 或 run_backtest(so_path=...) 使用
  - 新策略类型实盘部署前必须先通过回测
"""

import json
import logging
import re
import subprocess
import time
from pathlib import Path

import yaml
from mcp.server.fastmcp import FastMCP

from config import (
    BUILD_DIR,
    BUILD_TIMEOUT,
    CMAKELISTS_PATH,
    GTRADE_ROOT,
    STRATEGY_PARAM_DIR,
    STRATEGY_SRC_DIR,
    SENSITIVE_PATH_PATTERNS,
)

logger = logging.getLogger(__name__)

# 允许读取的源码目录白名单
_ALLOWED_READ_DIRS = [
    GTRADE_ROOT / "src/strategy",
    GTRADE_ROOT / "src/strategy_engine",
]


def _safe_path(path_str: str) -> Path | None:
    """
    检查路径是否在允许的目录内，防止路径穿越。
    包含敏感关键字的路径返回 None。
    """
    for pattern in SENSITIVE_PATH_PATTERNS:
        if pattern.lower() in path_str.lower():
            return None
    p = Path(path_str)
    if not p.is_absolute():
        # 相对路径以 GTRADE_ROOT 为基准
        p = GTRADE_ROOT / p
    p = p.resolve()
    for allowed in _ALLOWED_READ_DIRS:
        if p.is_relative_to(allowed.resolve()):
            return p
    return None


def register_codegen_tools(mcp: FastMCP) -> None:
    """将所有代码生成工具注册到 MCP server 实例。"""

    # ── read_strategy_reference ──────────────────────────────────────────────────

    @mcp.tool()
    def read_strategy_reference(filename: str) -> str:
        """
        读取现有策略源文件内容作为参考，帮助 AI 了解代码风格和接口规范。
        文件限制在 src/strategy/ 和 src/strategy_engine/ 目录内。

        推荐读取顺序（首次使用时）：
          1. "strategy_engine/strategy_base.h"    — 策略基类接口
          2. "strategy_engine/strategy_plugin.h"  — .so 工厂接口（必读！）
          3. "strategy/strategy_sma.h"             — 简单策略示例
          4. "strategy/strategy_sma.cpp"           — 简单策略实现
          5. "strategy/strategy_spot_grid.h"       — 复杂策略参考

        Args:
            filename: 相对于 src/ 的文件路径，如 "strategy/strategy_sma.h"。
        """
        file_path = _safe_path(f"src/{filename}")
        if file_path is None:
            return json.dumps(
                {"error": f"路径 {filename!r} 不在允许的目录内或包含敏感关键字"},
                ensure_ascii=False,
            )
        if not file_path.exists():
            return json.dumps(
                {"error": f"文件不存在: src/{filename}"},
                ensure_ascii=False,
            )

        content = file_path.read_text(encoding="utf-8", errors="replace")
        return json.dumps(
            {"filename": str(file_path.relative_to(GTRADE_ROOT)), "content": content},
            ensure_ascii=False,
        )

    # ── write_strategy_code ──────────────────────────────────────────────────────

    @mcp.tool()
    def write_strategy_code(
        strategy_name: str,
        type_id: str,
        header_code: str,
        impl_code: str,
        param_schema: list[dict],
        indicator_schema: list[dict] | None = None,
    ) -> str:
        """
        将 AI 生成的策略代码写入文件系统，并自动追加 CMakeLists.txt 插件目标。

        写入的文件：
          - src/strategy/strategy_{strategy_name}.h
          - src/strategy/strategy_{strategy_name}.cpp
          - src/strategy_param/{type_id}.yml

        CMakeLists.txt 末尾追加：
          add_strategy_plugin(strategy_{strategy_name}_live ...)
          add_strategy_plugin(strategy_{strategy_name}_bt   ...)

        重要约束（AI 生成代码必须遵守）：
          1. .cpp 文件必须实现两个 C 工厂函数（见 strategy_plugin.h）：
             extern "C" {
                 StrategyBase* gtrade_create_strategy(
                     const GTradeConfig& cfg, MyHandler* engine, const std::string& id);
                 int gtrade_get_build_mode();  // 回测版返回 1，实盘版返回 0
             }
          2. 不要 #include <dlfcn.h>，不要链接 dl 库
          3. 通过 GTRADE_IN_BACKTEST_MODE 宏区分回测/实盘

        Args:
            strategy_name:    策略名（snake_case），如 "my_momentum"。
                              会生成文件 strategy_my_momentum.h/.cpp。
            type_id:          策略类型 ID（CamelCase），如 "MyMomentum"。
                              用于 YAML strat_template_id 和参数文件名。
            header_code:      完整的 .h 文件内容。
            impl_code:        完整的 .cpp 文件内容（含工厂函数）。
            param_schema:     参数定义列表，每项格式: {"id": "xxx", "name": "说明"}。
                              示例: [{"id": "instrument", "name": "交易对"}, ...]
            indicator_schema: （可选）指标定义列表，格式同 param_schema。
        """
        header_path = STRATEGY_SRC_DIR / f"strategy_{strategy_name}.h"
        impl_path   = STRATEGY_SRC_DIR / f"strategy_{strategy_name}.cpp"
        param_path  = STRATEGY_PARAM_DIR / f"{type_id}.yml"

        # 校验工厂函数是否存在
        if "gtrade_create_strategy" not in impl_code:
            return json.dumps(
                {
                    "success": False,
                    "error": "impl_code 必须包含 gtrade_create_strategy 工厂函数（见 strategy_plugin.h）",
                },
                ensure_ascii=False,
            )
        if "gtrade_get_build_mode" not in impl_code:
            return json.dumps(
                {
                    "success": False,
                    "error": "impl_code 必须包含 gtrade_get_build_mode 函数",
                },
                ensure_ascii=False,
            )

        try:
            # 写入头文件和实现文件
            header_path.write_text(header_code, encoding="utf-8")
            impl_path.write_text(impl_code, encoding="utf-8")
            logger.info(f"已写入策略源码: {header_path}, {impl_path}")

            # 写入参数定义 YAML
            schema_data: dict = {"param": param_schema}
            if indicator_schema:
                schema_data["indicator"] = indicator_schema
            param_path.write_text(
                yaml.dump(schema_data, allow_unicode=True, default_flow_style=False),
                encoding="utf-8",
            )
            logger.info(f"已写入参数定义: {param_path}")

            # 追加 CMakeLists.txt 插件目标
            cmake_snippet = (
                f"\n# MCP-generated: {strategy_name} ({type_id})\n"
                f"add_strategy_plugin(strategy_{strategy_name}_live "
                f"${{SRC_DIR}}/strategy/strategy_{strategy_name}.cpp 0)\n"
                f"add_strategy_plugin(strategy_{strategy_name}_bt "
                f"${{SRC_DIR}}/strategy/strategy_{strategy_name}.cpp 1)\n"
            )
            with open(CMAKELISTS_PATH, "a", encoding="utf-8") as f:
                f.write(cmake_snippet)
            logger.info(f"已追加 CMakeLists.txt 插件目标: strategy_{strategy_name}_*")

            return json.dumps(
                {
                    "success": True,
                    "files_written": [
                        str(header_path.relative_to(GTRADE_ROOT)),
                        str(impl_path.relative_to(GTRADE_ROOT)),
                        str(param_path.relative_to(GTRADE_ROOT)),
                    ],
                    "cmake_targets": [
                        f"strategy_{strategy_name}_live",
                        f"strategy_{strategy_name}_bt",
                    ],
                    "next_step": (
                        f"调用 build_strategy_plugin('{strategy_name}', 'backtest') "
                        f"编译回测版 .so，然后用 run_backtest(so_path=...) 验证。"
                    ),
                },
                ensure_ascii=False,
                indent=2,
            )

        except Exception as e:
            logger.error(f"写入策略代码失败: {e}")
            return json.dumps({"success": False, "error": str(e)}, ensure_ascii=False)

    # ── build_strategy_plugin ────────────────────────────────────────────────────

    @mcp.tool()
    def build_strategy_plugin(
        strategy_name: str,
        mode: str = "backtest",
    ) -> str:
        """
        编译单个策略插件 .so（比全量重编快约 10 倍）。
        编译前请确保已调用 write_strategy_code() 写入源码。

        编译成功后，.so 输出路径：
          bin/strategy_plugins/libstrategy_{strategy_name}_{mode}.so
          （实盘: _live, 回测: _bt）

        若编译失败，根据 errors 字段修正代码后重试（最多 5 次）。

        Args:
            strategy_name: 策略名，如 "my_momentum"。
            mode: "backtest"（默认，编译 _bt 目标）| "live"（编译 _live 目标）| "both"。
        """
        if not BUILD_DIR.exists():
            return json.dumps(
                {
                    "success": False,
                    "error": f"构建目录不存在: {BUILD_DIR}\n请先运行 cmake 配置。",
                },
                ensure_ascii=False,
            )

        # 确定要构建的 CMake 目标
        if mode == "backtest":
            targets = [f"strategy_{strategy_name}_bt"]
        elif mode == "live":
            targets = [f"strategy_{strategy_name}_live"]
        elif mode == "both":
            targets = [
                f"strategy_{strategy_name}_bt",
                f"strategy_{strategy_name}_live",
            ]
        else:
            return json.dumps(
                {"success": False, "error": f"mode 必须是 'backtest'、'live' 或 'both'，收到: {mode!r}"},
                ensure_ascii=False,
            )

        results = []
        overall_success = True

        for target in targets:
            t0 = time.time()
            cmd = [
                "cmake", "--build", str(BUILD_DIR),
                "--target", target,
                "--", "-j4",
            ]
            logger.info(f"编译目标: {target}，命令: {' '.join(cmd)}")

            try:
                proc = subprocess.run(
                    cmd,
                    capture_output=True,
                    text=True,
                    timeout=BUILD_TIMEOUT,
                )
            except subprocess.TimeoutExpired:
                results.append({
                    "target": target,
                    "success": False,
                    "errors": [f"编译超时（>{BUILD_TIMEOUT}s）"],
                    "build_time_seconds": BUILD_TIMEOUT,
                })
                overall_success = False
                continue

            elapsed = round(time.time() - t0, 1)
            stdout = proc.stdout or ""
            stderr = proc.stderr or ""
            combined = stdout + "\n" + stderr

            # 解析编译错误和警告
            errors = _parse_build_errors(combined)
            warnings = _parse_build_warnings(combined)

            success = proc.returncode == 0
            if not success:
                overall_success = False

            # 计算输出 .so 路径
            suffix = "_bt" if target.endswith("_bt") else "_live"
            so_path = (
                BUILD_DIR / "bin" / "strategy_plugins"
                / f"libstrategy_{strategy_name}{suffix}.so"
            )

            results.append({
                "target": target,
                "success": success,
                "exit_code": proc.returncode,
                "so_path": str(so_path.relative_to(GTRADE_ROOT)) if success else None,
                "so_abs_path": str(so_path) if success else None,
                "errors": errors,
                "warnings": warnings[:10],  # 最多返回 10 条警告
                "build_time_seconds": elapsed,
            })

        return json.dumps(
            {
                "success": overall_success,
                "mode": mode,
                "strategy_name": strategy_name,
                "results": results,
            },
            ensure_ascii=False,
            indent=2,
        )


# ── 辅助函数 ─────────────────────────────────────────────────────────────────────

def _parse_build_errors(output: str) -> list[str]:
    """从 cmake/make 输出中提取 error 行。"""
    errors = []
    for line in output.splitlines():
        lower = line.lower()
        if ": error:" in lower or "error:" in lower:
            errors.append(line.strip())
    return errors


def _parse_build_warnings(output: str) -> list[str]:
    """从 cmake/make 输出中提取 warning 行。"""
    warnings = []
    for line in output.splitlines():
        if ": warning:" in line.lower():
            warnings.append(line.strip())
    return warnings
