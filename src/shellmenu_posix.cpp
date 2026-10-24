// -*- coding: utf-8 -*-
//
// 非 Windows 平台的右键菜单实现。设计说明见 shellmenu_posix.hpp。
//
// 这里的代码全部只在 Linux / macOS 上编译；Windows 走 shellmenu.cpp。

#if !defined(_WIN32)

#include "shellmenu_posix.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "common.hpp"
#include "i18n.hpp"

namespace pdfc {
namespace {

// ── 小工具 ────────────────────────────────────────────────────

/// 写一个文件，可选授予可执行权限。父目录不存在就顺手建出来。
bool write_file(const std::string& path, const std::string& text, bool executable) {
    const std::string parent = path.substr(0, path.find_last_of('/'));
    if (!parent.empty() && parent != path) {
        make_directories(parent);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    out.close();
    if (!out) return false;
    if (executable) {
        ::chmod(path.c_str(), 0755);   // 失败也不致命：菜单项退化成"用解释器打开"
    }
    return true;
}

bool remove_file(const std::string& path) {
    return std::remove(path.c_str()) == 0 || !is_regular_file(path);
}

/// 一行输出。tr() 带参数时返回的是 std::string，直接塞进 printf 的 %s 编不过，
/// 所以统一从这两个函数走。
void print_line(const std::string& text) {
    std::fputs(text.c_str(), stdout);
    std::fputc('\n', stdout);
}

void print_error(const std::string& text) {
    std::fputs(text.c_str(), stderr);
    std::fputc('\n', stderr);
}

std::string env_or(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return fallback;
    return value;
}

std::string home_dir() { return env_or("HOME", "/tmp"); }

/// $XDG_DATA_HOME，没设就是 ~/.local/share。
std::string data_home() {
    return env_or("XDG_DATA_HOME", path_join(home_dir(), ".local/share"));
}

/// 单引号包起来，内部的单引号按 shell 的规矩写成 '\''。
std::string shell_quote(const std::string& text) {
    std::string out = "'";
    for (char ch : text) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('\'');
    return out;
}

/// 装进去的脚本（Linux）与工作流（macOS）都会调用它，所以两边共用一个名字。
const char* kScriptName = "compress-pdf.sh";

/// 当前二进制自己的绝对路径：菜单项以后要用它，不能依赖 PATH。
std::string self_exe() {
    const std::string dir = executable_dir();
    const char* candidates[] = {"pdf_compress", "pdf-compress"};
    for (const char* name : candidates) {
        const std::string path = path_join(dir, name);
        if (is_regular_file(path)) return path;
    }
    return path_join(dir, candidates[0]);
}

/// 没读过设置时的兜底目标（MB）。命令行版没有图形界面可以让人改，所以这里用
/// 一个偏保守的值：宁可压得不够狠，也不要把文件压糊。
const char* kFallbackTarget = "8";

// ── 共用的 shell 片段 ─────────────────────────────────────────

/// 生成实际的压缩脚本。
///
/// 目标大小每次运行现从 settings.ini 读，而不是安装时写死：用户改了图形界面里
/// 的目标大小之后，菜单项应该跟着变。
std::string make_script(const std::string& exe) {
    std::string script;
    script += "#!/bin/sh\n";
    script += "# PDF Compressor —— 文件管理器右键菜单调用的入口。\n";
    script += "# 选中的文件由文件管理器作为参数传进来。\n";
    script += "set -u\n";
    script += "exe=" + shell_quote(exe) + "\n";
    script += "settings=\"${XDG_CONFIG_HOME:-$HOME/.config}/pdf-compress/settings.ini\"\n";
    script += "target=''\n";
    script += "[ -f \"$settings\" ] && target=$(sed -n 's/^target_size=//p' \"$settings\" | head -n 1)\n";
    script += "case \"$target\" in\n";
    script += "    ''|*[!0-9.]*) target='" + std::string(kFallbackTarget) + "' ;;\n";
    script += "esac\n";
    script += "log_dir=\"${XDG_CACHE_HOME:-$HOME/.cache}/pdf-compress\"\n";
    script += "mkdir -p \"$log_dir\" 2>/dev/null\n";
    script += "log=\"$log_dir/menu.log\"\n";
    script += "ok=0\n";
    script += "failed=0\n";
    script += "for f in \"$@\"; do\n";
    script += "    [ -f \"$f\" ] || continue\n";
    script += "    case \"$(printf '%s' \"$f\" | tr 'A-Z' 'a-z')\" in\n";
    script += "        *.pdf) ;;\n";
    script += "        *) continue ;;\n";
    script += "    esac\n";
    script += "    if \"$exe\" \"$f\" \"$target\" >>\"$log\" 2>&1; then\n";
    script += "        ok=$((ok + 1))\n";
    script += "    else\n";
    script += "        failed=$((failed + 1))\n";
    script += "    fi\n";
    script += "done\n";
    script += "[ $((ok + failed)) -eq 0 ] && exit 0\n";
    script += "if [ \"$failed\" -eq 0 ]; then\n";
    script += "    msg=$(printf " + shell_quote(tr("[完成] 已压缩 %s 个文件到 %s MB")) +
              " \"$ok\" \"$target\")\n";
    script += "else\n";
    script += "    msg=$(printf " + shell_quote(tr("[警告] %s 个成功、%s 个失败（日志：%s）")) +
              " \"$ok\" \"$failed\" \"$log\")\n";
    script += "fi\n";
    script += "if command -v zenity >/dev/null 2>&1; then\n";
    script += "    zenity --info --title 'PDF Compressor' --text \"$msg\" 2>/dev/null && exit 0\n";
    script += "fi\n";
    script += "if command -v kdialog >/dev/null 2>&1; then\n";
    script += "    kdialog --title 'PDF Compressor' --msgbox \"$msg\" 2>/dev/null && exit 0\n";
    script += "fi\n";
    script += "if command -v notify-send >/dev/null 2>&1; then\n";
    script += "    notify-send 'PDF Compressor' \"$msg\" 2>/dev/null && exit 0\n";
    script += "fi\n";
    script += "exit 0\n";
    return script;
}

/// 菜单项显示的名字。跟着界面语言走，与 Windows 那边保持一致。
std::string menu_title() {
    return (language() == Language::En) ? "Compress with PDF Compressor"
                                        : "用 PDF Compressor 压缩";
}

// ── Linux：各文件管理器的入口文件 ──────────────────────────────

std::string nemo_action(const std::string& script) {
    std::string out;
    out += "[Nemo Action]\n";
    out += "Name=" + menu_title() + "\n";
    out += "Comment=" + std::string(tr("把这个 PDF 压到设置里的目标大小")) + "\n";
    // %F 是"选中的全部文件"；用 bash -c 包一层，路径里带空格也不会被拆开。
    out += "Exec=bash -c '\"" + script + "\" \"$@\"' _ %F\n";
    out += "IconName=application-pdf\n";
    out += "Selection=notnone\n";
    out += "Extensions=pdf;\n";
    out += "Quote=double\n";
    return out;
}

std::string dolphin_service_menu(const std::string& script) {
    std::string out;
    out += "[Desktop Entry]\n";
    out += "Type=Service\n";
    out += "ServiceTypes=KonqPopupMenu/Plugin\n";
    out += "MimeType=application/pdf;\n";
    out += "Actions=CompressPdf;\n";
    out += "Icon=application-pdf\n";
    out += "X-KDE-Priority=TopLevel\n";
    out += "\n";
    out += "[Desktop Action CompressPdf]\n";
    out += "Name=" + menu_title() + "\n";
    out += "Exec=" + shell_quote(script) + " %F\n";
    out += "Icon=application-pdf\n";
    return out;
}

}  // namespace

// ── Linux ─────────────────────────────────────────────────────

#if defined(__linux__)

int install_shell_menu_posix(bool quiet) {
    const std::string exe = self_exe();
    if (!is_regular_file(exe)) {
        print_error(tr("[失败] 找不到 {exe}。", {{"exe", kExeName}}));
        return kExitFail;
    }

    const std::string data = data_home();
    const std::string script = path_join(data, std::string("pdf-compress/") + kScriptName);
    if (!write_file(script, make_script(exe), true)) {
        print_error(tr("[失败] 无法写入 {path}。", {{"path", script}}));
        return kExitFail;
    }
    if (!quiet) {
        print_line(tr("[完成] 已写入压缩脚本：{path}", {{"path", script}}));
    }

    // Nautilus 与 Caja 的脚本目录：文件名就是菜单项的名字，必须可执行。
    // 选中的文件会作为参数传进脚本。
    const std::string title = menu_title();
    const std::string script_file = path_join(script.substr(0, script.find_last_of('/')),
                                              kScriptName);
    const std::string wrappers[] = {
        path_join(data, "nautilus/scripts/" + title),
        path_join(data, "caja/scripts/" + title),
    };
    for (const std::string& path : wrappers) {
        const std::string body = "#!/bin/sh\nexec " + shell_quote(script_file) + " \"$@\"\n";
        if (!write_file(path, body, true)) {
            print_error(tr("[警告] 无法写入 {path}，对应的文件管理器不会有这一项。",
                            {{"path", path}}));
        } else if (!quiet) {
            print_line(tr("[完成] 已安装菜单项：{path}", {{"path", path}}));
        }
    }

    // Nemo 与 Dolphin 用的是各自的配置文件，不需要可执行位。
    const std::string menues[] = {
        path_join(data, "nemo/actions/pdf-compress.nemo_action"),
        path_join(data, "kio/servicemenus/pdf-compress.desktop"),
    };
    const std::string bodies[] = {nemo_action(script_file), dolphin_service_menu(script_file)};
    for (std::size_t index = 0; index < 2; ++index) {
        if (!write_file(menues[index], bodies[index], false)) {
            print_error(tr("[警告] 无法写入 {path}，对应的文件管理器不会有这一项。",
                            {{"path", menues[index]}}));
        } else if (!quiet) {
            print_line(tr("[完成] 已安装菜单项：{path}", {{"path", menues[index]}}));
        }
    }

    if (!quiet) {
        print_line(tr("[提示] Nautilus/Caja 的项在“脚本”子菜单里，Dolphin 在“动作”"
                               "子菜单里；Nemo 直接显示在右键菜单中。"));
        print_line(tr("[提示] 目标大小取自设置文件，改完设置重新打开文件管理器即可。"));
    }
    return kExitOk;
}

int uninstall_shell_menu_posix(bool quiet) {
    const std::string data = data_home();
    const std::string title = menu_title();
    const std::vector<std::string> files = {
        path_join(data, "nautilus/scripts/" + title),
        path_join(data, "caja/scripts/" + title),
        path_join(data, "nemo/actions/pdf-compress.nemo_action"),
        path_join(data, "kio/servicemenus/pdf-compress.desktop"),
        path_join(data, std::string("pdf-compress/") + kScriptName),
    };
    for (const std::string& path : files) {
        remove_file(path);
    }
    // 语言切换过之后菜单项名字会不一样，上面只删得掉当前语言那个，这里把另一个
    // 也顺手清掉。
    remove_file(path_join(data, "nautilus/scripts/Compress with PDF Compressor"));
    remove_file(path_join(data, "nautilus/scripts/用 PDF Compressor 压缩"));
    remove_file(path_join(data, "caja/scripts/Compress with PDF Compressor"));
    remove_file(path_join(data, "caja/scripts/用 PDF Compressor 压缩"));
    if (!quiet) {
        print_line(tr("[完成] 右键菜单已移除。"));
    }
    return kExitOk;
}

// ── macOS ─────────────────────────────────────────────────────

#elif defined(__APPLE__)

namespace {

/// .workflow 包里那份 Info.plist：声明"只对 PDF 出现"的快速操作。
std::string mac_info_plist(const std::string& title) {
    std::string out;
    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
    out += "<plist version=\"1.0\">\n<dict>\n";
    out += "    <key>NSServices</key>\n    <array>\n        <dict>\n";
    out += "            <key>NSMenuItem</key>\n            <dict>\n";
    out += "                <key>default</key>\n                <string>" + title +
           "</string>\n";
    out += "            </dict>\n";
    out += "            <key>NSMessage</key>\n            <string>runWorkflowAsService</string>\n";
    out += "            <key>NSSendFileTypes</key>\n            <array>\n";
    out += "                <string>com.adobe.pdf</string>\n            </array>\n";
    out += "        </dict>\n    </array>\n";
    out += "</dict>\n</plist>\n";
    return out;
}

/// document.wflow：一个 Run Shell Script 动作，输入方式设成"作为参数传入"。
std::string mac_document_wflow(const std::string& script_path) {
    // 脚本本身已经在 ~/Library/Application Support 里，这里只负责转调。
    const std::string command = "\"" + script_path + "\" \"$@\"";
    std::string out;
    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
    out += "<plist version=\"1.0\">\n<dict>\n";
    out += "    <key>AMApplicationBuild</key>\n    <string>523</string>\n";
    out += "    <key>AMApplicationVersion</key>\n    <string>2.10</string>\n";
    out += "    <key>AMDocumentVersion</key>\n    <string>2</string>\n";
    out += "    <key>actions</key>\n    <array>\n        <dict>\n";
    out += "            <key>action</key>\n            <dict>\n";
    out += "                <key>AMAccepts</key>\n                <dict>\n";
    out += "                    <key>Container</key>\n                    <string>List</string>\n";
    out += "                    <key>Optional</key>\n                    <true/>\n";
    out += "                    <key>Types</key>\n                    <array>\n";
    out += "                        <string>com.apple.cocoa.string</string>\n";
    out += "                    </array>\n                </dict>\n";
    out += "                <key>AMActionVersion</key>\n                <string>2.0.3</string>\n";
    out += "                <key>AMApplication</key>\n                <array>\n";
    out += "                    <string>Automator</string>\n                </array>\n";
    out += "                <key>AMParameterProperties</key>\n                <dict>\n";
    out += "                    <key>COMMAND_STRING</key>\n                    <dict/>\n";
    out += "                    <key>CheckedForUserDefaultShell</key>\n                    <dict/>\n";
    out += "                    <key>inputMethod</key>\n                    <dict/>\n";
    out += "                    <key>shell</key>\n                    <dict/>\n";
    out += "                    <key>source</key>\n                    <dict/>\n";
    out += "                </dict>\n";
    out += "                <key>AMProvides</key>\n                <dict>\n";
    out += "                    <key>Container</key>\n                    <string>List</string>\n";
    out += "                    <key>Types</key>\n                    <array>\n";
    out += "                        <string>com.apple.cocoa.string</string>\n";
    out += "                    </array>\n                </dict>\n";
    out += "                <key>ActionBundlePath</key>\n";
    out += "                <string>/System/Library/Automator/Run Shell Script.action</string>\n";
    out += "                <key>ActionName</key>\n                <string>Run Shell Script</string>\n";
    out += "                <key>ActionParameters</key>\n                <dict>\n";
    out += "                    <key>COMMAND_STRING</key>\n                    <string>" + command +
           "</string>\n";
    out += "                    <key>CheckedForUserDefaultShell</key>\n                    <true/>\n";
    // 1 = "把输入作为参数传入"，配合下面的 \"$@\"。
    out += "                    <key>inputMethod</key>\n                    <integer>1</integer>\n";
    out += "                    <key>shell</key>\n                    <string>/bin/sh</string>\n";
    out += "                    <key>source</key>\n                    <string></string>\n";
    out += "                </dict>\n";
    out += "                <key>BundleIdentifier</key>\n";
    out += "                <string>com.apple.RunShellScript</string>\n";
    out += "                <key>CFBundleVersion</key>\n                <string>2.0.3</string>\n";
    out += "                <key>CanShowSelectedItemsWhenRun</key>\n                <false/>\n";
    out += "                <key>CanShowWhenRun</key>\n                <true/>\n";
    out += "                <key>Category</key>\n                <array>\n";
    out += "                    <string>AMCategoryUtilities</string>\n                </array>\n";
    out += "                <key>Class Name</key>\n                <string>RunShellScriptAction</string>\n";
    out += "                <key>InputUUID</key>\n";
    out += "                <string>1B2C3D4E-5F60-4718-9AAB-0C1D2E3F4A5B</string>\n";
    out += "                <key>OutputUUID</key>\n";
    out += "                <string>2C3D4E5F-6071-4829-8BBC-1D2E3F4A5B6C</string>\n";
    out += "                <key>UUID</key>\n";
    out += "                <string>3D4E5F60-7182-493A-9CCD-2E3F4A5B6C7D</string>\n";
    out += "                <key>UnlocalizedApplications</key>\n                <array>\n";
    out += "                    <string>Automator</string>\n                </array>\n";
    out += "                <key>arguments</key>\n                <dict/>\n";
    out += "                <key>isViewVisible</key>\n                <integer>1</integer>\n";
    out += "                <key>nibPath</key>\n";
    out += "                <string>/System/Library/Automator/Run Shell Script.action/Contents/"
           "Resources/Base.lproj/main.nib</string>\n";
    out += "            </dict>\n            <key>isViewVisible</key>\n            <integer>1</integer>\n";
    out += "        </dict>\n    </array>\n";
    out += "    <key>connectors</key>\n    <dict/>\n";
    out += "    <key>workflowMetaData</key>\n    <dict>\n";
    out += "        <key>serviceInputTypeIdentifier</key>\n";
    out += "        <string>com.apple.Automator.fileSystemObject</string>\n";
    out += "        <key>serviceOutputTypeIdentifier</key>\n";
    out += "        <string>com.apple.Automator.nothing</string>\n";
    out += "        <key>serviceProcessesInput</key>\n        <integer>0</integer>\n";
    out += "        <key>workflowTypeIdentifier</key>\n";
    out += "        <string>com.apple.Automator.servicesMenu</string>\n";
    out += "    </dict>\n</dict>\n</plist>\n";
    return out;
}

}  // namespace

int install_shell_menu_posix(bool quiet) {
    const std::string exe = self_exe();
    if (!is_regular_file(exe)) {
        print_error(tr("[失败] 找不到 {exe}。", {{"exe", kExeName}}));
        return kExitFail;
    }

    // 脚本放在 Application Support 下，工作流只是转调它；这样菜单项名字（也就是
    // 目录名）换语言时不会牵动脚本路径。
    const std::string support = path_join(home_dir(), "Library/Application Support/pdf-compress");
    const std::string script = path_join(support, kScriptName);
    if (!write_file(script, make_script(exe), true)) {
        print_error(tr("[失败] 无法写入 {path}。", {{"path", script}}));
        return kExitFail;
    }

    const std::string workflow =
        path_join(home_dir(), "Library/Services/" + menu_title() + ".workflow");
    const std::string contents = path_join(workflow, "Contents");
    bool ok = write_file(path_join(contents, "Info.plist"), mac_info_plist(menu_title()), false);
    ok = write_file(path_join(contents, "document.wflow"), mac_document_wflow(script), false) && ok;
    if (!ok) {
        print_error(tr("[失败] 无法写入 {path}。", {{"path", workflow}}));
        return kExitFail;
    }

    if (!quiet) {
        print_line(tr("[完成] 已安装右键菜单：{path}", {{"path", workflow}}));
        print_line(tr("[提示] 在 Finder 里右键 PDF，菜单在“快速操作”里；"
                               "没有的话到“系统设置 → 键盘 → 键盘快捷键 → 服务”里勾选。"));
    }
    return kExitOk;
}

int uninstall_shell_menu_posix(bool quiet) {
    const std::vector<std::string> files = {
        path_join(home_dir(), "Library/Services/Compress with PDF Compressor.workflow/Contents/Info.plist"),
        path_join(home_dir(),
                  "Library/Services/Compress with PDF Compressor.workflow/Contents/document.wflow"),
        path_join(home_dir(), "Library/Services/用 PDF Compressor 压缩.workflow/Contents/Info.plist"),
        path_join(home_dir(), "Library/Services/用 PDF Compressor 压缩.workflow/Contents/document.wflow"),
        path_join(home_dir(), std::string("Library/Application Support/pdf-compress/") + kScriptName),
    };
    for (const std::string& path : files) remove_file(path);
    if (!quiet) {
        print_line(tr("[完成] 右键菜单已移除。"));
    }
    return kExitOk;
}

#else  // 其他 Unix：没有已知的文件管理器集成方式

int install_shell_menu_posix(bool quiet) {
    print_error(tr("[失败] 右键菜单只有 Windows、macOS 和 Linux 支持。"));
    (void)quiet;
    return kExitFail;
}

int uninstall_shell_menu_posix(bool quiet) {
    print_error(tr("[失败] 右键菜单只有 Windows、macOS 和 Linux 支持。"));
    (void)quiet;
    return kExitFail;
}

#endif

}  // namespace pdfc

#endif  // !_WIN32
