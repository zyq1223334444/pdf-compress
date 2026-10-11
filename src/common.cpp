// -*- coding: utf-8 -*-
//
// 公共类型与小工具的实现。

#include "common.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#  include <io.h>
#  include <windows.h>
#  include <direct.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <sys/sysctl.h>
#  endif
#endif

namespace pdfc {

// ── 单位换算 ──────────────────────────────────────────────────

std::string human_size(double bytes) {
    // 单位阶梯与 Python 版一致：B 不带小数，其余保留两位。
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = bytes;
    for (int index = 0; index < 5; ++index) {
        const bool last = index == 4;
        if (std::fabs(value) < 1024.0 || last) {
            char buffer[64];
            if (index == 0) {
                std::snprintf(buffer, sizeof(buffer), "%.0f %s", value, units[index]);
            } else {
                std::snprintf(buffer, sizeof(buffer), "%.2f %s", value, units[index]);
            }
            return buffer;
        }
        value /= 1024.0;
    }
    return "0 B";
}

double to_mb(double bytes) { return bytes / kMebibyte; }

std::int64_t mb_to_bytes(double mb) {
    return static_cast<std::int64_t>(std::llround(mb * kMebibyte));
}

double dpi_to_scale(double dpi) { return dpi / kDpiBase; }
double scale_to_dpi(double scale) { return scale * kDpiBase; }

// ── 字符串工具 ────────────────────────────────────────────────

std::string format(std::string text,
                   const std::vector<std::pair<std::string, std::string>>& values) {
    for (const auto& [name, value] : values) {
        std::string needle = "{" + name;
        for (std::size_t at = text.find(needle); at != std::string::npos;
             at = text.find(needle, at)) {
            // 占位符可能带格式说明符：{size:.2f}、{ratio:+.1%}，一并吃掉。
            // 找不到右花括号就说明这只是一个普通左花括号，跳过。
            const std::size_t close = text.find('}', at + needle.size());
            if (close == std::string::npos) break;
            const std::size_t between = close - (at + needle.size());
            if (between > 0 && text[at + needle.size()] != ':' && text[at + needle.size()] != '}') {
                // {sizefoo...} 这种只是前缀相同的别的词，不是我们的占位符
                break;
            }
            text.replace(at, close - at + 1, value);
            at += value.size();
        }
    }
    return text;
}

std::string fmt_fixed(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

std::string fmt_percent(double ratio, int decimals) {
    char buffer[64];
    const double percent = ratio * 100.0;
    // Python 的 {:+.1%} 对正数带加号、对负数带减号
    std::snprintf(buffer, sizeof(buffer), "%+.*f%%", decimals, percent);
    return buffer;
}

std::string fmt_general(double value) {
    char buffer[64];
    // %g 会把 7.2 打成 7.2、600 打成 600，正好是命令行参数的阅读习惯
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    return buffer;
}

std::string to_lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::string trim(const std::string& text) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    auto begin = std::find_if(text.begin(), text.end(), not_space);
    auto end = std::find_if(text.rbegin(), text.rend(), not_space).base();
    if (begin >= end) return {};
    return std::string(begin, end);
}

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : text) {
        if (ch == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    parts.push_back(current);
    return parts;
}

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() &&
           std::memcmp(text.data(), prefix.data(), prefix.size()) == 0;
}

// ── 路径 ──────────────────────────────────────────────────────

static bool is_separator(char ch) { return ch == '/' || ch == '\\'; }

/// 拼接路径时用的分隔符。
///
/// Windows 上必须用反斜杠：这些路径会原样打进消息给用户看，还会写进渲染子进程
/// 的规格文件。用 '/' 虽然系统认，但和用户自己给的路径（"D:\a\b.pdf"）混在一起
/// 就成了 "D:\a\b/c_compressed.pdf" 这种四不像；Python 版的 os.path.join 在
/// Windows 上也是反斜杠，两边输出才对得上。
#if defined(_WIN32)
constexpr char kSeparator = '\\';
#else
constexpr char kSeparator = '/';
#endif

std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (is_separator(b.front())) return b;
    std::string result = a;
    if (!is_separator(result.back())) result.push_back(kSeparator);
    result += b;
    return result;
}

std::string path_basename(const std::string& path) {
    const std::size_t at = path.find_last_of("/\\");
    return at == std::string::npos ? path : path.substr(at + 1);
}

std::string path_dirname(const std::string& path) {
    const std::size_t at = path.find_last_of("/\\");
    if (at == std::string::npos) return {};
    if (at == 0) return path.substr(0, 1);
    return path.substr(0, at);
}

std::string path_stem(const std::string& path) {
    std::string name = path_basename(path);
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return name;
    return name.substr(0, dot);
}

bool file_exists(const std::string& path) {
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(utf8_to_wide(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES;
#else
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0;
#endif
}

bool is_regular_file(const std::string& path) {
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(utf8_to_wide(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
#endif
}

bool is_directory(const std::string& path) {
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(utf8_to_wide(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

std::int64_t file_size(const std::string& path) {
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(utf8_to_wide(path).c_str(), GetFileExInfoStandard, &data)) {
        return 0;
    }
    LARGE_INTEGER size{};
    size.HighPart = static_cast<LONG>(data.nFileSizeHigh);
    size.LowPart = data.nFileSizeLow;
    return size.QuadPart;
#else
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0) return 0;
    return info.st_size;
#endif
}

bool make_directories(const std::string& path) {
    if (path.empty() || is_directory(path)) return true;
    std::string current;
    std::size_t index = 0;
    // 逐级建，跳过盘符（C:）与开头的分隔符
    if (path.size() >= 2 && path[1] == ':') {
        current = path.substr(0, 2);
        index = 2;
    } else if (is_separator(path.front())) {
        current = path.substr(0, 1);
        ++index;
    }
    while (index <= path.size()) {
        const std::size_t next = path.find_first_of("/\\", index);
        const std::size_t end = next == std::string::npos ? path.size() : next;
        if (end > index) {
            if (!current.empty() && !is_separator(current.back())) current.push_back(kSeparator);
            current += path.substr(index, end - index);
#if defined(_WIN32)
            _wmkdir(utf8_to_wide(current).c_str());
#else
            ::mkdir(current.c_str(), 0755);
#endif
        }
        if (next == std::string::npos) break;
        index = next + 1;
    }
    return is_directory(path);
}

// ── 系统信息 ──────────────────────────────────────────────────

int physical_cpu_count() {
#if defined(_WIN32)
    // GetLogicalProcessorInformationEx 里数 RelationProcessorCore，
    // 只认物理核：含超线程的逻辑核数在渲染任务上反而更慢。
    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
    if (length != 0) {
        std::vector<char> buffer(length);
        if (GetLogicalProcessorInformationEx(
                RelationProcessorCore,
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
                &length)) {
            int cores = 0;
            DWORD offset = 0;
            while (offset + sizeof(DWORD) * 2 <= length) {
                const auto* entry =
                    reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
                        buffer.data() + offset);
                if (entry->Size < sizeof(DWORD) * 2) break;
                ++cores;
                offset += entry->Size;
            }
            if (cores > 0) return cores;
        }
    }
#elif defined(__APPLE__)
    int cores = 0;
    std::size_t size = sizeof(cores);
    if (sysctlbyname("hw.physicalcpu", &cores, &size, nullptr, 0) == 0 && cores > 0) {
        return cores;
    }
#else
    // /proc/cpuinfo 里 (physical id, core id) 的唯一组合数
    if (FILE* handle = std::fopen("/proc/cpuinfo", "r")) {
        std::vector<std::pair<std::string, std::string>> pairs;
        char line[512];
        std::string physical, core;
        auto flush = [&]() {
            if (!physical.empty() && !core.empty()) {
                pairs.emplace_back(physical, core);
            }
            physical.clear();
            core.clear();
        };
        while (std::fgets(line, sizeof(line), handle)) {
            std::string text = trim(line);
            if (text.empty()) {
                flush();
                continue;
            }
            const std::size_t colon = text.find(':');
            if (colon == std::string::npos) continue;
            const std::string key = trim(text.substr(0, colon));
            const std::string value = trim(text.substr(colon + 1));
            if (key == "physical id") physical = value;
            else if (key == "core id") core = value;
        }
        flush();
        std::fclose(handle);
        std::sort(pairs.begin(), pairs.end());
        pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
        if (!pairs.empty()) return static_cast<int>(pairs.size());
    }
#endif
    // 都拿不到时退回"逻辑核数 ÷ 2"这个老启发式
    const unsigned logical = std::thread::hardware_concurrency();
    return std::max(1, static_cast<int>(logical) / 2);
}

double monotonic_seconds() {
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

bool looks_like_pdf(const std::string& path) {
    const std::string name = to_lower(path_basename(path));
    return name.size() > 4 && name.compare(name.size() - 4, 4, ".pdf") == 0;
}

// ── 标准输出 ──────────────────────────────────────────────────

void emit(const std::string& line) {
    // 输出可能被重定向到一个已经关闭的管道，写失败不该让整轮压缩崩掉
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void emit_line() { emit(std::string()); }

}  // namespace pdfc
