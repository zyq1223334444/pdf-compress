// -*- coding: utf-8 -*-
//
// 右键菜单载荷的**占位实现**，这份是仓库内容，永远跟着源码走。
//
// 真正的载荷是构建产物 src/shell_payload.cpp：shell/build_shell_menu.bat 把编出来的
// pdf_compress_shell.msix 与 .cer 编成两个十六进制字符串写进去（生成器是
// shell/make_payload.py）。
//
// 为什么要分成两个文件、而不是让生成脚本直接覆盖这一份：
//   * 生成出来的 src/shell_payload.cpp 带自签证书，每次构建都不一样，必须进
//     .gitignore；
//   * 如果占位实现也写在那个路径上，一次干净的 clone 里就**没有**任何一份
//     shell_payload.cpp，CMakeLists.txt 于是什么都不加，链接阶段直接报
//     "无法解析的外部符号 kShellMsixHex"——这在"只想编个命令行版本"的人手里
//     是最难懂的一种报错。
//   所以约定是：本文件永远参与编译，只有当 src/shell_payload.cpp 存在时才换成它，
//   两者不会同时进源文件列表（见 CMakeLists.txt）。
//
// 用占位实现编出来的程序照常运行，只是 --install-menu 会说明这份构建没有带右键
// 菜单组件。这是一条正常路径，不是错误：便携版和纯命令行用户根本不需要那个组件。

#include "shell_payload.hpp"

namespace pdfc {

const char* kShellMsixHex = "";
const char* kShellCerHex = "";

}  // namespace pdfc
