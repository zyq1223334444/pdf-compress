// -*- coding: utf-8 -*-
//
// Windows 资源编号。
//
// 只用到一个图标：主程序 exe 自己的图标（也是右键菜单项左边那个小图标，
// 因为菜单的 Icon 值写的是 "<exe>,0"，正好取这里的第 0 号图标）。
//
// ExplorerCommand.dll 里那个图标资源是 shell\build_shell_menu.bat 单独编的
// （它的编号是 101，写在生成的 .rc 里），不走这个头文件，两者互不影响。

#pragma once

// 主程序图标，来自 assets\pdf_compress.ico（由 assets\make_icon.py 画出）。
#define IDI_APPICON 101
