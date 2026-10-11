// -*- coding: utf-8 -*-
//
// 非 Windows 平台的图形界面占位实现。
//
// 图形界面（Win32 + Direct2D 手绘）只有 Windows 有。CMakeLists.txt 里
// WIN32 编 src/gui/gui.cpp，其它平台编这一个，所以两边都要提供 gui.hpp 里的
// 两个符号。
//
// 这里故意不做任何"降级实现"（比如弹个 Tk 窗口）：Linux/macOS 的发行形态就是
// 纯命令行程序，装模作样地开一个半成品界面只会让人以为程序坏了。打印一句
// 说明、返回失败码，让调用方（cli.cpp）自己决定接下来怎么办。

#include <cstdio>

// src/ 在 include 路径上（CMakeLists.txt 的 target_include_directories），
// 所以两种写法都能解析到 gui.hpp；这里直接用同目录的裸名字，换编译器也不用担心。
#include "gui.hpp"

#include "common.hpp"
#include "i18n.hpp"

namespace pdfc {

int run_gui(const std::vector<std::string>& files) {
    // files 用不上：没有界面可以往里放文件列表。参数仍然保留名字，
    // 免得 -Wunused-parameter（CMake 里开了 -Wall -Wextra）报警。
    static_cast<void>(files);

    // 文案键与 Windows 版 gui.cpp 打开失败时用的那条一致，英文表里已经有对应
    // 词条（tools/make_strings_en.py 从 Python 版的 lang_en.py 生成）。
    std::fputs(tr("[失败] 这个构建没有图形界面，请使用命令行：pdf_compress <输入PDF> <目标大小MB>"),
               stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
    // 明确走 stderr 之外的一条路径：这行提示本身就是给用户看的正常输出，
    // 但退出码必须是失败——调用方不该以为界面"打开过了"。
    return kExitFail;
}

bool gui_available() { return false; }

}  // namespace pdfc
