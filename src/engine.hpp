// -*- coding: utf-8 -*-
//
// 压缩流程编排：无损优化 → 判断是否已经达标 → 渲染搜索 → 收尾。
//
// 这一层不认识界面：所有要显示的文字靠自己拼（文案走 i18n），所有要问用户的
// 事情通过 UiCallbacks 回调出去。命令行和图形界面因此共用同一条路径，
// 不会出现两边行为不一致。
//
// 多进程渲染的取舍：分块数不超过进程数（见 engine.cpp 的 split_chunks），所以
// 一次把所有块并发启起来、等全部结束，与"完成一块再补一块"的结果完全一样，
// 但实现简单得多，取消时也只是把这一批都终止掉。

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "options.hpp"
#include "search.hpp"

namespace pdfc {

/// 界面回调。命令行实现见 cli.cpp，图形界面实现在 gui/。
struct UiCallbacks {
    /// 输出一行消息（终端 / 日志窗）
    std::function<void(const std::string&)> emit;

    /// 渲染进度：已完成页数 / 总页数 / 已用秒数
    std::function<void(int done, int total, double elapsed)> progress;

    /// 用户是否要求中断
    std::function<bool()> cancelled;

    /// 能不能提问。为假时下面三个回调不会被调用，一律按默认值走。
    bool interactive = false;

    /// 无损结果已经小于目标，要不要照样有损渲染把文件做大（默认否）
    std::function<bool(std::int64_t size, std::int64_t target)> ask_force_grow;

    /// 手动模式：直接接受无损结果吗（默认是）
    std::function<bool(std::int64_t size, std::int64_t target)> ask_accept_lossless;

    /// 手动模式：下一步怎么做，返回 "y" / "larger" / "smaller" / "q"
    std::function<std::string(const Trial& trial, std::optional<double> hint_dpi)> ask_manual;

    // 便捷包装：回调没设时不会崩，直接走默认行为
    void say(const std::string& line) const { if (emit) emit(line); }
    void report_progress(int done, int total, double elapsed) const {
        if (progress) progress(done, total, elapsed);
    }
    bool stopped() const { return cancelled && cancelled(); }
    bool can_ask() const { return interactive && ask_force_grow != nullptr; }
};

/// 命令行用的实现：打印到标准输出，提问读标准输入。
UiCallbacks console_ui();

/// 执行一次压缩，返回退出码（0 / 1 / 130）。
int run_job(const JobOptions& options, UiCallbacks& ui);

}  // namespace pdfc
