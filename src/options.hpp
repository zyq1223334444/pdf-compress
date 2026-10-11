// -*- coding: utf-8 -*-
//
// 一次压缩任务的全部参数。字段名与命令行选项一一对应，图形界面构造同一个
// 结构体，于是界面和命令行走的是同一条执行路径。

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pdfc {

enum class Mode { Auto, Manual };
enum class Strategy { Closest, Smaller };

struct JobOptions {
    std::string input;

    // 目标大小（MB）。与 dpi 至少给一个：给了 dpi 就按固定分辨率渲染一次。
    std::optional<double> target_size;
    std::optional<double> dpi;

    std::string output_dir;                 // 空 = 与输入同目录
    Mode mode = Mode::Auto;
    Strategy strategy = Strategy::Closest;
    int max_retries = 8;                    // 自动模式最多渲染几次
    double max_dpi = 600.0;                 // 搜索上界
    double min_dpi = 7.2;                   // 搜索下界
    int jobs = 0;                           // 0 = 物理核心数
    bool force_render = false;              // 无损已达标也强行渲染做大
    bool keep_page_box = false;             // 保留原始 MediaBox
    bool dry_run = false;                   // 只显示计划
};

/// 默认值集中放这里，命令行与图形界面共享。
inline constexpr int kDefaultRetries = 8;
inline constexpr double kDefaultMaxDpi = 600.0;
inline constexpr double kDefaultMinDpi = 7.2;

/// 把 --jobs 解析成实际进程数（0 = 自动，按物理核心数）。
int resolve_workers(int jobs);

/// 字段校验：返回空串表示合法，否则是给用户看的错误说明。
std::string validate_options(const JobOptions& options);

}  // namespace pdfc
