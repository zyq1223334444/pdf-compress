// -*- coding: utf-8 -*-
//
// Windows 11 一级右键菜单的安装与卸载。
//
// 这是 Python 版 pdfcomp/shellmenu.py 的移植，逻辑顺序、每一步的文案都与它逐字
// 一致（英文表由 tools/make_strings_en.py 从 lang_en.py 生成，两边共用同一批键）。
//
// 各步骤需要什么权限（和 shellmenu.hpp 的说明一致）：
//     自签证书进"受信任人"证书库   需要管理员（装过一次就够，安装包会先做）
//     Add-AppxPackage              不需要管理员（按用户注册）
//     HKCU 下的传统右键菜单项       不需要管理员
//
// 所有外部命令都通过 PowerShell / certutil 走，理由和 Python 版一样：这些是系统
// 自带、随 Windows 一起更新的东西，比自己再实现一遍 MSIX 注册 API 更稳。

#include "shellmenu.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "common.hpp"
#include "config.hpp"
#include "i18n.hpp"
#include "shell_payload.hpp"

// 载荷（kShellMsixHex / kShellCerHex）的定义**只有一处**，由构建系统挑：
//
//   src/shell_payload.cpp          构建产物：shell/build_shell_menu.bat 把签名好的
//                                  MSIX 与证书编成两个十六进制字符串写进去
//   src/shell_payload_default.cpp  仓库自带：两个空字符串，--install-menu 会说
//                                  "这份构建没有带右键菜单"
//
// CMakeLists.txt 里有这个二选一（见那里的注释）。本文件以前还留了一套
// __has_include("shell_payload.cpp") 的兜底定义，那是错的：只要构建产物存在，
// 它就会被 CMake 当独立源文件编一份、又被本文件 include 进来编第二份，链接期
// 直接 LNK2005 多重定义；产物不存在时它又和 default 那份撞车。所以这里只保留
// 头文件。

// 只有 Windows 才有注册表、MSIX、ShellExecuteEx。非 Windows 上整个文件只保留
// 两个"只有 Windows 支持"的失败分支，让 CMake 不必为平台再分一次源文件列表。
#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>     // ShellExecuteExW / SHFILEOPSTRUCTW / SHFileOperationW
#  include <shlobj.h>       // IsUserAnAdmin
#endif

namespace pdfc {
namespace {

// ── 常量（与 shellmenu.hpp、AppxManifest.xml、ExplorerCommand.cpp 对齐）──────

#if defined(_WIN32)
// 这三个只在 Windows 的实现里用：注册表要写传统菜单项、PowerShell 要按包名
// 卸载。放在条件编译里面，非 Windows 的构建才不会为它们报"定义了没用"。

// 包清单里 Identity/@Name，PowerShell 的 Get-AppxPackage/Remove-AppxPackage 用它。
const char* kPackageName = "zyq1223334444.PdfCompressShell";
// 证书主题。证书库和注册表里的比对都用它。
const char* kPublisher = "CN=zyq1223334444";

// 传统右键菜单项（Windows 10 与 Windows 11 的"显示更多选项"）。
// 一级菜单走 MSIX，这一项是给老系统兜底的，两者都要注册。
const wchar_t* kVerbKey =
    L"Software\\Classes\\SystemFileAssociations\\.pdf\\shell\\PdfCompress";

constexpr DWORD kCreateNoWindow = 0x08000000;   // CREATE_NO_WINDOW
#endif

// ── 小工具 ────────────────────────────────────────────────────

#if defined(_WIN32)

/// 十六进制字符串 -> 字节。奇数长度或非法字符就失败（宁可报错也不写半个文件：
/// 半个 MSIX 只会换来一个更难懂的 0x80073CF9）。
bool hex_decode(const char* hex, std::string& out) {
    out.clear();
    if (hex == nullptr) return false;
    const std::size_t length = std::strlen(hex);
    if (length % 2 != 0) return false;
    out.reserve(length / 2);
    const auto value_of = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    for (std::size_t index = 0; index < length; index += 2) {
        const int high = value_of(hex[index]);
        const int low = value_of(hex[index + 1]);
        if (high < 0 || low < 0) {
            out.clear();
            return false;
        }
        out.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

/// 把字节写到文件，成功返回真。
bool write_binary(const std::string& path, const std::string& data) {
    const std::wstring wide = utf8_to_wide(path);
    HANDLE handle = ::CreateFileW(wide.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    std::size_t written_total = 0;
    bool ok = true;
    while (written_total < data.size()) {
        const std::size_t remaining = data.size() - written_total;
        const DWORD chunk = static_cast<DWORD>(remaining > (1u << 20) ? (1u << 20) : remaining);
        DWORD written = 0;
        if (!::WriteFile(handle, data.data() + written_total, chunk, &written, nullptr) ||
            written == 0) {
            ok = false;
            break;
        }
        written_total += written;
    }
    ::CloseHandle(handle);
    return ok;
}

/// 跑一个外部命令并等它结束，返回 (退出码, stdout+stderr 的原始字节)。
///
/// 这里踩过坑，所以写清楚：
///   1. 必须给子进程建一条匿名管道，并把**写端设为可继承**（SECURITY_ATTRIBUTES 的
///      bInheritHandle = TRUE），否则子进程拿不到这个句柄，工具的输出会跑到父进程的
///      控制台上，我们什么都读不到。
///   2. CreateProcessW 必须传 bInheritHandles = TRUE，并把 STARTUPINFO 的
///      hStdOutput/hStdError 指向管道的写端、dwFlags 带上 STARTF_USESTDHANDLES。
///      只设 STARTF_USESTDHANDLES 而不给句柄，等于把子进程的标准流全关掉。
///   3. 父进程手里那份**写端必须马上关掉**。管道的读端要等到"所有写端都关闭"才会
///      收到 EOF；父进程自己留着一份写端不放，ReadFile 就会永远阻塞——这是最典型
///      的管道死锁。读完了再 CloseHandle(读端)。
///   4. 读到 EOF 之后再 WaitForSingleObject；顺序反了就可能在大输出量上卡死
///      （管道缓冲区写满，子进程阻塞在写，父进程阻塞在等）。
struct CommandResult {
    int status = -1;
    std::string output;   // 原始字节，不做代码页转换（证书库的输出需要按字节比对）
};

CommandResult run_command(const std::string& command_line) {
    CommandResult result;

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;   // 见上面第 1 点
    security.lpSecurityDescriptor = nullptr;

    HANDLE read_end = nullptr;
    HANDLE write_end = nullptr;
    if (!::CreatePipe(&read_end, &write_end, &security, 0)) return result;
    // 读端只给父进程用，不能被子进程继承（继承了只多一个句柄，没坏处，
    // 但让语义干净一点）。
    ::SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;   // 见上面第 2 点
    startup.hStdOutput = write_end;
    startup.hStdError = write_end;
    startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process{};
    std::wstring mutable_command = utf8_to_wide(command_line);
    const BOOL started = ::CreateProcessW(
        nullptr, mutable_command.data(), nullptr, nullptr,
        TRUE,                 // bInheritHandles：管道就是靠它传下去的
        kCreateNoWindow,      // 不给子进程弹控制台窗口（本程序是 GUI 子系统）
        nullptr, nullptr, &startup, &process);

    // 无论成功与否，父进程都必须立刻关掉自己那份写端，否则第 3 点说的死锁就来了。
    ::CloseHandle(write_end);

    if (!started) {
        ::CloseHandle(read_end);
        return result;
    }

    std::string captured;
    char buffer[4096];
    for (;;) {
        DWORD available = 0;
        if (!::ReadFile(read_end, buffer, sizeof(buffer), &available, nullptr) ||
            available == 0) {
            break;   // 0 字节 = 所有写端已关闭，EOF
        }
        captured.append(buffer, available);
    }
    ::CloseHandle(read_end);

    ::WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    ::GetExitCodeProcess(process.hProcess, &exit_code);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);

    result.status = static_cast<int>(exit_code);
    result.output = std::move(captured);
    return result;
}

/// 跑 PowerShell 脚本。脚本里有单引号包起来的路径，所以统一走 -Command，
/// 由 PowerShell 自己解析（路径里的单引号由 ps_quote 负责转义）。
CommandResult run_powershell(const std::string& script) {
    return run_command("powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass "
                       "-Command \"" +
                       script + "\"");
}

/// 把一段文本包成 PowerShell 单引号字符串（内部的单引号写两遍）。
std::string ps_quote(const std::string& text) {
    std::string out = "'";
    for (char ch : text) {
        if (ch == '\'') out += "''";
        else out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}

/// 当前进程是否已经是管理员。
bool is_elevated() {
    // IsUserAnAdmin 是 shlobj 里的老 API，对新式 UAC（比如受限制的令牌）判断
    // 不够准；这里照 Python 版的 ctypes.windll.shell32.IsUserAnAdmin 走，
    // 行为保持一致。
    return ::IsUserAnAdmin() != FALSE;
}

// ── 载荷 ──────────────────────────────────────────────────────

/// exe 旁边（安装目录）有没有现成的 msix/cer。安装包会把它们放在那儿。
bool sibling_path(const char* name, std::string& out) {
    const std::string directory = executable_dir();
    const std::string candidate = path_join(directory, name);
    if (is_regular_file(candidate)) {
        out = candidate;
        return true;
    }
    return false;
}

/// 临时目录：%TEMP%\pdf_compress_menu_<pid>。用完整个删掉。
std::string make_temp_dir() {
    wchar_t buffer[MAX_PATH + 1] = {};
    DWORD length = ::GetTempPathW(MAX_PATH, buffer);
    std::string base = length > 0 ? wide_to_utf8(buffer) : std::string(".");
    return path_join(base, "pdf_compress_menu_" + std::to_string(::GetCurrentProcessId()));
}

/// 递归删目录。载荷是刚刚由我们自己写出去的，路径可控，用不上更复杂的清理。
void remove_tree(const std::string& path) {
    const std::wstring wide = utf8_to_wide(path);
    ::SHFILEOPSTRUCTW operation{};
    // SHFileOperationW 要求双 \0 结尾的路径列表
    std::wstring from = wide;
    from.push_back(L'\0');
    from.push_back(L'\0');
    operation.wFunc = FO_DELETE;
    operation.pFrom = from.c_str();
    operation.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    ::SHFileOperationW(&operation);
}

/// 把 msix 和 cer 准备到磁盘上。
///
/// 优先用 exe 旁边那两个文件（安装包装出来的形态就是这样），否则用嵌进 exe 的
/// 十六进制载荷。两者都没有 → 这份构建没带右键菜单。
bool materialize_payload(const std::string& target_dir, std::string& msix_path,
                         std::string& cer_path) {
    if (sibling_path(kShellMsixName, msix_path) && sibling_path(kShellCerName, cer_path)) {
        return true;
    }

    std::string msix_bytes;
    std::string cer_bytes;
    if (!hex_decode(kShellMsixHex, msix_bytes) || !hex_decode(kShellCerHex, cer_bytes) ||
        msix_bytes.empty() || cer_bytes.empty()) {
        return false;
    }
    if (!make_directories(target_dir)) return false;

    msix_path = path_join(target_dir, kShellMsixName);
    cer_path = path_join(target_dir, kShellCerName);
    if (!write_binary(msix_path, msix_bytes)) return false;
    if (!write_binary(cer_path, cer_bytes)) return false;
    return true;
}

// ── 证书 ──────────────────────────────────────────────────────

/// 证书是否已经在"受信任人"里。
///
/// certutil 的输出跟着系统代码页走：中文系统上是 GBK，英文系统上是 CP1252，
/// 而且可能是 UTF-16。所以这里**按字节**找主题名，不做任何编码转换——
/// "CN=zyq1223334444" 全是 ASCII，在 GBK/CP1252/UTF-8 下的字节完全一样，
/// 只有 UTF-16 会插 \0，所以顺手把 \0 去掉再找。
bool cert_is_trusted() {
    const CommandResult result = run_command("certutil.exe -store TrustedPeople");
    if (result.status != 0) return false;
    std::string text;
    text.reserve(result.output.size());
    for (char ch : result.output) {
        if (ch != '\0') text.push_back(ch);
    }
    return text.find(kPublisher) != std::string::npos;
}

bool trust_cert(const std::string& cer_path) {
    CommandResult result = run_command("certutil.exe -addstore -f TrustedPeople \"" + cer_path + "\"");
    if (result.status == 0) return true;
    // certutil 失败时再用 PowerShell 试一次，两者的失败原因不完全一样
    result = run_powershell("Import-Certificate -FilePath " + ps_quote(cer_path) +
                            " -CertStoreLocation Cert:\\LocalMachine\\TrustedPeople | Out-Null");
    return result.status == 0;
}

void untrust_cert() {
    if (!is_elevated()) return;
    run_powershell("Get-ChildItem Cert:\\LocalMachine\\TrustedPeople | Where-Object { $_.Subject -eq " +
                   ps_quote(kPublisher) + " } | Remove-Item -Force");
}

// ── MSIX ──────────────────────────────────────────────────────

/// 这个包有没有为当前用户注册过。
bool package_registered() {
    const CommandResult result =
        run_powershell("(Get-AppxPackage " + std::string(kPackageName) + ") -ne $null");
    return result.status == 0 &&
           result.output.find("True") != std::string::npos;
}

void remove_package() {
    run_powershell("Get-AppxPackage " + std::string(kPackageName) +
                   " | Remove-AppxPackage -ErrorAction SilentlyContinue");
    // 卸载程序是以管理员身份跑的，拿不到"启动安装的那个用户"是谁，所以再做一次
    // 全用户清理，免得标准用户装的包留在机器上（这一步需要管理员，失败就算了）
    if (is_elevated()) {
        run_powershell(std::string("Get-AppxPackage -AllUsers ") + kPackageName +
                       " | Remove-AppxPackage -AllUsers -ErrorAction SilentlyContinue");
    }
    run_powershell("Get-AppxProvisionedPackage -Online | Where-Object { $_.DisplayName -eq " +
                   ps_quote(kPackageName) +
                   " } | Remove-AppxProvisionedPackage -Online -ErrorAction SilentlyContinue");
}

bool add_package(const std::string& msix_path, const std::string& external_dir,
                 std::string& output) {
    // 同一个版本重复注册会失败（0x80073CF9），先摘掉再装
    remove_package();
    const CommandResult result =
        run_powershell("Add-AppxPackage -Path " + ps_quote(msix_path) + " -ExternalLocation " +
                       ps_quote(external_dir) + " -ErrorAction Stop");
    output = result.output;
    return result.status == 0;
}

// ── 传统注册项 ────────────────────────────────────────────────

/// 把 HKCU 下的传统右键菜单项写好。返回是否成功。
bool register_classic(const std::string& exe_path) {
    // 菜单标题跟着界面语言走（与 Python 版一样：英文标题只在英文界面下出现）。
    const char* title = (language() == Language::En) ? "Compress with PDF Compressor"
                                                     : "用 PDF Compressor 压缩";

    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, kVerbKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key,
                          nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const auto write_string = [&key](const wchar_t* name, const std::string& value) {
        const std::wstring wide = utf8_to_wide(value);
        return ::RegSetValueExW(key, name, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(wide.c_str()),
                                static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t))) ==
               ERROR_SUCCESS;
    };

    bool ok = write_string(nullptr, title);
    // Icon：菜单里那一项左边的小图标，取 exe 自己的第 0 号图标
    ok = write_string(L"Icon", exe_path + ",0") && ok;
    // Player：选中多个文件时只调用一次，把所有路径一起传进来。
    // （默认的 Document 模型会对每个文件各调一次。）
    ok = write_string(L"MultiSelectModel", "Player") && ok;
    ::RegCloseKey(key);

    HKEY command = nullptr;
    const std::wstring command_key = std::wstring(kVerbKey) + L"\\command";
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, command_key.c_str(), 0, nullptr, 0, KEY_WRITE,
                          nullptr, &command, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const std::wstring command_line = L"\"" + utf8_to_wide(exe_path) + L"\" \"%1\"";
    ok = ::RegSetValueExW(command, nullptr, 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(command_line.c_str()),
                          static_cast<DWORD>((command_line.size() + 1) * sizeof(wchar_t))) ==
             ERROR_SUCCESS &&
         ok;
    ::RegCloseKey(command);
    return ok;
}

void unregister_classic() {
    const std::wstring command_key = std::wstring(kVerbKey) + L"\\command";
    ::RegDeleteKeyW(HKEY_CURRENT_USER, command_key.c_str());
    ::RegDeleteKeyW(HKEY_CURRENT_USER, kVerbKey);
}

// ── 提权 ──────────────────────────────────────────────────────

/// 以管理员身份重新启动自己，并把子进程的退出码带回来。
///
/// 返回 -1 表示没能提权（用户拒绝了 UAC），否则返回子进程退出码。
int relaunch_elevated() {
    const std::string exe_path = path_join(executable_dir(), kExeName);
    if (!is_regular_file(exe_path)) return -1;

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    // SEE_MASK_NOCLOSEPROCESS：把新进程的句柄交给我们，才能等它、取它的退出码
    // SEE_MASK_NOASYNC：调用线程在返回前一直有效，避免 Shell 在别的线程里
    //                  还引用我们的字符串
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    const std::wstring wide_exe = utf8_to_wide(exe_path);
    info.lpFile = wide_exe.c_str();
    info.lpParameters = L"--install-menu";
    const std::wstring wide_dir = utf8_to_wide(executable_dir());
    info.lpDirectory = wide_dir.c_str();
    info.nShow = SW_SHOWNORMAL;

    if (!::ShellExecuteExW(&info)) return -1;   // 用户拒绝 UAC 时就是这里失败
    if (info.hProcess == nullptr) return -1;

    ::WaitForSingleObject(info.hProcess, INFINITE);
    DWORD exit_code = 1;
    ::GetExitCodeProcess(info.hProcess, &exit_code);
    ::CloseHandle(info.hProcess);
    return static_cast<int>(exit_code);
}

#endif  // _WIN32

/// 这一步的输出去哪：quiet 时什么都不打印（安装包调用时用）。
void say(bool quiet, const std::string& message) {
    if (!quiet) emit(message);
}

}  // namespace

// ── 对外接口 ──────────────────────────────────────────────────

int install_shell_menu(bool quiet) {
#if !defined(_WIN32)
    say(quiet, tr("[失败] 右键菜单只有 Windows 支持。"));
    return kExitFail;
#else
    const std::string external_dir = executable_dir();
    std::string exe_path = path_join(external_dir, kExeName);
    if (!is_regular_file(exe_path)) {
        // 免安装版的文件名可能是 pdf_compress 或 pdf_compress.exe，找找看。
        const std::vector<std::string> candidates = {"pdf_compress", "pdf_compress.exe"};
        bool found = false;
        for (const std::string& name : candidates) {
            const std::string candidate = path_join(external_dir, name);
            if (is_regular_file(candidate)) {
                exe_path = candidate;
                found = true;
                break;
            }
        }
        if (!found) {
            say(quiet, tr("[失败] 在 {dir} 里找不到 {exe}，无法安装右键菜单。",
                          {{"dir", external_dir}, {"exe", kExeName}}));
            return kExitFail;
        }
    }

    const std::string temp_dir = make_temp_dir();
    bool package_ok = false;
    bool classic_ok = false;
    {
        // 用一个作用域保证临时目录在任何返回路径上都被删掉（对应 Python 的 finally）。
        struct TempCleanup {
            const std::string& path;
            ~TempCleanup() { remove_tree(path); }
        } cleanup{temp_dir};

        say(quiet, tr("[1/4] 准备右键菜单组件…"));
        std::string msix_path;
        std::string cer_path;
        if (!materialize_payload(temp_dir, msix_path, cer_path)) {
            say(quiet, tr("[失败] 找不到右键菜单组件（{msix}）。这份构建没有带右键菜单。",
                          {{"msix", kShellMsixName}}));
            return kExitFail;
        }

        // 证书只需要装一次，装过就不必再提权
        if (!cert_is_trusted()) {
            if (is_elevated()) {
                say(quiet, tr("[2/4] 把自签证书加入“受信任人”证书库…"));
                if (!trust_cert(cer_path)) {
                    say(quiet, tr("[失败] 无法把证书加入“受信任人”证书库。"));
                    return kExitFail;
                }
            } else if (quiet) {
                say(quiet,
                    tr("[失败] 证书尚未受信任，需要管理员权限；请使用安装包安装，"
                       "或以管理员身份运行 pdf_compress --install-menu。"));
                return kExitFail;
            } else {
                say(quiet,
                    tr("[提示] 首次安装需要管理员权限（只用于把自签证书加入"
                       "“受信任人”证书库），正在请求提权…"));
                const int code = relaunch_elevated();
                // 提权失败（用户拒绝 UAC）或子进程的退出码，原样带回去。
                return code < 0 ? kExitFail : code;
            }
        } else {
            say(quiet, tr("[2/4] 证书已受信任，跳过。"));
        }

        say(quiet, tr("[3/4] 注册 Windows 11 一级右键菜单…"));
        std::string output;
        package_ok = add_package(msix_path, external_dir, output);
        if (!package_ok) {
            say(quiet, tr("[警告] 一级右键菜单注册失败：{error}", {{"error", trim(output)}}));
            say(quiet,
                tr("[提示] Windows 10（1809 之前）不支持稀疏包，"
                   "下面注册的传统右键菜单项仍然可用。"));
        }
    }

    say(quiet, tr("[4/4] 注册传统右键菜单项（Windows 10 与“显示更多选项”）…"));
    classic_ok = register_classic(exe_path);
    if (!classic_ok) {
        say(quiet, tr("[警告] 传统右键菜单项写入失败（注册表不可写）。"));
    }

    // 便携版自己记住安装目录与语言，不需要管理员权限（安装包写的是 HKLM）。
    write_user_registry("InstallDir", external_dir);
    write_user_registry("Language", language_code(language()));

    if (package_ok || classic_ok) {
        say(quiet, tr("[完成] 右键 .pdf 文件即可看到“用 PDF Compressor 压缩”。"));
        if (!quiet) {
            say(quiet, tr("[提示] 若菜单没有立刻出现，重启资源管理器或重新登录即可。"));
        }
        return kExitOk;
    }
    say(quiet, tr("[失败] 右键菜单没有注册成功。"));
    return kExitFail;
#endif
}

int uninstall_shell_menu(bool quiet) {
#if !defined(_WIN32)
    say(quiet, tr("[失败] 右键菜单只有 Windows 支持。"));
    return kExitFail;
#else
    say(quiet, tr("[1/3] 注销 Windows 11 一级右键菜单…"));
    remove_package();

    say(quiet, tr("[2/3] 删除传统右键菜单项…"));
    unregister_classic();
    delete_user_registry_key();

    say(quiet, tr("[3/3] 清理“受信任人”证书库…"));
    untrust_cert();

    say(quiet, tr("[完成] 右键菜单已移除。"));
    if (!quiet) {
        say(quiet, tr("[提示] 若菜单项仍然可见，重新登录一次即可。"));
    }
    return kExitOk;
#endif
}

}  // namespace pdfc
