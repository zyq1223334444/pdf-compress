// -*- coding: utf-8 -*-
//
// Linux / macOS 的平台上实现。
//
// common.hpp 里声明了一批"平台相关"的函数，Windows 那一份在 platform_win.cpp
// （Win32 的宽字符 API、%LOCALAPPDATA%、控制台附加、DPI 声明），这一份是
// POSIX 的对应物。CMakeLists.txt 里二者互斥：WIN32 编 platform_win.cpp，
// 其它平台编这一个，所以两边必须把同一组符号补齐。
//
// POSIX 上这些函数大多是"能少做就少做"：
//   * 路径本来就是 UTF-8 字节串，跟内核、libc 完全一致，所以宽字符互转原样返回，
//     不做真正的编码转换——真转了反而会把不是 UTF-8 的文件名弄坏。
//     纯 ASCII 的路径经这条朴素路径也完全正确，而 ASCII 覆盖了绝大多数构建/CI 场景。
//   * 控制台和 DPI 都是 Windows 特有的概念，这里全是空实现，好让上层代码
//     （main.cpp / gui）可以无条件调用它们，不必到处写 #ifdef。

#include "common.hpp"

// MSVC 把 getenv 标成"不安全"（C4996）。这里保留标准 C 的 getenv 而不是换成
// Windows 专有的 _dupenv_s：这个文件是给 Linux/macOS 编的，_dupenv_s 在那两个
// 平台上根本不存在，用一个平台宏去切换只会把简单的事情弄复杂。
// 只有在本文件被 Windows 编译器拿来跑一遍（做语法检查）时这条才会生效，
// 那时整段 POSIX 实现本来也不会被编进去。
#if defined(_MSC_VER)
#  pragma warning(disable : 4996)
#endif

#include <cstdlib>
#include <string>

#if defined(__APPLE__)
#  include <mach-o/dyld.h>   // _NSGetExecutablePath
#  include <unistd.h>        // realpath
#elif defined(__linux__)
#  include <sys/types.h>     // ssize_t
#  include <unistd.h>        // readlink
#endif

// 注意 #if 的结构：Windows 上两种平台宏都不成立，于是 executable_dir() 会落到
// 最下面那个 #else（返回 "."）——这个文件在 Windows 上永远不该被编
// （CMakeLists.txt 的 WIN32 分支编的是 platform_win.cpp，两边提供同一批符号），
// 但"被错编进来"时它也必须能编过，所以每个分支都要有个说得通的落点。
// 真正不能写的是 "#if __APPLE__ ... #else <Linux 代码>"——那样 Windows 会掉进
// Linux 分支，readlink 直接编不过，这正是第一版踩过的坑。

namespace pdfc {

// ── UTF-8 与宽字符 ────────────────────────────────────────────

std::wstring utf8_to_wide(const std::string& text) {
    // 朴素逐字节加宽：POSIX 上没有任何 API 需要真正的 UTF-16，而 wchar_t 在
    // Linux 上是 4 字节的 UTF-32、在 macOS 上是 4 字节，语义本来就和 Windows
    // 的 UTF-16 不一样。"转换"在这里只会引入错误的机会。
    return std::wstring(text.begin(), text.end());
}

std::string wide_to_utf8(const std::wstring& text) {
    // 反过来同样是"能原样就原样"。逐字符显式收窄（static_cast）而不是用
    // std::string(text.begin(), text.end())：后者在 MSVC 上会报 C4244
    // （wchar_t -> char 可能丢数据），而这条路径本来就是给 Windows 之外的
    // 兜底，丢数据不丢数据都不影响真实调用者——POSIX 上真正要转字符串的地方
    // 只有上面的 executable_dir()，它在各自的分支里直接用窄字符串。
    std::string out;
    out.reserve(text.size());
    for (wchar_t ch : text) out.push_back(static_cast<char>(ch));
    return out;
}

// ── 路径 ──────────────────────────────────────────────────────

std::string executable_dir() {
#if defined(__APPLE__)
    // macOS 没有 /proc，官方的办法是问 dyld 要自己的路径。
    // _NSGetExecutablePath 可能返回一个带 .. 的相对路径，所以先用 realpath 规范一次。    char stack_buffer[4096];
    std::uint32_t size = sizeof(stack_buffer);
    std::string path;
    if (_NSGetExecutablePath(stack_buffer, &size) == 0) {
        path = stack_buffer;
    } else {
        // 返回非 0 表示缓冲区不够，size 已经被改成实际需要的长度
        std::string grown(size, '\0');
        if (_NSGetExecutablePath(grown.data(), &size) == 0) {
            grown.resize(std::char_traits<char>::length(grown.c_str()));
            path = grown;
        }
    }
    if (path.empty()) return ".";
    // realpath 解掉 ".." 和符号链接；失败就退回原路径（至少能用）
    if (char* resolved = ::realpath(path.c_str(), nullptr)) {
        std::string answer = resolved;
        std::free(resolved);
        path = answer;
    }
#elif defined(__linux__)
    // Linux：/proc/self/exe 是一个指向真实可执行文件的符号链接，即使进程是通过
    // PATH 起的、argv[0] 被改过也准确。容器里 /proc 没挂的极端情况下退回 "."
    // （调用方自己会处理相对路径）。
    std::string path;
    char buffer[4096];
    const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length > 0) {
        buffer[length] = '\0';
        path = buffer;
    } else {
        path = ".";
    }
#else
    // 其它 POSIX 系统（BSD 等）没有这层支持，返回当前目录。
    // 这段代码只在"既不是 macOS 也不是 Linux"时才会被编到，而 Windows
    // 走的是 platform_win.cpp，根本不会编这个文件。
    const std::string path = ".";
#endif
    const std::string directory = path_dirname(path);
    return directory.empty() ? std::string(".") : directory;
}

std::string user_config_dir() {
    // 与 Python 版 config._local_appdata() 一致的规则：
    // XDG_CONFIG_HOME 优先，否则 ~/.config，最后拼上 /pdf-compress。
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string base;
    if (xdg != nullptr && *xdg != '\0') {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        if (home == nullptr || *home == '\0') {
            // 连 HOME 都没有（某些 CI / 守护进程环境）：退到临时目录下的固定名字，
            // 这样设置文件至少能落在某个可写的地方，而不是写进 "/pdf-compress"。
            const char* tmp = std::getenv("TMPDIR");
            base = (tmp != nullptr && *tmp != '\0') ? tmp : "/tmp";
        } else {
            base = path_join(home, ".config");
        }
    }
    return path_join(base, kAppId);
}

// ── Windows 专属概念的空实现 ─────────────────────────────────

void setup_console() {
    // POSIX 上标准流一定存在（哪怕指向 /dev/null），没有"GUI 子系统没有控制台"
    // 这回事，所以什么都不用做。
}

void attach_parent_console() {
    // 同上：不需要附加，也就没有"父控制台"要找。
}

void detach_console() {
    // 图形界面在非 Windows 上不存在（见 gui_stub.cpp），这里保持空实现，
    // 让上层可以无条件调用。
}

void enable_dpi_awareness() {
    // DPI 感知是 Win32 的概念。GTK/Qt 之类的工具包有自己的缩放处理，
    // 这个项目在 POSIX 上没有图形界面，所以什么都不用做。
}

}  // namespace pdfc
