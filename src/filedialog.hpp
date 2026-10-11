// -*- coding: utf-8 -*-
//
// 原生文件选择对话框。
//
// 要求是"调用系统自己的文件管理器"，所以每个平台都用各自的：
//     Windows  IFileOpenDialog（资源管理器在"打开/另存为"里用的那个新式对话框，
//              不是老旧的 comdlg32 版本，也不是自绘的）
//     macOS    留给 shell 的 osascript（Tk/Python 版用的是 NSOpenPanel）
//     Linux    优先 zenity / kdialog，都没有就报错让调用方退回自绘提示
//
// Windows 这一支是纯 COM：为了一个文件对话框引 pywin32/ATL 之类都不划算。

#pragma once

#include <string>
#include <vector>

namespace pdfc {

/// 弹出"打开 PDF"对话框，返回选中的路径；取消返回空列表。
/// owner 是父窗口句柄（可为 nullptr），initial_dir 为初始目录（可空）。
std::vector<std::string> choose_files(void* owner, const std::string& initial_dir);

/// 弹出"选择文件夹"对话框，取消返回空串。
std::string choose_folder(void* owner, const std::string& initial_dir);

}  // namespace pdfc
