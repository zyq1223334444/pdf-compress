// -*- coding: utf-8 -*-
//
// 图形界面的入口。只有 Windows 有实现；其它平台的 gui_stub.cpp 会打印一句
// "这个构建没有图形界面"并让调用方退回命令行。

#pragma once

#include <string>
#include <vector>

namespace pdfc {

/// 打开图形界面并进入消息循环。files 是要预先进列表的 PDF（可为空）。
/// 返回进程退出码。
int run_gui(const std::vector<std::string>& files);

/// 这个构建是否带图形界面。
bool gui_available();

}  // namespace pdfc
