// -*- coding: utf-8 -*-
//
// 子进程管理的实现：Windows 用 CreateProcessW，POSIX 用 fork/execv。
//
// 为什么不用 CreateProcess 的 A 版本、也不用 system()/popen()：路径里可能有
// 中文，A 版本会按系统代码页转换而丢字符；system() 还要经过 cmd.exe 的解析，
// 多一层引号规则，也更容易被路径里的特殊字符搞坏。这里的引号规则是按
// CommandLineToArgvW 的反解析写的（见 quote_argument 的注释）。

#include "process.hpp"

#include <string>
#include <vector>

#include "common.hpp"

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <cerrno>
#  include <csignal>
#  include <cstdlib>
#  include <cstring>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <mach-o/dyld.h>
#  endif
#endif

namespace pdfc {
namespace {

#if defined(_WIN32)

bool needs_quoting(const std::wstring& text) {
    return text.empty() || text.find_first_of(L" \t\n\v\"") != std::wstring::npos;
}

/// 按 CommandLineToArgvW 的反解析规则给单个参数加引号。
///
/// Windows 的命令行是"一个字符串"，参数怎么切分由目标程序（CRT）按固定规则
/// 自己算，规则只有三条：
///   1. 空格/制表符分隔参数，所以参数本身含空白或引号时必须整体加引号；
///   2. 反斜杠只有在引号前面才有特殊含义：
///        n 个反斜杠 + 引号  -> 2n 个反斜杠 + \"（引号属于参数内容）
///        2n+1 个反斜杠 + 引号 -> 2n+1 个反斜杠 + "（引号是结束引号）
///   3. 参数末尾的反斜杠紧跟着结束引号，也必须加倍，否则会把结束引号吃掉。
/// 第 3 条最容易被忽略，而"C:\Program Files\某目录\"这种以反斜杠结尾的路径
/// 恰恰是最常见的，所以这里完整实现。
std::wstring quote_argument(const std::wstring& argument) {
    if (!needs_quoting(argument)) return argument;

    std::wstring quoted;
    quoted.push_back(L'"');
    std::size_t index = 0;
    while (true) {
        std::size_t backslashes = 0;
        while (index < argument.size() && argument[index] == L'\\') {
            ++backslashes;
            ++index;
        }
        if (index == argument.size()) {
            quoted.append(backslashes * 2, L'\\');
            break;
        }
        if (argument[index] == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
        } else {
            quoted.append(backslashes, L'\\');
        }
        quoted.push_back(argument[index]);
        ++index;
    }
    quoted.push_back(L'"');
    return quoted;
}

std::string windows_error(DWORD code) {
    return "Windows error " + std::to_string(static_cast<unsigned long>(code));
}

#endif  // _WIN32

}  // namespace

std::string current_executable_path() {
#if defined(_WIN32)
    // GetModuleFileNameW 在缓冲区不够时返回缓冲区大小（截断），所以要试着放大
    std::vector<wchar_t> buffer(1024);
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (static_cast<std::size_t>(length) < buffer.size()) {
            return wide_to_utf8(std::wstring(buffer.data(), length));
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    return std::string(buffer.data());
#else
    std::vector<char> buffer(1024);
    for (;;) {
        const ssize_t length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0) return {};
        if (static_cast<std::size_t>(length) < buffer.size()) {
            return std::string(buffer.data(), static_cast<std::size_t>(length));
        }
        buffer.resize(buffer.size() * 2);
    }
#endif
}

bool spawn_child(const std::vector<std::string>& args, ChildProcess& child,
                 std::string& error) {
    child = ChildProcess{};

    const std::string executable = current_executable_path();
    if (executable.empty()) {
        error = "无法定位本程序的可执行文件路径";
        return false;
    }

#if defined(_WIN32)
    std::wstring command = quote_argument(utf8_to_wide(executable));
    for (const std::string& argument : args) {
        command.push_back(L' ');
        command += quote_argument(utf8_to_wide(argument));
    }
    // CreateProcessW 可能会就地改写命令行缓冲区，必须给一份可写的副本
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    // 不设 STARTF_USESTDHANDLES，并且 bInheritHandles = FALSE：
    // 子进程拿不到父进程的标准句柄，既不会污染父进程的输出（父进程要独占
    // 终端上的进度行），也不会因为父进程的输出被重定向到已关闭的管道而失败。
    PROCESS_INFORMATION info{};
    const std::wstring wide_executable = utf8_to_wide(executable);
    if (!CreateProcessW(wide_executable.c_str(), mutable_command.data(), nullptr,
                        nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &info)) {
        error = "无法启动渲染子进程（" + windows_error(GetLastError()) + "）";
        return false;
    }
    // 线程句柄已经没用（我们只等进程），进程句柄留给 wait/terminate 用
    CloseHandle(info.hThread);
    child.handle = info.hProcess;
    child.pid = static_cast<long long>(info.dwProcessId);
    child.running = true;
    return true;
#else
    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const std::string& argument : args) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        error = "无法启动渲染子进程（fork 失败）";
        return false;
    }
    if (pid == 0) {
        // 子进程：execv 之后不会再回到这里；失败也只 _exit，绝不能落到父进程
        // 的代码里去（那会把整轮压缩跑第二遍）。
        ::execv(executable.c_str(), argv.data());
        ::_exit(127);
    }
    child.handle = nullptr;
    child.pid = static_cast<long long>(pid);
    child.running = true;
    return true;
#endif
}

bool wait_child(ChildProcess& child, int& exit_code, std::string& error) {
    exit_code = -1;
    if (!child.running) {
        error = "渲染子进程没有在运行";
        return false;
    }

#if defined(_WIN32)
    HANDLE handle = static_cast<HANDLE>(child.handle);
    if (handle == nullptr) {
        error = "渲染子进程的句柄无效";
        return false;
    }
    // 这里是阻塞等待。Python 版也是等整块渲染完才看取消，行为一致：
    // 取消最多只会等当前正在渲染的那一块结束。
    if (WaitForSingleObject(handle, INFINITE) != WAIT_OBJECT_0) {
        error = "等待渲染子进程失败（" + windows_error(GetLastError()) + "）";
        return false;
    }
    DWORD code = 0;
    if (!GetExitCodeProcess(handle, &code)) {
        error = "读取渲染子进程退出码失败（" + windows_error(GetLastError()) + "）";
        return false;
    }
    exit_code = static_cast<int>(code);
    child.running = false;
    return true;
#else
    int status = 0;
    const pid_t pid = static_cast<pid_t>(child.pid);
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            error = "等待渲染子进程失败";
            return false;
        }
    }
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        // 与 shell 的约定一致：被信号杀死时返回 128 + 信号号
        exit_code = 128 + WTERMSIG(status);
    } else {
        exit_code = -1;
    }
    child.running = false;
    return true;
#endif
}

void terminate_child(ChildProcess& child) {
#if defined(_WIN32)
    HANDLE handle = static_cast<HANDLE>(child.handle);
    if (handle == nullptr) {
        child.running = false;
        return;
    }
    TerminateProcess(handle, 1);
    // 等它真的结束再返回：父进程紧接着要删临时目录，不能和还在写文件的
    // 子进程抢同一个目录。
    WaitForSingleObject(handle, 2000);
    CloseHandle(handle);
    child.handle = nullptr;
    child.pid = 0;
    child.running = false;
#else
    if (!child.running || child.pid <= 0) return;
    const pid_t pid = static_cast<pid_t>(child.pid);
    ::kill(pid, SIGTERM);
    // 给它一点时间自己收尾，超时就 SIGKILL（渲染是纯计算，没什么要落盘的）
    bool reaped = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        int status = 0;
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            reaped = true;
            break;
        }
        if (done < 0 && errno != EINTR) break;
        ::usleep(50 * 1000);
    }
    if (!reaped) {
        ::kill(pid, SIGKILL);
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
    }
    child.pid = 0;
    child.running = false;
#endif
}

void close_child(ChildProcess& child) {
#if defined(_WIN32)
    if (child.handle != nullptr) {
        CloseHandle(static_cast<HANDLE>(child.handle));
    }
#endif
    child.handle = nullptr;
    child.pid = 0;
    child.running = false;
}

}  // namespace pdfc
