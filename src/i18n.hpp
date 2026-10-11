// -*- coding: utf-8 -*-
//
// 语言切换。
//
// 中文是这个项目的源语言：所有文案都以中文字面量写在代码里，tr() 在中文下
// 原样返回，在英文下查 strings_en.cpp 的表（以中文原文为键）。这跟 Python 版
// 用的是同一套做法，两边的文案表可以逐条对照，甚至能用脚本互相校验。
//
// 语言从哪来（优先级从高到低）：
//     1. 用户在设置文件里选过的语言（图形界面里的语言开关写它）
//     2. 安装包写进 HKLM\SOFTWARE\pdf-compress 的 Language
//     3. --lang 命令行参数（只影响本次运行，不落盘）
//     4. 系统区域：中文系用中文，其余用英文

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace pdfc {

enum class Language { Zh, En };

/// 把各种写法（zh-CN、zh_CN、ZH、Chinese…）收敛成 Language；认不出返回 false。
bool parse_language(const std::string& text, Language& out);

/// 语言代码（"zh" / "en"）与显示名（"简体中文" / "English"）。
const char* language_code(Language language);
const char* language_display_name(Language language);

/// 按系统区域猜一个。
Language language_from_locale();

/// 当前语言。
Language language();
void set_language(Language language);

/// 文案查询：中文原样返回，英文查表；查不到就退回中文原文。
const char* tr(const char* chinese);

/// 带占位符的文案：先用 tr() 取文案，再按名字替换 {name} 形式的占位符。
std::string tr(const char* chinese,
               const std::vector<std::pair<std::string, std::string>>& values);

/// 英文表是否收录了这条文案（tests 用来查漏翻）。
bool english_table_has(const std::string& chinese);

/// 从 Python 版生成的那张英文表，供测试遍历。
const std::vector<std::pair<const char*, const char*>>& english_table();

/// C++ 版独有的补充表（见 strings_en_extra.cpp）。
const std::vector<std::pair<const char*, const char*>>& english_extra_table();

}  // namespace pdfc
