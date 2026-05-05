//
// Created by Claude on 2026/01/19.
//

#pragma once
#include <string>

/**
 * @brief 进程工具类 - 跨平台设置进程名称
 *
 * 主要用于 Linux 平台，其他平台提供基本支持或优雅降级
 */
/**
 * @brief 设置进程名称（仅修改内核进程名）
 *
 * @param name 进程名称，建议不超过 15 字符（Linux prctl 限制）
 * @return true 设置成功
 * @return false 设置失败或当前平台不支持
 *
 * @note Linux: 使用 prctl(PR_SET_NAME)，最多 15 个可见字符
 * @note 其他平台: 优雅降级，返回 false
 */
bool SetProcessName(const std::string& name);

/**
 * @brief 设置进程名称（修改内核进程名 + argv[0]）
 *
 * @param name 进程名称，建议不超过 15 字符
 * @param argc 从 main() 函数传入的 argc 值
 * @param argv 从 main() 函数传入的 argv 指针
 * @return true 设置成功
 * @return false 设置失败或当前平台不支持
 *
 * @note 这个版本会同时修改：
 *       1. 内核进程名 (prctl) - 影响 ps aux, ps -o comm, /proc/$pid/comm
 *       2. argv[0] - 影响 ps -ef 的 CMD 列
 * @note 修改 argv[0] 后，命令行参数将被清空（安全做法）
 */
bool SetParamShow(const std::string& name, int argc, char** argv);

std::string GetProcessName();
