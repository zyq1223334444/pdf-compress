// -*- coding: utf-8 -*-
//
// Windows 11 一级右键菜单的安装与卸载。
//
// Windows 11 的"一级"右键菜单（不用点"显示更多选项"）只认一种注册方式：
// 把一个实现了 IExplorerCommand 的 COM DLL 放进 MSIX 稀疏包（sparse package），
// 再用 windows.fileExplorerContextMenus 声明它对 .pdf 生效。传统注册项在
// Windows 11 上只会掉进"显示更多选项"，所以两者都注册：一级菜单用 MSIX，
// 传统项留给 Windows 10 和那个子菜单。
//
// 各步骤需要什么权限：
//     自签证书进"受信任人"证书库   需要管理员（装过一次就够，安装包会先做）
//     Add-AppxPackage              不需要管理员（按用户注册）
//     HKCU 下的传统右键菜单项       不需要管理员
//
// 于是便携版第一次执行 --install-menu 会请求提权（只为装证书），之后升级/重装
// 都不再需要；安装包已经用管理员权限装好证书，它调用 --install-menu --quiet
// 时不会再弹 UAC。

#pragma once

#include <string>

namespace pdfc {

// 与 shell/AppxManifest.xml 必须逐字一致
inline constexpr const char* kShellPackageName = "zyq1223334444.PdfCompressShell";
inline constexpr const char* kShellPublisher = "CN=zyq1223334444";
inline constexpr const char* kShellMsixName = "pdf_compress_shell.msix";
inline constexpr const char* kShellCerName = "pdf_compress_shell.cer";

/// 加右键菜单。quiet 为真时不打印过程（安装包调用时用），也不会提权。
int install_shell_menu(bool quiet);

/// 去掉右键菜单，顺带清理证书与注册表。
int uninstall_shell_menu(bool quiet);

}  // namespace pdfc
