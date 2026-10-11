// -*- coding: utf-8 -*-
//
// 配色与字体。
//
// 配色照着 Win11 的设计语言来：背景 #F3F3F3 / 深色 #202020，卡片 #FFFFFF /
// #2B2B2B，正文 #1A1A1A / #FFFFFF，次要文字 #616161 / #C6C6C6，强调色
// #0067C0 / #60CDFF，边框 #E5E5E5 / #3A3A3A，圆角 8px。
//
// 字体中文优先：界面主要以中文呈现，微软雅黑的中文字形比 Segoe UI 回退出来的
// 结果更饱满，小字号下也更清楚；Segoe UI 留在后面给英文系统兜底。标题和按钮
// 用同一个字族的粗体（微软雅黑自带真粗体，中英文一起变实），而不是去凑
// "Semibold" 那类只有拉丁字形的独立字族——那样中文会走字体链接按常规字重
// 回退，看起来还是细的。

#pragma once

#include <d2d1.h>
#include <dwrite.h>

namespace pdfc::gui {

struct Theme {
    const char* name = "light";
    bool dark = false;

    D2D1_COLOR_F bg{};            // 窗口底色
    D2D1_COLOR_F card{};          // 卡片底色
    D2D1_COLOR_F border{};        // 卡片描边、滚动条滑块
    D2D1_COLOR_F text{};          // 正文
    D2D1_COLOR_F muted{};         // 次要文字
    D2D1_COLOR_F accent{};        // 强调色（主按钮、进度条、复选框）
    D2D1_COLOR_F accent_text{};   // 强调色上的文字
    D2D1_COLOR_F hover{};         // 主按钮悬停
    D2D1_COLOR_F subtle{};        // 次要按钮底色
    D2D1_COLOR_F subtle_hover{};
    D2D1_COLOR_F field{};         // 输入框底色
    D2D1_COLOR_F field_border{};
    D2D1_COLOR_F selected{};      // 列表选中底色
    D2D1_COLOR_F log_bg{};        // 日志窗底色
};

const Theme& light_theme();
const Theme& dark_theme();
const Theme& theme_by_name(const char* name);

enum class FontRole {
    Ui,            // 正文控件
    Small,         // 标签、说明
    Title,         // 标题
    Subtitle,      // 标题下的一行说明
    Heading,       // 卡片标题（粗）
    Semibold,      // 主按钮（粗）
    SemiboldSmall, // 小号主按钮（粗）
    Mono,          // 日志窗
    Count
};

/// DirectWrite 文本格式集合，按 DPI 缩放创建一次。
struct FontSet {
    IDWriteTextFormat* formats[static_cast<int>(FontRole::Count)] = {};

    bool create(IDWriteFactory* factory, float scale);
    void destroy();
    IDWriteTextFormat* get(FontRole role) const {
        return formats[static_cast<int>(role)];
    }
};

/// 界面元素共用的圆角半径（96 DPI 下的像素值，使用时要乘缩放）。
inline constexpr float kCardRadius = 8.0f;
inline constexpr float kButtonRadius = 6.0f;
inline constexpr float kBoxRadius = 4.0f;

}  // namespace pdfc::gui
