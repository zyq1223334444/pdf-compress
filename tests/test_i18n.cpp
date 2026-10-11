// -*- coding: utf-8 -*-
//
// 文案表的运行时检查。
//
// 静态的"有没有漏翻"由 tools/check_i18n.py 跨语言比对（它要读 Python 那份表，
// 编译期做不到）。这里管的是编译进来的这张表本身对不对：
//   * 两张表都非空、没有空键值、没有重复键
//   * 中英文的占位符名字一致（不一致的话英文界面会缺参数）
//   * tr() 在两种语言下的行为：中文返回原文、英文返回译文、查不到时退回原文
//   * 带占位符的 tr() 真的做了替换，而且格式说明符（{size:.2f}）被正确吃掉
//
// 不引第三方测试框架：这个工程的测试只需要"能跑、失败时退出码非 0"。

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "common.hpp"
#include "i18n.hpp"

using namespace pdfc;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

/// 取出 {...} 里的字段名（忽略格式说明符），与 check_i18n.py 的规则一致。
std::set<std::string> placeholders(const std::string& text) {
    std::set<std::string> names;
    std::size_t at = 0;
    while ((at = text.find('{', at)) != std::string::npos) {
        const std::size_t close = text.find('}', at);
        if (close == std::string::npos) break;
        std::string body = text.substr(at + 1, close - at - 1);
        const std::size_t colon = body.find(':');
        if (colon != std::string::npos) body = body.substr(0, colon);
        // 只有像标识符的才算占位符，避开帮助文本里 "{auto,manual}" 这类字面量
        bool identifier = !body.empty();
        for (char ch : body) {
            if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) {
                identifier = false;
                break;
            }
        }
        if (identifier && !std::isdigit(static_cast<unsigned char>(body.front()))) {
            names.insert(body);
        }
        at = close + 1;
    }
    return names;
}

void test_tables_are_sane() {
    const auto& generated = english_table();
    const auto& extra = english_extra_table();
    expect(!generated.empty(), "从 Python 版生成的英文表不能为空");
    expect(!extra.empty(), "C++ 补充英文表不能为空");

    std::set<std::string> seen;
    const auto check = [&](const char* chinese, const char* english, const char* which) {
        expect(chinese != nullptr && *chinese != '\0',
               std::string(which) + " 里有空的中文键");
        expect(english != nullptr && *english != '\0',
               std::string(which) + " 里 " + chinese + " 的译文为空");
        if (chinese != nullptr && *chinese != '\0') {
            expect(seen.insert(chinese).second,
                   std::string("两张表里有重复的键：") + chinese);
        }
        if (chinese != nullptr && english != nullptr) {
            expect(placeholders(chinese) == placeholders(english),
                   std::string("占位符对不上：") + chinese + " -> " + english);
        }
    };
    for (const auto& [chinese, english] : generated) check(chinese, english, "生成表");
    for (const auto& [chinese, english] : extra) check(chinese, english, "补充表");
}

void test_language_switching() {
    set_language(Language::Zh);
    expect(std::string(tr("失败")) == "失败", "中文下 tr() 应当原样返回");

    set_language(Language::En);
    // 注意大小写：表里就是 "Failed"
    expect(std::string(tr("失败")) == "Failed", "英文下 tr() 应当返回译文");
    // 表里没有的文案要退回中文，而不是返回空串或崩溃
    expect(std::string(tr("这句没有翻译")) == "这句没有翻译",
           "查不到译文时应当退回中文原文");
    // 帮助文本里真的有花括号字面量，没有参数时不能被当成占位符
    expect(std::string(tr("--mode {auto,manual}")) == "--mode {auto,manual}",
           "没有参数时不该做替换");

    set_language(Language::Zh);
}

void test_placeholder_formatting() {
    set_language(Language::En);
    const std::string text = tr("[结果] 文件大小 {size}（目标 {target:.2f} MB，误差 {deviation:+.1%}）",
                                {{"size", human_size(1024 * 1024)},
                                 {"target", fmt_fixed(8.0, 2)},
                                 {"deviation", fmt_percent(0.012, 1)}});
    expect(text.find("{size}") == std::string::npos, "占位符 {size} 没有被替换");
    expect(text.find("1.00 MB") != std::string::npos, "human_size 的结果没有填进去");
    expect(text.find("8.00") != std::string::npos, "带格式说明符的占位符没有被替换");
    expect(text.find("+1.2%") != std::string::npos, "百分号格式化不对");
    set_language(Language::Zh);
}

void test_number_formatting() {
    expect(human_size(0) == "0 B", "0 字节应当显示成 0 B");
    expect(human_size(1023) == "1023 B", "不到 1KB 时按字节显示");
    expect(human_size(1024) == "1.00 KB", "1KB 应当保留两位小数");
    expect(human_size(1024.0 * 1024 * 3.5) == "3.50 MB", "MB 换算不对");
    expect(fmt_general(600.0) == "600", "整数不该带小数点");
    expect(fmt_general(7.2) == "7.2", "小数应当原样");
    expect(fmt_percent(-0.05, 1) == "-5.0%", "负数百分号应当带减号");
}

}  // namespace

int main() {
    test_tables_are_sane();
    test_language_switching();
    test_placeholder_formatting();
    test_number_formatting();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d 项检查未通过\n", g_failures);
        return 1;
    }
    std::printf("文案表检查通过（生成表 %zu 条 + 补充表 %zu 条）\n",
                english_table().size(), english_extra_table().size());
    return 0;
}
