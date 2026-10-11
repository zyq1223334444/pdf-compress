// -*- coding: utf-8 -*-
//
// 构建时生成的右键菜单载荷声明。
//
// build/windows/build_shell_menu.bat 会产出 src/shell_payload.cpp，里面是两个**十六进制
// 字符串**常量：
//
//     kShellMsixHex   pdf_compress_shell.msix（已签名的 MSIX 稀疏包）
//     kShellCerHex    pdf_compress_shell.cer（自签代码签名证书的公钥部分）
//
// 为什么用十六进制而不是 C 的字节数组，也不是 Python 版那种 base64+zlib：
//   * 十六进制字符串是纯 ASCII，直接写进 .cpp 不会碰到 MSVC 的源文件编码问题；
//   * 生成脚本不用引 zlib（Python 标准库有，但 C++ 侧要解码 zlib 就得再拉一个
//     依赖，而十六进制解码只是查表）；
//   * 体积代价：约 2 倍（base64 是 1.33 倍），对一个几百 KB 的 MSIX 来说无关紧要。
//
// 定义这两个符号的文件有且只有一个，由 CMakeLists.txt 二选一：
//   src/shell_payload.cpp          构建产物，不进仓库（见 .gitignore）
//   src/shell_payload_default.cpp  仓库自带，两个空字符串
// 于是"还没跑过 build/windows/build_shell_menu.bat"的检出也能直接编译通过，
// 此时 --install-menu 会说"这份构建没有带右键菜单"。这是一条正常路径，
// 不是错误：便携版和纯命令行用户根本不需要那个组件。

#pragma once

namespace pdfc {

/// pdf_compress_shell.msix 的十六进制表示；没有嵌入时是空字符串。
extern const char* kShellMsixHex;

/// pdf_compress_shell.cer 的十六进制表示；没有嵌入时是空字符串。
extern const char* kShellCerHex;

}  // namespace pdfc
