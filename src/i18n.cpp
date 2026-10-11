// -*- coding: utf-8 -*-
//
// 语言切换与文案查表。

#include "i18n.hpp"

#include <cstdlib>
#include <unordered_map>

#include "common.hpp"

#if defined(_WIN32)
#  include <windows.h>       // GetUserDefaultUILanguage
#endif

namespace pdfc {
namespace {

Language g_language = Language::Zh;

// 英文表分两张：strings_en.cpp 是从 Python 版生成的（两边共用的文案），
// strings_en_extra.cpp 是 C++ 侧独有的补充。合并成一张哈希表来查。
const std::unordered_map<std::string, std::string>& english_map() {
    static const std::unordered_map<std::string, std::string> map = [] {
        std::unordered_map<std::string, std::string> built;
        built.reserve(512);
        for (const auto& [chinese, english] : english_table()) {
            built.emplace(chinese, english);
        }
        for (const auto& [chinese, english] : english_extra_table()) {
            built.emplace(chinese, english);
        }
        return built;
    }();
    return map;
}

}  // namespace

bool parse_language(const std::string& text, Language& out) {
    std::string code = to_lower(trim(text));
    for (char& ch : code) {
        if (ch == '_') ch = '-';
    }
    const std::size_t dash = code.find('-');
    const std::string head = dash == std::string::npos ? code : code.substr(0, dash);
    if (head == "zh" || head == "cn" || head == "chs" || head == "chinese" ||
        head == "hans" || head == "zhongwen") {
        out = Language::Zh;
        return true;
    }
    if (head == "en" || head == "eng" || head == "english") {
        out = Language::En;
        return true;
    }
    return false;
}

const char* language_code(Language language) {
    return language == Language::En ? "en" : "zh";
}

const char* language_display_name(Language language) {
    // 语言名用各自的语言书写，语言选择框里才不会出现看不懂的选项
    return language == Language::En ? "English" : "简体中文";
}

Language language_from_locale() {
    // 环境变量优先（Linux/macOS 上最可靠）
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE"}) {
        if (const char* value = std::getenv(name)) {
            Language parsed;
            if (parse_language(value, parsed)) return parsed;
        }
    }
#if defined(_WIN32)
    // GetUserDefaultUILanguage 返回 LANGID，中文系的语言 ID 低 10 位是 0x04
    const LANGID id = GetUserDefaultUILanguage();
    return (id & 0x3FF) == 0x04 ? Language::Zh : Language::En;
#else
    return Language::Zh;
#endif
}

Language language() { return g_language; }

void set_language(Language value) { g_language = value; }

const char* tr(const char* chinese) {
    if (chinese == nullptr) return "";
    if (g_language == Language::Zh) return chinese;
    const auto& map = english_map();
    const auto found = map.find(chinese);
    return found == map.end() ? chinese : found->second.c_str();
}

std::string tr(const char* chinese,
               const std::vector<std::pair<std::string, std::string>>& values) {
    return format(tr(chinese), values);
}

bool english_table_has(const std::string& chinese) {
    return english_map().find(chinese) != english_map().end();
}

}  // namespace pdfc
