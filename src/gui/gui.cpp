// -*- coding: utf-8 -*-
//
// 图形界面的入口：把"打开窗口"这件事收敛成两个函数。
//
// gui.hpp 只声明了 run_gui / gui_available，真正的实现在 window.cpp 的 App 里。
// 这一层薄得几乎不像一层，但它有存在的必要：
//
//   * 非 Windows 平台用 gui_stub.cpp 提供同名的两个函数（打印一句"这个构建没有
//     图形界面"），调用方（main.cpp / cli.cpp）因此完全不用写 #ifdef。
//   * 命令行参数已经在这一层之前被解析掉了；run_gui 只接受"要预先进列表的
//     PDF 路径"，界面自己不解析任何命令行。
//
// 启动前的两件杂事也放在这里：把从终端启动时占着的控制台摘掉（GUI 子系统下
// 双击启动不会有控制台，但从 cmd 里启动时会有一个），以及声明 DPI 感知
// （必须在建任何窗口之前，见 common.hpp 的说明）。

#include "gui/gui.hpp"

#if defined(_WIN32)

#include <windows.h>

#include <string>
#include <vector>

#include "common.hpp"
#include "gui/window.hpp"

namespace pdfc {

bool gui_available() { return true; }

int run_gui(const std::vector<std::string>& files) {
    // 从终端启动时把控制台摘掉：图形界面不需要它，留着既不美观，
    // 还会让"命令行走掉、界面留下来"的进程关系变得混乱。
    detach_console();
    // 关掉系统的 DPI 虚拟化，否则高 DPI 下整个界面会被拉伸糊掉
    enable_dpi_awareness();

    gui::App app;
    return app.Run(files);
}

}  // namespace pdfc

#else

namespace pdfc {

bool gui_available() { return false; }

int run_gui(const std::vector<std::string>&) { return 1; }

}  // namespace pdfc

#endif  // _WIN32
