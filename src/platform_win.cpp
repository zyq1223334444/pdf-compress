// -*- coding: utf-8 -*-
//
// Windows 平台层：UTF-8 与宽字符互转、可执行文件位置、控制台接管、DPI 感知。
//
// 关于控制台：可执行文件按 GUI 子系统编译（双击时不会多弹一个黑框），代价是
// 进程默认没有控制台。attach_parent_console 在"标准句柄无效、但父进程有控制台"
// 时附加过去并把三个流接回 CONOUT$/CONIN$；句柄本来就有效（被终端、管道或文件
// 占着）时什么都不做——重定向必须原样保留，这一点搞错的话 `> log.txt` 和
// `| findstr` 都会静默失效（Nuitka 的 --windows-console-mode=attach 就是这么错的，
// 这个项目踩过一次）。

#include "common.hpp"

#if defined(_WIN32)

#include <fcntl.h>
#include <io.h>
#include <windows.h>

#include <cstdio>
#include <string>

namespace pdfc {
namespace {

/// 附加到控制台之后，把 C 运行时的三个流重新绑到控制台设备上。
void rebind_streams() {
    const HANDLE console_out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                           OPEN_EXISTING, 0, nullptr);
    if (console_out != INVALID_HANDLE_VALUE) {
        SetStdHandle(STD_OUTPUT_HANDLE, console_out);
        SetStdHandle(STD_ERROR_HANDLE, console_out);
        FILE* stream = nullptr;
        (void)freopen_s(&stream, "CONOUT$", "w", stdout);
        (void)freopen_s(&stream, "CONOUT$", "w", stderr);
    }
    const HANDLE console_in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                          OPEN_EXISTING, 0, nullptr);
    if (console_in != INVALID_HANDLE_VALUE) {
        SetStdHandle(STD_INPUT_HANDLE, console_in);
        FILE* stream = nullptr;
        (void)freopen_s(&stream, "CONIN$", "r", stdin);
    }
}

}  // namespace

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        wide.data(), size);
    return wide;
}

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) return {};
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        utf8.data(), size, nullptr, nullptr);
    return utf8;
}

std::string executable_dir() {
    // 路径可能比 MAX_PATH 长（长路径支持打开时），所以循环扩容而不是一次到位
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return path_dirname(wide_to_utf8(buffer));
}

std::string user_config_dir() {
    std::wstring buffer(MAX_PATH, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(),
                                                 static_cast<DWORD>(buffer.size()));
    std::string base;
    if (length > 0 && length < buffer.size()) {
        buffer.resize(length);
        base = wide_to_utf8(buffer);
    } else {
        // 极端情况下（服务账户、被裁剪的环境）退回用户主目录下的隐藏目录
        base = path_dirname(path_dirname(executable_dir()));
    }
    return path_join(base, kAppId);
}

void attach_parent_console() {
    const auto usable = [](DWORD handle_id) {
        const HANDLE handle = GetStdHandle(handle_id);
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return false;
        return GetFileType(handle) != FILE_TYPE_UNKNOWN;
    };

    // 标准句柄还有效，说明输出被终端、管道或文件占着——绝不能动
    if (usable(STD_OUTPUT_HANDLE) || usable(STD_ERROR_HANDLE)) return;
    // 父进程（资源管理器）没有控制台时，安静地当纯图形程序
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    rebind_streams();
}

void setup_console() {
    attach_parent_console();
    // 控制台代码页设成 UTF-8，否则中文在默认的 936 代码页下会变乱码；
    // 二进制模式让 fputs 原样写 UTF-8 字节，不再经过代码页转换。
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    for (FILE* stream : {stdout, stderr}) {
        if (stream) (void)_setmode(_fileno(stream), _O_BINARY);
    }
}

void detach_console() {
    if (GetConsoleWindow() != nullptr) FreeConsole();
}

void enable_dpi_awareness() {
    // 系统 DPI 感知：界面自己按 dpi/96 缩放，比让系统把整个窗口拉伸要清晰，
    // 又不像"每显示器感知"那样在窗口跨屏时布局错位。
    // 动态取 SetProcessDpiAwarenessContext，老系统上没有就退回老 API。
    using SetContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        auto fn = reinterpret_cast<SetContextFn>(
            reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
        if (fn != nullptr && fn(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE)) return;
    }
    SetProcessDPIAware();
}

}  // namespace pdfc

#endif  // _WIN32
