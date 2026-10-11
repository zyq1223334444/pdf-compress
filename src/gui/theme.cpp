// -*- coding: utf-8 -*-
//
// 配色表与 DirectWrite 文本格式集合的实现。
//
// 两件事在这里落地：
//
//   1. 两套配色常量。深浅两套的每一个色值都与 Python 版 pdfcomp/gui.py 的
//      LIGHT / DARK 逐字对应——用户已经看过 Python 版的截图并认可，颜色不能
//      再"顺手调一调"。色值直接写 0xRRGGBB，比手写 D2D1_COLOR_F 的可读性高，
//      也方便和 Python 文件对照。
//
//   2. 字体族挑选与字号表。挑选方式和 Python 版一样是"候选表里取第一个能用的"，
//      只是用 IDWriteFontCollection::FindFamilyName 判断"能不能用"，比 Tk 的
//      tkfont.families() 更准确（它是按已安装字体族登记的）。
//
// 字号表按 96 DPI 下的像素写死，create() 时统一乘 DPI 缩放。理由见 theme.hpp
// 的说明：进程已声明为系统 DPI 感知，DirectWrite 不会替我们再缩放一次，所以
// 这个乘法必须由我们自己来做，而且只能做一次。

#include "gui/theme.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "common.hpp"

namespace pdfc::gui {
namespace {

// ── 色值工具 ──────────────────────────────────────────────────

/// 0xRRGGBB 展开成 D2D1_COLOR_F（含 alpha=1）。
///
/// 不能写成 constexpr：D2D1::ColorF 的构造函数内部走 snprintf，编译器不允许在
/// 常量表达式里调用它。inline 就够了——每个主题只构造一次（函数内的 static），
/// 这点开销可以忽略。
inline D2D1_COLOR_F rgb(unsigned int hex) {
    const float r = static_cast<float>((hex >> 16) & 0xFFu) / 255.0f;
    const float g = static_cast<float>((hex >> 8) & 0xFFu) / 255.0f;
    const float b = static_cast<float>(hex & 0xFFu) / 255.0f;
    return D2D1::ColorF(r, g, b, 1.0f);
}

// ── 字体候选表 ────────────────────────────────────────────────
//
// 界面主要以中文呈现，所以中文优先：微软雅黑的字形比 Segoe UI 链接出来的
// 结果更饱满，小字号下更清楚；Segoe UI 留在后面给英文系统兜底。
const wchar_t* const kUiFamilies[] = {
    L"Microsoft YaHei UI",
    L"Microsoft YaHei",
    L"Segoe UI Variable Text",
    L"Segoe UI",
    L"PingFang SC",
    L"Noto Sans CJK SC",
    L"Helvetica",
};

// 标题用一组"显示"字族，理由同上，只是拉丁字族的名字不同。
const wchar_t* const kDisplayFamilies[] = {
    L"Microsoft YaHei UI",
    L"Microsoft YaHei",
    L"Segoe UI Variable Display",
    L"Segoe UI",
    L"PingFang SC",
    L"Helvetica",
};

// 日志窗要求等宽。中文注释也一样对齐，所以优先中英兼顾的等宽字族。
const wchar_t* const kMonoFamilies[] = {
    L"Cascadia Mono",
    L"Consolas",
    L"DejaVu Sans Mono",
    L"Menlo",
    L"Noto Sans Mono CJK SC",
    L"Courier New",
};

DWRITE_FONT_WEIGHT normal_weight() { return DWRITE_FONT_WEIGHT_NORMAL; }
DWRITE_FONT_WEIGHT bold_weight() { return DWRITE_FONT_WEIGHT_BOLD; }

/// 取第一个能用的字族名写入 out；都不行返回 false。
bool pick_family(IDWriteFontCollection* collection, const wchar_t* const* names,
                 int count, std::wstring& out) {
    if (collection == nullptr) return false;
    for (int index = 0; index < count; ++index) {
        UINT32 found = 0;
        BOOL exists = FALSE;
        const std::wstring name(names[index]);
        // FindFamilyName 要的是 locale 无关的名字，这里给空串表示"当前区域"
        if (SUCCEEDED(collection->FindFamilyName(name.c_str(), &found, &exists)) && exists) {
            out = name;
            return true;
        }
    }
    return false;
}

}  // namespace

// ── 主题 ──────────────────────────────────────────────────────

const Theme& light_theme() {
    // 顺序与 gui.py 的 LIGHT 完全一致：bg/card/border/text/muted/accent/
    // accent_text/hover/subtle/subtle_hover/field/field_border/selected/log_bg
    static const Theme value = [] {
        Theme theme;
        theme.name = "light";
        theme.dark = false;
        theme.bg = rgb(0xF3F3F3);
        theme.card = rgb(0xFFFFFF);
        theme.border = rgb(0xE5E5E5);
        theme.text = rgb(0x1A1A1A);
        theme.muted = rgb(0x616161);
        theme.accent = rgb(0x0067C0);
        theme.accent_text = rgb(0xFFFFFF);
        theme.hover = rgb(0x1A7BD0);
        theme.subtle = rgb(0xFDFDFD);
        theme.subtle_hover = rgb(0xF5F5F5);
        theme.field = rgb(0xFFFFFF);
        theme.field_border = rgb(0xD6D6D6);
        theme.selected = rgb(0xDCEBFB);
        theme.log_bg = rgb(0xFBFBFB);
        return theme;
    }();
    return value;
}

const Theme& dark_theme() {
    static const Theme value = [] {
        Theme theme;
        theme.name = "dark";
        theme.dark = true;
        theme.bg = rgb(0x202020);
        theme.card = rgb(0x2B2B2B);
        theme.border = rgb(0x3A3A3A);
        theme.text = rgb(0xFFFFFF);
        theme.muted = rgb(0xC6C6C6);
        theme.accent = rgb(0x60CDFF);
        theme.accent_text = rgb(0x003355);
        theme.hover = rgb(0x7BD8FF);
        theme.subtle = rgb(0x323232);
        theme.subtle_hover = rgb(0x3C3C3C);
        theme.field = rgb(0x333333);
        theme.field_border = rgb(0x5A5A5A);
        theme.selected = rgb(0x0A4A75);
        theme.log_bg = rgb(0x262626);
        return theme;
    }();
    return value;
}

const Theme& theme_by_name(const char* name) {
    // 设置文件里存的是 "light" / "dark"，未知值一律当浅色，和 Python 版一致。
    return (name != nullptr && to_lower(std::string(name)) == "dark") ? dark_theme()
                                                                     : light_theme();
}

// ── 字体 ──────────────────────────────────────────────────────

bool FontSet::create(IDWriteFactory* factory, float scale) {
    destroy();
    if (factory == nullptr) return false;
    if (!(scale > 0.0f)) scale = 1.0f;

    IDWriteFontCollection* collection = nullptr;
    if (FAILED(factory->GetSystemFontCollection(&collection, FALSE)) || collection == nullptr) {
        return false;
    }

    std::wstring ui_family = L"Segoe UI";
    std::wstring display_family = ui_family;
    std::wstring mono_family = L"Consolas";
    pick_family(collection, kUiFamilies,
                static_cast<int>(std::size(kUiFamilies)), ui_family);
    pick_family(collection, kDisplayFamilies,
                static_cast<int>(std::size(kDisplayFamilies)), display_family);
    pick_family(collection, kMonoFamilies,
                static_cast<int>(std::size(kMonoFamilies)), mono_family);

    // 字号表：与 gui.py 的 fonts() 逐项对应（那里的 px 就是这个值）。
    struct Spec {
        FontRole role;
        const std::wstring* family;
        float size;
        DWRITE_FONT_WEIGHT weight;
    };
    const Spec specs[] = {
        {FontRole::Ui, &ui_family, 13.5f, normal_weight()},
        {FontRole::Small, &ui_family, 12.0f, normal_weight()},
        {FontRole::Title, &display_family, 23.0f, bold_weight()},
        {FontRole::Subtitle, &ui_family, 12.5f, normal_weight()},
        {FontRole::Heading, &ui_family, 13.5f, bold_weight()},
        {FontRole::Semibold, &ui_family, 13.5f, bold_weight()},
        {FontRole::SemiboldSmall, &ui_family, 12.0f, bold_weight()},
        {FontRole::Mono, &mono_family, 12.0f, normal_weight()},
    };

    for (const Spec& spec : specs) {
        IDWriteTextFormat* format = nullptr;
        // 语言给空串：让 DirectWrite 按字体自己的默认语言整形，中英混排都不会挑错字形
        const HRESULT hr = factory->CreateTextFormat(
            spec.family->c_str(), nullptr, spec.weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, spec.size * scale, L"", &format);
        if (FAILED(hr) || format == nullptr) {
            collection->Release();
            destroy();
            return false;
        }
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        // 单行控件：不让 DirectWrite 自作主张换行，宽度不够时按字符裁剪
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        formats[static_cast<int>(spec.role)] = format;
    }

    collection->Release();
    return true;
}

void FontSet::destroy() {
    for (IDWriteTextFormat*& format : formats) {
        if (format != nullptr) {
            format->Release();
            format = nullptr;
        }
    }
}

}  // namespace pdfc::gui
