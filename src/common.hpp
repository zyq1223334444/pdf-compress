// -*- coding: utf-8 -*-
//
// 公共类型与小工具：版本号、退出码、单位换算、UTF-8 与宽字符互转。
//
// 这个项目的所有字符串一律用 UTF-8 窄字符（MSVC 上靠 /utf-8 保证），
// 只在调用 Windows API 的那一刻转成宽字符——Windows 的 API 只有 W 版本
// 才真正按 Unicode 处理路径，A 版本会受系统代码页限制。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pdfc {

// ── 版本与标识 ────────────────────────────────────────────────
// 与 CMakeLists.txt 的 project(... VERSION) 保持一致；构建时也会以
// PDFC_VERSION 宏注入，这里只作为没有注入时的兜底。
#ifndef PDFC_VERSION
#define PDFC_VERSION "2.1.0"
#endif

inline constexpr const char* kVersion = PDFC_VERSION;
inline constexpr const char* kAppName = "PDF Compressor";
inline constexpr const char* kAppId = "pdf-compress";
inline constexpr const char* kPublisher = "zyq1223334444";
inline constexpr const char* kAppUrl = "https://github.com/zyq1223334444/pdf-compress";
inline constexpr const char* kExeName = "pdf_compress.exe";

// ── 退出码 ────────────────────────────────────────────────────
// 130 是"被用户中断"的约定值，命令行与图形界面共用同一套语义。
inline constexpr int kExitOk = 0;
inline constexpr int kExitFail = 1;
inline constexpr int kExitInterrupt = 130;

// ── 单位换算 ──────────────────────────────────────────────────
inline constexpr double kDpiBase = 72.0;      // PDF 用户空间：72pt = 1 英寸
inline constexpr double kMebibyte = 1024.0 * 1024.0;

/// 把字节数格式化成便于阅读的字符串（与 Python 版逐字一致）。
std::string human_size(double bytes);

/// 字节数换算成 MB（以 1024 为进制，和命令行参数的语义一致）。
double to_mb(double bytes);

/// 目标大小换算成字节。
std::int64_t mb_to_bytes(double mb);

double dpi_to_scale(double dpi);
double scale_to_dpi(double scale);

// ── 字符串工具 ────────────────────────────────────────────────

/// 用 {name} 形式的占位符做替换。
///
/// 与 Python 的 str.format 不同，这里不做类型格式化：调用方先把自己那部分
/// 数字格式化成字符串（human_size / fmt_dpi 等），占位符只负责按名字填入。
/// 好处是中英文表可以共用同一份占位符名字，格式说明符（{:+.1%%}）被忽略，
/// 于是两份文案不会因为精度写法不同而对不上。
std::string format(std::string text,
                   const std::vector<std::pair<std::string, std::string>>& values);

/// 数字的常见格式化（等价于 Python 的 :.0f / :.2f / :g / :+.1%%）。
std::string fmt_fixed(double value, int decimals);
std::string fmt_percent(double ratio, int decimals);      // 0.0123 -> "+1.2%"
std::string fmt_general(double value);                    // 去掉多余的小数零

std::string to_lower(std::string text);
std::string trim(const std::string& text);
std::vector<std::string> split(const std::string& text, char separator);
bool starts_with(const std::string& text, const std::string& prefix);

// ── 平台相关（实现分别在 platform_win.cpp / platform_posix.cpp）──

/// 把 UTF-8 转成 Windows 宽字符（非 Windows 上直接返回输入）。
std::wstring utf8_to_wide(const std::string& text);
std::string wide_to_utf8(const std::wstring& text);

/// 本进程可执行文件所在目录（安装目录）。
std::string executable_dir();

/// 用户级配置目录（%LOCALAPPDATA%\pdf-compress 或 ~/.config/pdf-compress）。
std::string user_config_dir();

/// 建立目录（含中间层级），已存在也算成功。
bool make_directories(const std::string& path);

/// 路径拼接与取文件名/目录名/主名（跨平台，Windows 与 POSIX 分隔符都认）。
std::string path_join(const std::string& a, const std::string& b);
std::string path_basename(const std::string& path);
std::string path_dirname(const std::string& path);
std::string path_stem(const std::string& path);

/// 文件/目录是否存在。
bool file_exists(const std::string& path);
bool is_regular_file(const std::string& path);
bool is_directory(const std::string& path);

/// 文件大小；取不到返回 0。
std::int64_t file_size(const std::string& path);

/// 物理核心数（超线程不重复计数）。渲染是纯 CPU 任务，逻辑核数反而更慢。
int physical_cpu_count();

/// 高精度秒表，用于 --dry-run 之外的耗时统计。
double monotonic_seconds();

/**
 * 保证标准流可用。
 *
 * Windows 上可执行文件按 GUI 子系统编译（双击不弹控制台黑框），所以进程默认
 * 没有控制台。这个函数在"标准句柄无效、但父进程有控制台"时附加到父控制台并
 * 把 stdout/stderr/stdin 接过去；句柄本来就有效（被终端、管道或文件占着）时
 * 什么都不做，重定向必须原样保留。
 */
void setup_console();
void attach_parent_console();

/// 图形界面启动时把占着的控制台摘掉（从终端启动时才有意义）。
void detach_console();

/// 声明进程 DPI 感知，必须在创建窗口之前调用。
void enable_dpi_awareness();

/// 写一行到标准输出（带刷新，子进程/重定向时也能实时看到）。
void emit(const std::string& line);
void emit_line();

/// 路径是否指向一个 PDF 文件（只按扩展名判断，不打开文件）。
bool looks_like_pdf(const std::string& path);

}  // namespace pdfc
