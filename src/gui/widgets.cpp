// -*- coding: utf-8 -*-
//
// 自绘控件的实现：圆角矩形、文字、勾、滑块。
//
// 这里只做"像素"级别的事：拿到一个矩形和一份状态，往 Direct2D 渲染目标上画。
// 所有的鼠标命中判定也在这里，但"点完之后要干什么"由调用方通过回调决定——
// 控件不认识文件列表、不认识引擎，换主题换语言重建界面时它们跟着一起重建就行。
//
// 几处刻意的选择：
//   * 勾用两段折线（ID2D1PathGeometry）画，不用字符 "√"：字体里有没有这个
//     字形、它的粗细和位置合不合适都不由我们决定，折线则任何环境都一样。
//   * 下拉的箭头用三角形填充，不用字符，理由同上（也和 gui.py 里"不依赖
//     Tk 的箭头样式"是同一个思路）。
//   * 滚动条内容放得下时完全不画，也不接受点击（交给列表去处理选中）。
//   * 所有 96 DPI 下的尺寸（圆角半径、复选框边长）都要乘缩放，见各函数注释。

#include "gui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <windows.h>
#include <shellapi.h>   // DragAcceptFiles：输入框也要接住拖进来的 PDF

#include "common.hpp"
#include "i18n.hpp"

namespace pdfc::gui {
namespace {

/// 把 Rect 转成 D2D1 的圆角矩形。半径按 D2D1 的规矩收敛：
/// 半径大于半高或半宽时 D2D 会把它按比例缩掉，这里先自己夹一下更直观。
D2D1_ROUNDED_RECT rounded(const Rect& rect, float radius) {
    const float limit = std::min(rect.width(), rect.height()) * 0.5f;
    const float r = std::max(0.0f, std::min(radius, limit));
    return D2D1::RoundedRect(D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom), r, r);
}

/// 单色画刷辅助：拿不到时不画（正常流程下不会发生）。
void fill_rect(const DrawContext& ctx, const Rect& rect, ID2D1SolidColorBrush* brush) {
    if (brush == nullptr || rect.empty()) return;
    ctx.rt->FillRectangle(D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom), brush);
}

/// 复选框的勾：一条两段折线。
ID2D1PathGeometry* make_check_geometry(ID2D1Factory* factory, float size, float top) {
    if (factory == nullptr) return nullptr;
    ID2D1PathGeometry* geometry = nullptr;
    if (FAILED(factory->CreatePathGeometry(&geometry)) || geometry == nullptr) return nullptr;
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(geometry->Open(&sink)) || sink == nullptr) {
        geometry->Release();
        return nullptr;
    }
    sink->BeginFigure(D2D1::Point2F(size * 0.26f, top + size * 0.52f),
                      D2D1_FIGURE_BEGIN_HOLLOW);
    const D2D1_POINT_2F middle = D2D1::Point2F(size * 0.44f, top + size * 0.70f);
    const D2D1_POINT_2F end = D2D1::Point2F(size * 0.76f, top + size * 0.30f);
    sink->AddLine(middle);
    sink->AddLine(end);
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    sink->Release();
    return geometry;
}

/// 下拉箭头的三角形。
void fill_triangle(const DrawContext& ctx, const D2D1_POINT_2F& a, const D2D1_POINT_2F& b,
                   const D2D1_POINT_2F& c, ID2D1SolidColorBrush* brush) {
    if (brush == nullptr) return;
    ID2D1Factory* factory = nullptr;
    ctx.rt->GetFactory(&factory);
    if (factory == nullptr) return;
    ID2D1PathGeometry* geometry = nullptr;
    if (SUCCEEDED(factory->CreatePathGeometry(&geometry)) && geometry != nullptr) {
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(geometry->Open(&sink)) && sink != nullptr) {
            sink->BeginFigure(a, D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(b);
            sink->AddLine(c);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            ctx.rt->FillGeometry(geometry, brush);
        }
        geometry->Release();
    }
    factory->Release();
}

}  // namespace

// ── 矩形工具 ──────────────────────────────────────────────────

Rect make_rect(float left, float top, float right, float bottom) {
    return Rect{left, top, right, bottom};
}

Rect inset(const Rect& rect, float amount) {
    return Rect{rect.left + amount, rect.top + amount, rect.right - amount,
                rect.bottom - amount};
}

Rect offset(const Rect& rect, float dx, float dy) {
    return Rect{rect.left + dx, rect.top + dy, rect.right + dx, rect.bottom + dy};
}

void split_top(const Rect& rect, float height, float gap, Rect& out_top, Rect& out_rest) {
    out_top = Rect{rect.left, rect.top, rect.right, rect.top + height};
    out_rest = Rect{rect.left, rect.top + height + gap, rect.right, rect.bottom};
}

void split_left(const Rect& rect, float width, float gap, Rect& out_left, Rect& out_right) {
    out_left = Rect{rect.left, rect.top, rect.left + width, rect.bottom};
    out_right = Rect{rect.left + width + gap, rect.top, rect.right, rect.bottom};
}

void split_right(const Rect& rect, float width, float gap, Rect& out_left, Rect& out_right) {
    out_right = Rect{rect.right - width, rect.top, rect.right, rect.bottom};
    out_left = Rect{rect.left, rect.top, rect.right - width - gap, rect.bottom};
}

Rect take_top(const Rect& rect, float height, Rect& out_rest) {
    const Rect taken{rect.left, rect.top, rect.right, rect.top + height};
    out_rest = Rect{rect.left, rect.top + height, rect.right, rect.bottom};
    return taken;
}

Rect take_bottom(const Rect& rect, float height, Rect& out_rest) {
    const Rect taken{rect.left, rect.bottom - height, rect.right, rect.bottom};
    out_rest = Rect{rect.left, rect.top, rect.right, rect.bottom - height};
    return taken;
}

Rect take_right(const Rect& rect, float width, Rect& out_rest) {
    const Rect taken{rect.right - width, rect.top, rect.right, rect.bottom};
    out_rest = Rect{rect.left, rect.top, rect.right - width, rect.bottom};
    return taken;
}

Rect take_left(const Rect& rect, float width, Rect& out_rest) {
    const Rect taken{rect.left, rect.top, rect.left + width, rect.bottom};
    out_rest = Rect{rect.left + width, rect.top, rect.right, rect.bottom};
    return taken;
}

// ── 绘制 ──────────────────────────────────────────────────────

void draw_text(const DrawContext& ctx, const std::wstring& text, const Rect& rect,
               FontRole role, const D2D1_COLOR_F& color, TextAlign align, TextVAlign valign) {
    if (!ctx.ready() || text.empty() || rect.empty()) return;
    IDWriteTextFormat* format = ctx.fonts->get(role);
    if (format == nullptr) return;

    ID2D1SolidColorBrush* brush = nullptr;
    if (FAILED(ctx.rt->CreateSolidColorBrush(color, &brush)) || brush == nullptr) return;

    // 对齐方式放在 format 上（DirectWrite 没有 per-call 的对齐参数）。
    // format 是全局共享的，所以这里改完必须还原，否则会串到别的控件上。
    const DWRITE_TEXT_ALIGNMENT previous = format->GetTextAlignment();
    const DWRITE_PARAGRAPH_ALIGNMENT previous_line = format->GetParagraphAlignment();
    format->SetTextAlignment(align == TextAlign::Center  ? DWRITE_TEXT_ALIGNMENT_CENTER
                             : align == TextAlign::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                         : DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(valign == TextVAlign::Center ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                                               : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    ctx.rt->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format,
                      D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom), brush,
                      D2D1_DRAW_TEXT_OPTIONS_CLIP);
    format->SetTextAlignment(previous);
    format->SetParagraphAlignment(previous_line);
    brush->Release();
}

void draw_text_utf8(const DrawContext& ctx, const std::string& text, const Rect& rect,
                    FontRole role, const D2D1_COLOR_F& color, TextAlign align,
                    TextVAlign valign) {
    if (text.empty()) return;
    draw_text(ctx, utf8_to_wide(text), rect, role, color, align, valign);
}

void draw_text_utf8_wrapped(const DrawContext& ctx, const std::string& text, const Rect& rect,
                            FontRole role, const D2D1_COLOR_F& color, TextAlign align,
                            TextVAlign valign) {
    if (!ctx.ready() || text.empty() || rect.empty()) return;
    IDWriteTextFormat* format = ctx.fonts->get(role);
    if (format == nullptr) return;
    const DWRITE_WORD_WRAPPING previous = format->GetWordWrapping();
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    draw_text(ctx, utf8_to_wide(text), rect, role, color, align, valign);
    format->SetWordWrapping(previous);
}

TextExtent measure_text_extent(const DrawContext& ctx, const std::wstring& text,
                               FontRole role, float max_width, bool wrap) {
    TextExtent extent;
    if (!ctx.ready() || text.empty() || max_width <= 1.0f) return extent;
    IDWriteTextFormat* format = ctx.fonts->get(role);
    if (format == nullptr || ctx.dwrite == nullptr) return extent;
    // 量高度时换行方式必须和真正绘制时一致，否则"No wrap 量出一行、wrap 画成三行"
    const DWRITE_WORD_WRAPPING previous = format->GetWordWrapping();
    if (wrap) format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(ctx.dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                            format, max_width, 100000.0f, &layout)) ||
        layout == nullptr) {
        if (wrap) format->SetWordWrapping(previous);
        return extent;
    }
    DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(layout->GetMetrics(&metrics))) {
        extent.width = metrics.widthIncludingTrailingWhitespace;
        extent.height = metrics.height;
    }
    layout->Release();
    if (wrap) format->SetWordWrapping(previous);
    return extent;
}

float measure_text(const DrawContext& ctx, const std::wstring& text, FontRole role) {
    return measure_text_extent(ctx, text, role).width;
}

void fill_round(const DrawContext& ctx, const Rect& rect, float radius,
                ID2D1SolidColorBrush* brush) {
    if (!ctx.ready() || brush == nullptr || rect.empty()) return;
    ctx.rt->FillRoundedRectangle(rounded(rect, radius), brush);
}

void stroke_round(const DrawContext& ctx, const Rect& rect, float radius,
                  ID2D1SolidColorBrush* brush, float width) {
    if (!ctx.ready() || brush == nullptr || rect.empty()) return;
    // 描边压在矩形边界上会有一半落在外面，往里收半个线宽看起来更整齐
    const Rect inner = inset(rect, width * 0.5f);
    ctx.rt->DrawRoundedRectangle(rounded(inner, radius), brush, width);
}

// ── 文字输入框 ────────────────────────────────────────────────

HWND create_edit(HWND parent, HINSTANCE instance, const Rect& bounds, bool multiline,
                 bool read_only, DWRITE_FONT_WEIGHT weight, float font_size, int margin_x,
                 int margin_y) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    if (multiline) {
        style |= ES_MULTILINE | ES_LEFT | ES_AUTOVSCROLL;
        if (read_only) style |= ES_READONLY;
    } else {
        style |= ES_AUTOHSCROLL | ES_LEFT;
    }
    // 不要 WS_EX_CLIENTEDGE：输入框的边框和圆角由我们自己在卡片上画
    const HWND edit = CreateWindowExW(
        0, L"EDIT", L"", style, static_cast<int>(std::lround(bounds.left)),
        static_cast<int>(std::lround(bounds.top)),
        static_cast<int>(std::lround(bounds.width())),
        static_cast<int>(std::lround(bounds.height())), parent, nullptr, instance, nullptr);
    if (edit == nullptr) return nullptr;

    // 字体必须给：GDI 的 EDIT 不会用 DirectWrite 的字体，不给就是 System 字体，
    // 中文在高 DPI 下会糊，行高也和自绘部分对不齐。
    HFONT font = CreateFontW(-static_cast<int>(std::lround(font_size)),
                            0, 0, 0, static_cast<int>(weight), FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                            L"Microsoft YaHei UI");
    if (font != nullptr) SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    // 内边距：EDIT 默认贴着边框写字，加一点留白才和卡片里的其它元素对齐
    if (margin_x != 0 || margin_y != 0) {
        if (multiline) {
            RECT margins{};
            margins.left = margin_x;
            margins.right = margin_x;
            margins.top = margin_y;
            margins.bottom = margin_y;
            SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                         MAKELPARAM(margin_x, margin_x));
            SendMessageW(edit, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&margins));
        } else {
            SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                         MAKELPARAM(margin_x, margin_x));
        }
    }
    // 输入框本身也要收拖放：鼠标停在"输出目录"或日志窗上松手时，消息只会
    // 送到这个子控件；不注册的话拖到这些地方就完全没反应（整个窗口看着像
    // 不接受拖放）。子类化那里会把它转给父窗口，落到同一个文件列表里。
    DragAcceptFiles(edit, TRUE);
    return edit;
}

void set_edit_text(HWND edit, const std::string& text) {
    if (edit == nullptr) return;
    const std::wstring wide = utf8_to_wide(text);
    SetWindowTextW(edit, wide.c_str());
    // 光标移到末尾：重建界面后接着输入时，插入点不该留在一堆旧文字中间
    SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(wide.size()),
                 static_cast<LPARAM>(wide.size()));
}

std::string get_edit_text(HWND edit) {
    if (edit == nullptr) return {};
    const int length = GetWindowTextLengthW(edit);
    if (length <= 0) return {};
    std::wstring buffer(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(edit, buffer.data(), length + 1);
    if (copied <= 0) return {};
    buffer.resize(static_cast<std::size_t>(copied));
    return wide_to_utf8(buffer);
}

void clip_round_region(HWND hwnd, float radius) {
    if (hwnd == nullptr) return;
    RECT client{};
    if (!GetClientRect(hwnd, &client)) return;
    const int width = static_cast<int>(std::max<LONG>(1, client.right - client.left));
    const int height = static_cast<int>(std::max<LONG>(1, client.bottom - client.top));

    // 椭圆宽高就是圆角直径。半径不足 1 像素时没有裁的必要，直接恢复矩形
    // （传 nullptr 会让系统把之前那个区域释放掉）。
    const int diameter = static_cast<int>(std::lround(std::max(0.0f, radius) * 2.0f));
    if (diameter <= 1) {
        SetWindowRgn(hwnd, nullptr, TRUE);
        return;
    }

    // CreateRoundRectRgn 的右/下边界是开区间，所以各 +1：不 +1 的话最右一列和
    // 最下一行像素会留在区域外面，看起来就是边框缺了一条边。
    HRGN region = ::CreateRoundRectRgn(0, 0, width + 1, height + 1, diameter, diameter);
    if (region == nullptr) return;
    if (::SetWindowRgn(hwnd, region, TRUE) == 0) {
        // 设置失败时系统没有接管这个区域，必须自己删，否则漏一个 GDI 对象
        ::DeleteObject(region);
    }
}

// ── 按钮 ──────────────────────────────────────────────────────

void Button::paint(const DrawContext& ctx, float scale) const {
    if (!ctx.ready() || bounds.empty()) return;

    ID2D1SolidColorBrush* fill = ctx.subtle;
    ID2D1SolidColorBrush* foreground = ctx.text;
    ID2D1SolidColorBrush* outline = ctx.border;

    if (!enabled) {
        // 禁用态：底色退成次要底色，文字退成灰色。
        // 主按钮这里**不能**用卡片色当文字色：浅色主题下 minor 底色(#FDFDFD)和
        // 卡片色(#FFFFFF)几乎一样，白字落在白底上，整个按钮看上去就是空框一个。
        fill = ctx.subtle;
        foreground = ctx.muted;
        outline = ctx.border;
    } else if (kind == ButtonKind::Accent) {
        fill = hover ? ctx.hover : ctx.accent;
        foreground = ctx.accent_text;
        outline = fill;
    } else {
        fill = hover ? ctx.subtle_hover : ctx.subtle;
        foreground = ctx.text;
        outline = ctx.border;
    }

    // 按下时整块往下错 1px（96 DPI 下），和 gui.py 的 offset 一致
    const float dy = pressed ? scale : 0.0f;
    const Rect box = offset(bounds, 0.0f, dy);

    fill_round(ctx, box, radius * scale, fill);
    stroke_round(ctx, box, radius * scale, outline, std::max(1.0f, scale));

    Rect label = box;
    draw_text_utf8(ctx, text, label, role, foreground->GetColor(), TextAlign::Center);
}

void Button::on_mouse_move(float x, float y) {
    const bool inside = enabled && bounds.contains(x, y);
    hover = inside;
    if (!inside) pressed = false;
}

bool Button::on_mouse_down(float x, float y) {
    if (!enabled || !bounds.contains(x, y)) return false;
    pressed = true;
    return true;
}

bool Button::on_mouse_up(float x, float y) {
    const bool was_pressed = pressed;
    pressed = false;
    if (!enabled || !was_pressed) return false;
    if (!bounds.contains(x, y)) return false;
    if (on_click) on_click();
    return true;
}

// ── 复选框 ────────────────────────────────────────────────────

bool CheckBox::hit_box(float x, float y, float scale) const {
    // 只有方块本身响应点击是不够的（Win11 里点文字也切换），所以命中区是整行
    (void)scale;
    return bounds.contains(x, y);
}

void CheckBox::on_mouse_move(float x, float y) { hover = enabled && bounds.contains(x, y); }

bool CheckBox::on_click(float x, float y, float scale) {
    if (!enabled || !hit_box(x, y, scale) || value == nullptr) return false;
    *value = !*value;
    if (on_toggle) on_toggle();
    return true;
}

void CheckBox::paint(const DrawContext& ctx, float scale) const {
    if (!ctx.ready() || bounds.empty() || value == nullptr) return;
    const Theme& theme = *ctx.theme;
    const float size = box_size * scale;
    const float top = bounds.top + (bounds.height() - size) * 0.5f;
    const Rect box{bounds.left, top, bounds.left + size, top + size};

    const bool checked = *value;
    ID2D1SolidColorBrush* fill = checked ? ctx.accent : ctx.field;
    // 未勾选时描边要够深：Win11 的复选框边框本来就比较明显，太浅会看不见
    ID2D1SolidColorBrush* outline = checked ? ctx.accent : (hover ? ctx.accent : ctx.muted);

    fill_round(ctx, box, kBoxRadius * scale, fill);
    stroke_round(ctx, box, kBoxRadius * scale, outline, 1.5f * scale);

    if (checked) {
        ID2D1Factory* factory = nullptr;
        ctx.rt->GetFactory(&factory);
        if (factory != nullptr) {
            if (ID2D1PathGeometry* check = make_check_geometry(factory, size, top)) {
                ID2D1StrokeStyle* stroke_style = nullptr;
                const D2D1_STROKE_STYLE_PROPERTIES properties =
                    D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
                if (SUCCEEDED(factory->CreateStrokeStyle(properties, nullptr, 0,
                                                         &stroke_style)) &&
                    stroke_style != nullptr) {
                    ctx.rt->DrawGeometry(check, ctx.accent_text, 2.0f * scale, stroke_style);
                    stroke_style->Release();
                }
                check->Release();
            }
            factory->Release();
        }
    }

    const Rect label{bounds.left + size + 8.0f * scale, bounds.top, bounds.right,
                     bounds.bottom};
    draw_text_utf8(ctx, text, label, FontRole::Small, theme.text);
}

// ── 进度条 ────────────────────────────────────────────────────

void ProgressBar::set_value(float v) {
    v = std::max(0.0f, std::min(1.0f, v));
    if (std::fabs(v - value) < 0.001f) return;
    value = v;
}

void ProgressBar::paint(const DrawContext& ctx) const {
    if (!ctx.ready() || bounds.empty()) return;
    // 圆角半径取半高，就是一个胶囊（和 gui.py 的 radius = height / 2 一致）
    const float radius = bounds.height() * 0.5f;
    fill_round(ctx, bounds, radius, ctx.border);
    const float filled = bounds.width() * value;
    if (filled >= bounds.height()) {
        const Rect inner{bounds.left, bounds.top, bounds.left + filled, bounds.bottom};
        fill_round(ctx, inner, radius, ctx.accent);
    }
}

// ── 滚动条 ────────────────────────────────────────────────────

int ScrollBar::max_offset() const {
    return std::max(0, content_height - view_height);
}

bool ScrollBar::visible() const { return max_offset() > 0 && !bounds.empty(); }

bool ScrollBar::thumb_rect(Rect& out) const {
    if (!visible() || view_height <= 0) return false;
    const float track = bounds.height();
    const float ratio = static_cast<float>(view_height) / static_cast<float>(content_height);
    float height = track * ratio;
    // 太短的滑块不好抓，给一个最小高度（gui.py 的 minimum=32）
    const float minimum = std::min(track, 32.0f);
    height = std::max(height, minimum);
    float top = 0.0f;
    if (max_offset() > 0) {
        top = (track - height) * (static_cast<float>(offset) / static_cast<float>(max_offset()));
    }
    top = std::max(0.0f, std::min(top, track - height));
    out = Rect{bounds.left, bounds.top + top, bounds.right, bounds.top + top + height};
    return true;
}

void ScrollBar::paint(const DrawContext& ctx) const {
    Rect thumb;
    if (!ctx.ready() || !thumb_rect(thumb)) return;   // 内容放得下就不画
    ID2D1SolidColorBrush* brush = active ? ctx.muted : ctx.border;
    const float radius = std::max(0.0f, (bounds.width() - 2.0f) * 0.5f);
    fill_round(ctx, inset(thumb, 1.0f), radius, brush);
}

void ScrollBar::set_offset(int value) {
    offset = std::max(0, std::min(value, max_offset()));
}

bool ScrollBar::scroll_by(int delta) {
    const int before = offset;
    set_offset(offset + delta);
    if (offset == before) return false;
    if (on_scroll) on_scroll(offset);
    return true;
}

bool ScrollBar::on_mouse_down(float x, float y) {
    if (!visible() || !bounds.contains(x, y)) return false;
    Rect thumb;
    if (!thumb_rect(thumb)) return false;
    if (y >= thumb.top && y <= thumb.bottom) {
        drag_grab = y - thumb.top;
    } else {
        // 点在空白处：把滑块中心挪到鼠标位置（和 gui.py 一样，不翻页而是直接跳）
        drag_grab = thumb.height() * 0.5f;
        const float track = bounds.height();
        const float height = thumb.height();
        const float span = std::max(1.0f, track - height);
        const float ratio = std::max(0.0f, std::min(1.0f, (y - bounds.top - drag_grab) / span));
        set_offset(static_cast<int>(std::lround(ratio * max_offset())));
        if (on_scroll) on_scroll(offset);
    }
    dragging = true;
    active = true;
    return true;
}

void ScrollBar::on_mouse_move(float x, float y) {
    active = dragging || bounds.contains(x, y);
    if (!dragging) return;
    Rect thumb;
    if (!thumb_rect(thumb)) return;
    const float track = bounds.height();
    const float height = thumb.height();
    const float span = std::max(1.0f, track - height);
    const float ratio = std::max(0.0f, std::min(1.0f, (y - bounds.top - drag_grab) / span));
    set_offset(static_cast<int>(std::lround(ratio * max_offset())));
    if (on_scroll) on_scroll(offset);
}

void ScrollBar::on_mouse_up() { dragging = false; }

void ScrollBar::on_mouse_leave() {
    if (!dragging) active = false;
}

// ── 下拉框 ────────────────────────────────────────────────────

std::string Dropdown::current_text() const {
    if (index < 0 || index >= static_cast<int>(options.size())) return {};
    return options[static_cast<std::size_t>(index)];
}

void Dropdown::on_mouse_move(float x, float y) { hover = bounds.contains(x, y); }

void Dropdown::paint(const DrawContext& ctx, float scale) const {
    if (!ctx.ready() || bounds.empty()) return;
    const Theme& theme = *ctx.theme;
    fill_round(ctx, bounds, kBoxRadius * scale, hover ? ctx.subtle_hover : ctx.field);
    stroke_round(ctx, bounds, kBoxRadius * scale, ctx.field_border, std::max(1.0f, scale));

    // 右侧留出箭头的宽度，文字不会压到箭头下面
    const float pad = 10.0f * scale;
    const float arrow = 12.0f * scale;
    const Rect label{bounds.left + pad, bounds.top, bounds.right - pad - arrow,
                     bounds.bottom};
    draw_text_utf8(ctx, current_text(), label, FontRole::Small, theme.text);

    const float cx = bounds.right - pad - arrow * 0.5f;
    const float cy = bounds.top + bounds.height() * 0.5f;
    const float half = 2.0f * scale;
    fill_triangle(ctx, D2D1::Point2F(cx - half * 1.5f, cy - half * 0.5f),
                  D2D1::Point2F(cx + half * 1.5f, cy - half * 0.5f),
                  D2D1::Point2F(cx, cy + half), ctx.muted);
}

// ── 文件列表 ──────────────────────────────────────────────────

void paint_file_list(const DrawContext& ctx, const FileList& list,
                     const std::vector<FileRowText>& rows, float scale) {
    if (!ctx.ready() || list.bounds.empty()) return;
    const Theme& theme = *ctx.theme;
    const float frame = std::max(1.0f, scale);

    // 整个列表先铺一层"输入框底色"并框起来。卡片底色和列表底色都是白的，不画框
    // 的话这块区域和卡片留白连成一片，文件少的时候看上去就是"界面没画完"
    // （gui.py 里 ttk.Treeview 自带一圈边框，视觉上正是这个作用）。
    fill_round(ctx, list.bounds, kBoxRadius * scale, ctx.field);

    // ---- 列位置 ----
    // 三列之间必须留缝：右对齐的"大小"和左对齐的"状态"本来就是贴在一起的，
    // 不留缝两个表头会连成"大小状态"，看起来像一个错别字。
    const float pad = 4.0f * scale;
    const float column_gap = 12.0f * scale;
    const float status_left = list.header.right - list.status_width;
    const float size_right = status_left - column_gap;
    const float size_left = size_right - list.size_width;

    // 表头和数据行共用同一组 x 范围。以前表头不带 pad、数据行带，于是每列的表头
    // 都比列内容偏右 4 像素——单看都不明显，一整列对齐看就是歪的。
    const Rect header_text{list.header.left, list.header.top, list.header.right,
                           list.header.bottom - 1.0f};
    const Rect name_head{header_text.left + pad, header_text.top,
                         list.header.left + list.name_width - pad, header_text.bottom};
    const Rect size_head{size_left + pad, header_text.top, size_right - pad, header_text.bottom};
    const Rect status_head{status_left + pad, header_text.top, header_text.right - pad,
                           header_text.bottom};
    draw_text_utf8(ctx, tr("文件名"), name_head, FontRole::SemiboldSmall, theme.muted);
    draw_text_utf8(ctx, tr("大小"), size_head, FontRole::SemiboldSmall, theme.muted,
                   TextAlign::Right);
    draw_text_utf8(ctx, tr("状态"), status_head, FontRole::SemiboldSmall, theme.muted);

    // 表头下面一条分隔线（gui.py 里 Treeview 的 heading 自带的分隔感）
    const Rect rule{list.header.left, list.header.bottom - 1.0f, list.header.right,
                    list.header.bottom};
    fill_rect(ctx, rule, ctx.border);

    // ---- 数据行 ----
    for (int index = 0; index < static_cast<int>(rows.size()); ++index) {
        const float top = list.body.top + index * static_cast<float>(list.row_height) -
                          static_cast<float>(list.scroll.offset);
        const Rect row{list.body.left, top, list.body.right,
                       top + static_cast<float>(list.row_height)};
        if (row.bottom <= list.body.top || row.top >= list.body.bottom) continue;  // 省点力气

        const FileRowText& item = rows[static_cast<std::size_t>(index)];
        if (item.selected) {
            fill_rect(ctx, row, ctx.selected);
        } else if (index == list.hover_row) {
            // 悬停行：用次要底色轻轻带一下，不抢选中行的强调色
            fill_rect(ctx, row, ctx.subtle_hover);
        }

        // 行裁剪：滚动时半行不该画到表头或提示文字上面去
        ctx.rt->PushAxisAlignedClip(
            D2D1::RectF(list.body.left, list.body.top, list.body.right, list.body.bottom),
            D2D1_ANTIALIAS_MODE_ALIASED);
        const Rect name_cell{name_head.left, row.top, name_head.right, row.bottom};
        const Rect size_cell{size_head.left, row.top, size_head.right, row.bottom};
        const Rect status_cell{status_head.left, row.top, status_head.right, row.bottom};
        draw_text_utf8(ctx, item.name, name_cell, FontRole::Small, theme.text);
        draw_text_utf8(ctx, item.size, size_cell, FontRole::Small, theme.text,
                       TextAlign::Right);
        draw_text_utf8(ctx, item.status, status_cell, FontRole::Small, theme.text);
        ctx.rt->PopAxisAlignedClip();
    }

    list.scroll.paint(ctx);

    // 边框最后画：选中行/悬停行是整行铺色，先画框就会被它们盖掉一条边。
    stroke_round(ctx, list.bounds, kBoxRadius * scale, ctx.field_border, frame);

    draw_text_utf8(ctx, tr("双击列表项可打开它所在的文件夹"), list.hint, FontRole::Small,
                   theme.muted);
}

}  // namespace pdfc::gui
