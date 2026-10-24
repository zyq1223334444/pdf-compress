// -*- coding: utf-8 -*-
//
// 非 Windows 平台的资源管理器右键菜单。
//
// Linux 没有统一的"注册右键菜单"机制，每个文件管理器各有一套约定，所以这里给
// 常见的几个各写一份。全部落在用户目录下，不需要 root：
//
//     Nautilus / Caja   ~/.local/share/nautilus/scripts/      "脚本"子菜单
//     Nemo              ~/.local/share/nemo/actions/         直接在右键菜单里
//     Dolphin           ~/.local/share/kio/servicemenus/     "动作"子菜单
//
// Thunar 故意不支持：它的自定义动作只能整份替换 uca.xml，覆盖用户已有的动作
// 不可接受，而这个文件不是我们的。
//
// macOS 的入口是"快速操作"（Quick Action），本质是 ~/Library/Services 下的一个
// .workflow 包：Info.plist 声明它只对 PDF 出现，document.wflow 里挂一个 Run
// Shell Script 动作调命令行版。macOS 上没有别的免签名做法——真正的 Finder 扩展
// 要 Xcode 工程和代码签名。
//
// 两边真正干活的是同一段 shell：目标大小从 settings.ini 读（与图形界面共用同一
// 份设置），读不到用 8 MB；日志写进缓存目录，有 zenity / kdialog / notify-send
// 时再弹一条结果。

#pragma once

namespace pdfc {

/// Linux 与 macOS 的 `--install-menu`。quiet 为真时不打印过程。
int install_shell_menu_posix(bool quiet);

/// 去掉上面装的所有东西。重复执行是安全的。
int uninstall_shell_menu_posix(bool quiet);

}  // namespace pdfc
