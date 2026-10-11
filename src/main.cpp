// -*- coding: utf-8 -*-
//
// 程序入口。
//
// Windows 上可执行文件按 GUI 子系统链接（双击不弹控制台黑框），入口因此是
// wWinMain 而不是 main：命令行要从 GetCommandLineW 取、用 CommandLineToArgvW
// 拆开，再转成 UTF-8 交给 pdfc::run。POSIX 那边就是普通的 main。
//
// 子进程（--worker）不接管父控制台：它不打印任何东西，附加上去只会平白多占
// 一个控制台引用。

#include "cli.hpp"
#include "common.hpp"

#include <string>
#include <vector>

#if defined(_WIN32)
// windows.h 必须排在 shellapi.h 前面：后者用到前者的类型，单独包含会报一堆
// "缺少类型说明符" 的错。
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <clocale>
#endif

namespace {

bool has_worker_flag(const std::vector<std::string>& args) {
    for (const std::string& arg : args) {
        if (arg == "--worker") return true;
    }
    return false;
}

}  // namespace

#if defined(_WIN32)

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc > 0 ? argc : 0));
    if (wide != nullptr) {
        for (int index = 0; index < argc; ++index) {
            args.push_back(pdfc::wide_to_utf8(wide[index]));
        }
        LocalFree(wide);
    }
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (std::string& arg : args) argv.push_back(arg.data());

    if (!has_worker_flag(args)) pdfc::setup_console();
    return pdfc::run(static_cast<int>(argv.size()), argv.data());
}

#else

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int index = 0; index < argc; ++index) args.emplace_back(argv[index]);
    if (!has_worker_flag(args)) pdfc::setup_console();
    // 让本地化相关的库函数也按 UTF-8 走（LC_ALL 会影响 tolower 之类的行为）
    std::setlocale(LC_ALL, "");
    return pdfc::run(argc, argv);
}

#endif
