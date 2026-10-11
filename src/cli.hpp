// -*- coding: utf-8 -*-
//
// 命令行入口：参数解析、帮助文本、启动方式分派。
//
// 启动方式（与 Python 版一致，脚本和文档都靠这套约定）：
//     没有参数                    -> 图形界面
//     参数全是存在的文件路径       -> 图形界面，并把这些文件放进列表
//     给了目标大小 / --dpi / 选项  -> 命令行压缩
//     -h / --help / --help-en / --help-cn -> 帮助
//     --install-menu / --uninstall-menu   -> 安装或卸载资源管理器右键菜单
//
// 帮助语言取自"当前语言"（设置文件 → 安装包写入的注册表 → 系统区域）；
// -h/--help 跟随它，--help-en 与 --help-cn 强制指定。

#pragma once

#include <string>
#include <vector>

#include "i18n.hpp"      // Language
#include "options.hpp"   // JobOptions

namespace pdfc {

/// 中文帮助文本（源语言）。
const char* help_zh();
/// 英文帮助文本。
const char* help_en();

/// 程序入口，返回进程退出码。
int run(int argc, char** argv);

/// 参数解析的结果。
struct ParsedArgs {
    bool show_help = false;
    bool help_force_en = false;
    bool help_force_zh = false;
    bool show_version = false;
    bool install_menu = false;
    bool uninstall_menu = false;
    bool quiet = false;
    bool gui = false;
    bool has_language_flag = false;
    Language language_flag = Language::Zh;
    std::vector<std::string> files;      // 非选项参数（输入文件）
    JobOptions options;
    std::string error;                   // 非空表示参数有错
};

/// 解析命令行。argc/argv 与 main 的一致（argv[0] 是程序名）。
ParsedArgs parse_args(int argc, char** argv);

}  // namespace pdfc
