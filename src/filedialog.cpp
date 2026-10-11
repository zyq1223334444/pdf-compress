// -*- coding: utf-8 -*-
//
// 原生文件选择对话框，三个平台各一份实现（见 filedialog.hpp 的说明）。
//
// 这一份刻意"又臭又长"：把一个文件对话框做得跟系统自己的一模一样，本来就要
// 处理这些平台差异。三条路各有各的坑，注释里都写清楚了。

#include "filedialog.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "common.hpp"
#include "i18n.hpp"

#if defined(_WIN32)
// ── Windows ───────────────────────────────────────────────────
// 只用 COM：IFileOpenDialog 就是资源管理器"打开/另存为"里那个新式对话框。
// 刻意不用 comdlg32 的 GetOpenFileName：那是 Windows 95 时代的外观，
// 而且 32 位长度限制、编码转换一堆历史包袱。
#  include <windows.h>
#  include <shobjidl.h>      // IFileOpenDialog / IShellItem
#  include <shlobj.h>        // SHCreateItemFromParsingName
#  include <objbase.h>
#else
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace pdfc {

#if defined(_WIN32)

namespace {

// 一个极小的 COM 智能指针。这个文件只用到四五个接口，引一个 ATL/WRL 都不划算。
// 只实现这里需要的操作（取地址、->、*、move、release），不做复制。
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    /// 给 out 参数的取地址用，例如 dialog->GetResult(&item)。
    T** put() {
        reset();
        return &ptr_;
    }

    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    void reset() {
        if (ptr_ != nullptr) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

    /// 交出所有权（用在需要把指针转交给别处的时候）。
    T* detach() {
        T* value = ptr_;
        ptr_ = nullptr;
        return value;
    }

private:
    T* ptr_ = nullptr;
};

/// CoTaskMemFree 的 RAII：COM 返回的宽字符串一律是它分配的。
class CoTaskString {
public:
    CoTaskString() = default;
    ~CoTaskString() {
        if (ptr_ != nullptr) ::CoTaskMemFree(ptr_);
    }
    CoTaskString(const CoTaskString&) = delete;
    CoTaskString& operator=(const CoTaskString&) = delete;

    LPWSTR* put() { return &ptr_; }
    const wchar_t* get() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

private:
    LPWSTR ptr_ = nullptr;
};

/// 保证这个线程上的 COM 被初始化过，并且退出时配对地反初始化。
/// 已经初始化过（返回 RPC_E_CHANGED_MODE 之外的成功码）就不重复初始化、
/// 也不重复反初始化——多减一次引用会把别人的 apartment 拆掉。
class ComScope {
public:
    ComScope() {
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                         COINIT_DISABLE_OLE1DDE);
        // 图形界面线程已经初始化过时会返回 S_FALSE，同样是成功
        owns_ = SUCCEEDED(hr);
    }
    ~ComScope() {
        if (owns_) ::CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    bool owns_ = false;
};

/// 从 UTF-8 目录名建一个 IShellItem。建不出来（目录不存在、路径非法）就返回空，
/// 调用方跳过 SetFolder——对话框会落在它自己的默认位置，而不是失败。
bool make_shell_item(const std::string& path, IShellItem** out) {
    if (path.empty()) return false;
    const std::wstring wide = utf8_to_wide(path);
    return SUCCEEDED(::SHCreateItemFromParsingName(wide.c_str(), nullptr,
                                                   IID_PPV_ARGS(out)));
}

/// IShellItem -> UTF-8 文件系统路径。非文件系统的项（库、网络位置）取不到
/// FILESYSPATH，这时返回空串，由调用方丢掉这一项。
std::string shell_item_path(IShellItem* item) {
    if (item == nullptr) return {};
    CoTaskString path;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, path.put()))) return {};
    if (!path) return {};
    return wide_to_utf8(path.get());
}

/// 两项过滤器：PDF / 所有文件，都经过 tr()。
///
/// SetFileTypes 只保存指针、不复制字符串，所以这两份宽字符串必须活到 Show 返回
/// 之后。因此由调用方（choose_files）持有 owner，这里只返回指向 owner 元素的
/// 过滤器数组——返回临时 wstring 的 c_str() 是一个很容易犯、又很难查的错误。
std::vector<COMDLG_FILTERSPEC> prepare_filter_specs(std::vector<std::wstring>& owner) {
    owner.clear();
    owner.emplace_back(utf8_to_wide(tr("PDF 文件")));
    owner.emplace_back(utf8_to_wide(tr("所有文件")));
    std::vector<COMDLG_FILTERSPEC> specs;
    specs.push_back(COMDLG_FILTERSPEC{owner[0].c_str(), L"*.pdf"});
    specs.push_back(COMDLG_FILTERSPEC{owner[1].c_str(), L"*.*"});
    return specs;
}

}  // namespace

std::vector<std::string> choose_files(void* owner, const std::string& initial_dir) {
    std::vector<std::string> result;
    ComScope com;

    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(dialog.put())))) {
        return result;
    }

    DWORD options = 0;
    if (FAILED(dialog->GetOptions(&options))) return result;
    options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_FILEMUSTEXIST |
               FOS_PATHMUSTEXIST | FOS_ALLOWMULTISELECT;
    if (FAILED(dialog->SetOptions(options))) return result;

    dialog->SetTitle(utf8_to_wide(tr("选择 PDF 文件")).c_str());

    // 过滤器字符串必须活到 Show 之后，所以由这里持有。
    std::vector<std::wstring> filter_owner;
    std::vector<COMDLG_FILTERSPEC> filters = prepare_filter_specs(filter_owner);
    dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());
    // 默认选中第一项（PDF），否则用户看到的是"所有文件"
    dialog->SetFileTypeIndex(1);

    ComPtr<IShellItem> folder;
    if (make_shell_item(initial_dir, folder.put())) {
        dialog->SetFolder(folder.get());
    }

    if (FAILED(dialog->Show(reinterpret_cast<HWND>(owner)))) {
        // 取消（HRESULT_FROM_WIN32(ERROR_CANCELLED)）和真正的失败在这里都一样：
        // 没有选中任何文件，调用方按空列表处理。
        return result;
    }

    ComPtr<IShellItemArray> items;
    if (FAILED(dialog->GetResults(items.put()))) return result;

    DWORD count = 0;
    if (FAILED(items->GetCount(&count))) return result;
    for (DWORD index = 0; index < count; ++index) {
        ComPtr<IShellItem> item;
        if (FAILED(items->GetItemAt(index, item.put()))) continue;
        std::string path = shell_item_path(item.get());
        if (!path.empty()) result.push_back(path);
    }
    return result;
}

std::string choose_folder(void* owner, const std::string& initial_dir) {
    ComScope com;

    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(dialog.put())))) {
        return {};
    }

    DWORD options = 0;
    if (FAILED(dialog->GetOptions(&options))) return {};
    // 只选文件夹：不要 ALLOWMULTISELECT（选一个就够），也不要 FILEMUSTEXIST。
    options |= FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
    if (FAILED(dialog->SetOptions(options))) return {};

    dialog->SetTitle(utf8_to_wide(tr("选择输出目录")).c_str());

    ComPtr<IShellItem> folder;
    if (make_shell_item(initial_dir, folder.put())) {
        dialog->SetFolder(folder.get());
    }

    if (FAILED(dialog->Show(reinterpret_cast<HWND>(owner)))) return {};

    ComPtr<IShellItem> chosen;
    if (FAILED(dialog->GetResult(chosen.put()))) return {};
    return shell_item_path(chosen.get());
}

#else  // ── macOS / Linux ────────────────────────────────────────

namespace {

/// 给 shell 拼参数用的引号。
///
/// 文件名里可能有空格、引号、$、中文——直接拼进命令行会被 shell 拆开或者当变量
/// 展开。这里统一用单引号包住，并把内部的单引号写成 '\''（关引号、转义单引号、
/// 再开引号）。单引号里 $、`、\、" 都不再有特殊含义，这是最省心的写法。
///
/// 提醒：不要在这上面省事去用双引号 + 反斜杠转义，`$` 和反引号在双引号里仍然
/// 会被展开，用户取个带 $ 的文件名就会中招。
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

/// 这个外部程序在不在 PATH 上。用 `command -v` 而不是 `which`：后者在某些
/// 精简发行版/容器里根本没装，而 command 是 shell 内建，一定有。
///
/// 只有 Linux 分支用得到（它要挑 zenity/kdialog）；macOS 分支走 osascript，
/// 于是在那边就是"定义了没人调"，clang 会报 -Wunused-function。
[[maybe_unused]] bool command_exists(const std::string& name) {
    const std::string line = "command -v " + shell_quote(name) + " >/dev/null 2>&1";
    const int status = std::system(line.c_str());
    return status == 0;
}

struct Capture {
    int status = -1;
    std::string output;
};

/// popen 跑一条命令并把输出整段读回来。
///
/// 注意这里传的是给 shell 的整条命令，所以调用方必须自己用 shell_quote 拼好每个
/// 参数；这个函数不做任何转义。
Capture run_capture(const std::string& command) {
    Capture capture;
    std::FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) return capture;
    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        capture.output += buffer;
    }
    const int status = ::pclose(pipe);
    if (status != -1 && WIFEXITED(status)) {
        capture.status = WEXITSTATUS(status);
    } else {
        capture.status = -1;
    }
    return capture;
}

/// 把整段输出按行切开，去掉空行和首尾空白（zenity 会带 \r，kdialog 会带空行）。
std::vector<std::string> output_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (char ch : text) {
        if (ch == '\n' || ch == '\r') {
            const std::string trimmed = trim(current);
            if (!trimmed.empty()) lines.push_back(trimmed);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    const std::string trimmed = trim(current);
    if (!trimmed.empty()) lines.push_back(trimmed);
    return lines;
}

/// zenity 的过滤串写法是"名字 | 通配符"。同样只有 Linux 分支用得到。
[[maybe_unused]] std::string pdf_filter_zenity() {
    return std::string(tr("PDF 文件")) + " | *.pdf";
}  // namespace

std::vector<std::string> choose_files(void* owner, const std::string& initial_dir) {
    static_cast<void>(owner);   // 这三种对话框都不接受父窗口句柄
    std::vector<std::string> result;

#if defined(__APPLE__)
    // macOS 用系统自带的 osascript。Tk 版用的是 NSOpenPanel，这里没有 Tk，
    // 而 osascript 是唯一"不需要额外依赖、又是系统原生面板"的路子。
    // 一个小细节：AppleScript 返回的是 HFS 风格的 alias 文本（冒号分隔），
    // 需要手工换成 POSIX 路径。
    const std::string prompt = tr("选择 PDF 文件");
    const std::string script =
        "set theFiles to choose file with prompt " + shell_quote(prompt) +
        " of type {\"pdf\", \"PDF\"} with multiple selections allowed\n"
        "set out to \"\"\n"
        "repeat with f in theFiles\n"
        "  set out to out & POSIX path of f & linefeed\n"
        "end repeat\n"
        "return out";
    const Capture capture = run_capture("osascript -e " + shell_quote(script) + " 2>/dev/null");
    if (capture.status != 0) return result;
    // POSIX path of 已经给出 "/Users/..."，所以这里不用做 : -> / 的转换；
    // 万一系统返回的是 alias 文本（"alias Macintosh HD:Users:..."），下面的
    // 兜底分支会把它转过来。
    for (std::string line : output_lines(capture.output)) {
        if (starts_with(line, "alias ")) {
            line = line.substr(6);
            // 去掉卷名之前的开头（HFS 路径从冒号开始）
            std::string converted;
            for (char ch : line) converted.push_back(ch == ':' ? '/' : ch);
            if (!converted.empty() && converted.front() == '/') {
                const std::size_t slash = converted.find('/', 1);
                if (slash != std::string::npos) converted = converted.substr(slash);
            }
            line = converted;
        }
        if (!line.empty()) result.push_back(line);
    }
    return result;
#else
    // Linux：优先 zenity（GNOME 系，几乎都能装上），其次 kdialog（KDE）。
    // 两个都没有时返回空列表——调用方（GUI）自己要能处理这种情况。
    if (command_exists("zenity")) {
        std::string command = "zenity --file-selection --multiple --separator=\"\\n\"";
        command += " --title=" + shell_quote(tr("选择 PDF 文件"));
        command += " --file-filter=" + shell_quote(pdf_filter_zenity());
        if (!initial_dir.empty()) {
            command += " --filename=" + shell_quote(path_join(initial_dir, ""));
        }
        command += " 2>/dev/null";
        const Capture capture = run_capture(command);
        if (capture.status != 0) return result;   // 1 = 用户取消
        return output_lines(capture.output);
    }

    if (command_exists("kdialog")) {
        // kdialog 的过滤器写法是 "名字|通配符"，多选要把 --multiple --separate 一起给，
        // 否则它只返回一个路径。
        std::string command = "kdialog --getopenfilename";
        command += " " + shell_quote(initial_dir.empty() ? std::string(".") : initial_dir);
        command += " " + shell_quote(std::string(tr("PDF 文件")) + "|*.pdf");
        command += " --multiple --separate 2>/dev/null";
        const Capture capture = run_capture(command);
        if (capture.status != 0) return result;
        return output_lines(capture.output);
    }

    return result;   // 两者都没有：调用方自己处理"没有可用对话框"
#endif
}

std::string choose_folder(void* owner, const std::string& initial_dir) {
    static_cast<void>(owner);

#if defined(__APPLE__)
    const std::string script =
        "set theFolder to choose folder with prompt " + shell_quote(tr("选择输出目录")) + "\n"
        "return POSIX path of theFolder";
    const Capture capture = run_capture("osascript -e " + shell_quote(script) + " 2>/dev/null");
    if (capture.status != 0) return {};
    const std::vector<std::string> lines = output_lines(capture.output);
    return lines.empty() ? std::string() : lines.front();
#else
    if (command_exists("zenity")) {
        std::string command = "zenity --file-selection --directory";
        command += " --title=" + shell_quote(tr("选择输出目录"));
        if (!initial_dir.empty()) {
            command += " --filename=" + shell_quote(path_join(initial_dir, ""));
        }
        command += " 2>/dev/null";
        const Capture capture = run_capture(command);
        if (capture.status != 0) return {};
        const std::vector<std::string> lines = output_lines(capture.output);
        return lines.empty() ? std::string() : lines.front();
    }

    if (command_exists("kdialog")) {
        // kdialog 的 --getexistingdirectory 必须给一个起始目录，给不了就给 "."。
        std::string command = "kdialog --getexistingdirectory";
        command += " " + shell_quote(initial_dir.empty() ? std::string(".") : initial_dir);
        command += " 2>/dev/null";
        const Capture capture = run_capture(command);
        if (capture.status != 0) return {};
        const std::vector<std::string> lines = output_lines(capture.output);
        if (lines.empty()) return {};
        // kdialog 返回的目录名带结尾斜杠，去掉让下游的路径拼接干净一点
        std::string folder = lines.front();
        while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
        return folder;
    }

    return {};
#endif
}

#endif

}  // namespace pdfc
