// -*- coding: utf-8 -*-
//
// 用户设置与安装信息。
//
// 三份数据，来源不同、可写性不同：
//
//   设置文件  %LOCALAPPDATA%\pdf-compress\settings.ini —— 用户可写，图形界面
//             里的语言/主题开关和上次填的参数写这里（便携版也能记住）
//   注册表    HKLM\SOFTWARE\pdf-compress —— 安装包写，存安装目录和安装时选的
//             语言；普通权限读得到、写不了
//   注册表    HKCU\Software\pdf-compress —— 便携版自己写的安装目录与语言，
//             不需要管理员权限
//
// 文件格式是极简的 key=value（UTF-8、LF），不引 JSON 库：这里只有十来个
// 平铺的键，用不着嵌套。

#pragma once

#include <map>
#include <optional>
#include <string>

#include "i18n.hpp"

namespace pdfc {

inline constexpr const char* kRegistryPath = "SOFTWARE\\pdf-compress";

/// settings.ini 的路径（目录不存在时由 save() 创建）。
std::string settings_path();

class Settings {
public:
    bool load();                                  // 读不到就当空的，绝不因此失败
    bool save() const;

    std::string get(const std::string& key, const std::string& fallback = {}) const;
    int get_int(const std::string& key, int fallback) const;
    double get_double(const std::string& key, double fallback) const;
    bool get_bool(const std::string& key, bool fallback) const;

    void set(const std::string& key, const std::string& value);
    void set(const std::string& key, int value);
    void set(const std::string& key, double value);
    void set(const std::string& key, bool value);

    const std::map<std::string, std::string>& values() const { return values_; }

private:
    std::map<std::string, std::string> values_;
};

// ── 注册表（非 Windows 上全部返回失败/空） ──────────────────────

std::optional<std::string> registry_value(const std::string& name);
bool write_user_registry(const std::string& name, const std::string& value);
void delete_user_registry_key();
std::optional<std::string> registry_install_dir();

// ── 语言 ──────────────────────────────────────────────────────

/// 用户明确选择过的语言（设置文件 > 注册表），没有则 nullopt。
std::optional<Language> configured_language();

/// 本次运行该用哪种语言：设置过的 → 系统区域。
Language detected_language();

/// 记住用户的选择（图形界面里的语言开关）。
bool remember_language(Language language);
bool remember_theme(const std::string& theme);

}  // namespace pdfc
