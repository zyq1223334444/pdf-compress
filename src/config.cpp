// -*- coding: utf-8 -*-
//
// 用户设置与安装信息。
//
// settings.ini 是极简的 key=value（UTF-8、LF、# 开头是注释）：这里只有十来个
// 平铺的键，用不着 JSON 库，而且手改起来比 JSON 友好（出问题时用户能自己看）。
// 文件坏了就当空的处理——设置读不出来绝不能变成启动失败。

#include "config.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "common.hpp"
#include "i18n.hpp"      // settings.ini 的注释头也跟着界面语言走

#if defined(_WIN32)
#  include <windows.h>
#endif

namespace pdfc {

std::string settings_path() {
    return path_join(user_config_dir(), "settings.ini");
}

bool Settings::load() {
    values_.clear();
    std::ifstream input(settings_path(), std::ios::binary);
    if (!input) return false;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string text = trim(line);
        if (text.empty() || text.front() == '#') continue;
        const std::size_t equals = text.find('=');
        if (equals == std::string::npos) continue;
        values_[trim(text.substr(0, equals))] = trim(text.substr(equals + 1));
    }
    return true;
}

bool Settings::save() const {
    const std::string path = settings_path();
    if (!make_directories(path_dirname(path))) return false;
    // 先写临时文件再改名：断电或崩溃不会留下半个设置文件
    const std::string temp = path + ".tmp";
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        // 注释跟着当前语言走：英文用户打开这个文件看到中文注释同样是"漏翻"
        output << tr("# PDF Compressor 的用户设置。\n"
                    "# 由程序自动生成，删掉它就会回到出厂状态（语言按系统区域猜）。\n");
        for (const auto& [key, value] : values_) {
            output << key << '=' << value << '\n';
        }
        if (!output) return false;
    }
    std::remove(path.c_str());
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(temp.c_str());
        return false;
    }
    return true;
}

std::string Settings::get(const std::string& key, const std::string& fallback) const {
    const auto found = values_.find(key);
    return found == values_.end() ? fallback : found->second;
}

int Settings::get_int(const std::string& key, int fallback) const {
    const std::string text = get(key);
    if (text.empty()) return fallback;
    try {
        return std::stoi(text);
    } catch (...) {
        return fallback;
    }
}

double Settings::get_double(const std::string& key, double fallback) const {
    const std::string text = get(key);
    if (text.empty()) return fallback;
    try {
        return std::stod(text);
    } catch (...) {
        return fallback;
    }
}

bool Settings::get_bool(const std::string& key, bool fallback) const {
    const std::string text = to_lower(get(key));
    if (text.empty()) return fallback;
    return text == "1" || text == "true" || text == "yes" || text == "on";
}

void Settings::set(const std::string& key, const std::string& value) {
    values_[key] = value;
}

void Settings::set(const std::string& key, int value) {
    values_[key] = std::to_string(value);
}

void Settings::set(const std::string& key, double value) {
    values_[key] = fmt_general(value);
}

void Settings::set(const std::string& key, bool value) {
    values_[key] = value ? "1" : "0";
}

// ── 注册表 ────────────────────────────────────────────────────

#if defined(_WIN32)
namespace {

/// 依次尝试 HKCU、HKLM；找不到返回 nullopt。
std::optional<std::string> read_registry(HKEY root, const std::string& name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, utf8_to_wide(kRegistryPath).c_str(), 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        return std::nullopt;
    }
    wchar_t buffer[1024];
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const LONG status = RegQueryValueExW(key, utf8_to_wide(name).c_str(), nullptr, &type,
                                         reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) return std::nullopt;
    const std::wstring value(buffer, size / sizeof(wchar_t));
    std::string trimmed = wide_to_utf8(value);
    while (!trimmed.empty() && trimmed.back() == '\0') trimmed.pop_back();
    if (trimmed.empty()) return std::nullopt;
    return trimmed;
}

}  // namespace
#endif

std::optional<std::string> registry_value(const std::string& name) {
#if defined(_WIN32)
    if (auto value = read_registry(HKEY_CURRENT_USER, name)) return value;
    return read_registry(HKEY_LOCAL_MACHINE, name);
#else
    (void)name;
    return std::nullopt;
#endif
}

bool write_user_registry(const std::string& name, const std::string& value) {
#if defined(_WIN32)
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, utf8_to_wide(kRegistryPath).c_str(), 0, nullptr,
                        0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const std::wstring wide = utf8_to_wide(value);
    const LONG status = RegSetValueExW(key, utf8_to_wide(name).c_str(), 0, REG_SZ,
                                       reinterpret_cast<const BYTE*>(wide.c_str()),
                                       static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
#else
    (void)name;
    (void)value;
    return false;
#endif
}

void delete_user_registry_key() {
#if defined(_WIN32)
    RegDeleteTreeW(HKEY_CURRENT_USER, utf8_to_wide(kRegistryPath).c_str());
#endif
}

std::optional<std::string> registry_install_dir() { return registry_value("InstallDir"); }

// ── 语言 ──────────────────────────────────────────────────────

std::optional<Language> configured_language() {
    Settings settings;
    settings.load();
    Language parsed;
    if (parse_language(settings.get("language"), parsed)) return parsed;
    if (auto value = registry_value("Language")) {
        if (parse_language(*value, parsed)) return parsed;
    }
    return std::nullopt;
}

Language detected_language() {
    if (auto configured = configured_language()) return *configured;
    return language_from_locale();
}

bool remember_language(Language value) {
    Settings settings;
    settings.load();
    settings.set("language", language_code(value));
    return settings.save();
}

bool remember_theme(const std::string& theme) {
    Settings settings;
    settings.load();
    settings.set("theme", theme);
    return settings.save();
}

}  // namespace pdfc
