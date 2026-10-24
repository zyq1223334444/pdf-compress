// -*- coding: utf-8 -*-
//
// 自绘控件的"模型 + 绘制"小工具集。
//
// 这里没有窗口，也没有消息循环：每个控件只是一个普通的 C++ 结构体，装着自己的
// 矩形与状态，由 window.cpp 的窗口过程把鼠标/键盘消息翻译成对它们的调用。这样做
// 的理由是这套界面里除了输入框和日志窗（那两个必须是原生 EDIT，否则中文输入法
// 会出问题）以外的所有元件都是画出来的，如果每个控件再建一个 HWND，光消息转发
// 和子类化就要写一堆胶水，还得自己处理 Z 序和重绘，得不偿失。
//
// 绘制全部走 Direct2D：圆角一律用 FillRoundedRectangle/DrawRoundedRectangle，
// 文字一律用 DirectWrite。控件自己不持有任何 D2D 资源——画刷由 window.cpp 统一
// 管理（换主题时一次性重建），控件只从 DrawContext 里取，避免每帧 CreateSolidColorBrush。
//
// 关于颜色：控件只认"角色"（强调、次要、边框…），不认具体色值，色值全部来自
// theme.hpp 的 Theme。这样深浅两套主题、以及将来再调色，都只改 theme.cpp 一处。

#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "gui/theme.hpp"

namespace pdfc::gui {

// ── 矩形小工具（D2D1_RECT_F 的 left/top/right/bottom 写法太长） ──

struct Rect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    float width() const { return right - left; }
    float height() const { return bottom - top; }
    bool empty() const { return right <= left || bottom <= top; }

    bool contains(float x, float y) const {
        return x >= left && x < right && y >= top && y < bottom;
    }
};

Rect make_rect(float left, float top, float right, float bottom);
Rect inset(const Rect& rect, float amount);
Rect offset(const Rect& rect, float dx, float dy);

/// 纵向切一刀：上半部分给 out_top，下半部分给 out_rest（gap 是两者之间的空隙）。
void split_top(const Rect& rect, float height, float gap, Rect& out_top, Rect& out_rest);
/// 横向切一刀：左边固定宽度给 out_left，剩下的给 out_right。
void split_left(const Rect& rect, float width, float gap, Rect& out_left, Rect& out_right);
/// 横向切一刀：右边固定宽度给 out_right，剩下的给 out_left。
void split_right(const Rect& rect, float width, float gap, Rect& out_left, Rect& out_right);
/// 从顶部取出高度为 height 的一条，其余留给 out_rest（用于表单逐行往下排）。
Rect take_top(const Rect& rect, float height, Rect& out_rest);
/// 从底部取出高度为 height 的一条。
Rect take_bottom(const Rect& rect, float height, Rect& out_rest);
/// 从右边取出宽度为 width 的一条，其余留给 out_rest。
Rect take_right(const Rect& rect, float width, Rect& out_rest);
/// 从左边取出宽度为 width 的一条，其余留给 out_rest。
Rect take_left(const Rect& rect, float width, Rect& out_rest);

/// 把 D2D1_RECT_F 看成 Rect（两者布局完全相同，纯粹为了少写字）。
inline Rect as_rect(const D2D1_RECT_F& value) {
    return Rect{value.left, value.top, value.right, value.bottom};
}

// ── 绘制上下文 ────────────────────────────────────────────────

/// 一次 BeginDraw/EndDraw 之间用到的画笔与字体。window.cpp 持有，按引用传递。
struct DrawContext {
    ID2D1RenderTarget* rt = nullptr;
    IDWriteFactory* dwrite = nullptr;
    const FontSet* fonts = nullptr;
    const Theme* theme = nullptr;

    // 常用色画刷（换主题时由 window.cpp 重建）
    ID2D1SolidColorBrush* bg = nullptr;
    ID2D1SolidColorBrush* card = nullptr;
    ID2D1SolidColorBrush* border = nullptr;
    ID2D1SolidColorBrush* text = nullptr;
    ID2D1SolidColorBrush* muted = nullptr;
    ID2D1SolidColorBrush* accent = nullptr;
    ID2D1SolidColorBrush* accent_text = nullptr;
    ID2D1SolidColorBrush* hover = nullptr;
    ID2D1SolidColorBrush* subtle = nullptr;
    ID2D1SolidColorBrush* subtle_hover = nullptr;
    ID2D1SolidColorBrush* field = nullptr;
    ID2D1SolidColorBrush* field_border = nullptr;
    ID2D1SolidColorBrush* selected = nullptr;
    ID2D1SolidColorBrush* log_bg = nullptr;

    bool ready() const { return rt != nullptr && fonts != nullptr && theme != nullptr; }
};

/// 文本对齐方式。等价于 Tk 的 anchor="w"/"center"/"e"。
enum class TextAlign { Left, Center, Right };

/// 文本在矩形里的垂直位置。
///
/// DirectWrite 默认是顶对齐，而 Tk 的 Label、表格单元格都是"在行高里居中"。
/// 行高只比文字高一点点时两种排法看不出区别，但表单每一行、列表每一行都留了
/// 五六像素的上下留白，顶对齐就会整体偏上——右边紧挨着的原生 EDIT 控件是居中的，
/// 两者一并排就成了用户嘴里的"输入框错位"。默认居中就是为了和它们对齐。
enum class TextVAlign { Top, Center };

/// 按钮的两种外观。Accent 是"强调色实心"，Subtle 是"浅底 + 边框"。
/// 放在这里是因为后面好几个结构体（按钮、询问条、以及 window.cpp 的布局）
/// 都要在 Button 定义之前用到它。
enum class ButtonKind {
    Accent,    // 主按钮：强调色实心
    Subtle,    // 次要按钮：浅底 + 边框
};

/// 画一行文字。text 是 UTF-8（本项目内部统一 UTF-8，只有进 Win32/DirectWrite
/// 的那一刻转宽字符），矩形按 align 决定水平位置，垂直方向居中。
/// 超长时按字符裁剪（D2D1_DRAW_TEXT_OPTIONS_CLIP），不会溢出到别的控件上。
void draw_text(const DrawContext& ctx, const std::wstring& text, const Rect& rect,
               FontRole role, const D2D1_COLOR_F& color, TextAlign align = TextAlign::Left,
               TextVAlign valign = TextVAlign::Center);

/// 同上，输入是 UTF-8。
void draw_text_utf8(const DrawContext& ctx, const std::string& text, const Rect& rect,
                    FontRole role, const D2D1_COLOR_F& color,
                    TextAlign align = TextAlign::Left,
                    TextVAlign valign = TextVAlign::Center);

/// 按矩形宽度**折行**画一段文字（长提示、询问条上的问题）。
///
/// 和 draw_text 的区别只有一处：绘制前把文本格式的换行方式临时改成 WRAP。
/// 格式是全局共享的，所以必须成对设置/还原，否则会串到别的控件上。
/// 需要它的原因很具体：默认的 NO_WRAP 遇到超长提示会一路画到卡片外面再被裁掉，
/// 看上去就是半句话——而量高度时用的也是同一个 NO_WRAP 格式，于是"量出来只有
/// 一行"，两个错误刚好互相掩盖。
void draw_text_utf8_wrapped(const DrawContext& ctx, const std::string& text, const Rect& rect,
                            FontRole role, const D2D1_COLOR_F& color,
                            TextAlign align = TextAlign::Left,
                            TextVAlign valign = TextVAlign::Center);

/// 量一行文字要占多宽（用于按钮自适应宽度、提示文字折行判断）。
float measure_text(const DrawContext& ctx, const std::wstring& text, FontRole role);

/// 一次量出 (宽, 高)。high 是按"内容自然高度"排出来的行高，会把字体回退
/// （比如拉丁字族缺中文字形时链接到微软雅黑）带来的行距变化算进去，
/// 所以比拿单个字体的 ascent+descent+lineGap 再乘系数准。
struct TextExtent {
    float width = 0.0f;
    float height = 0.0f;
};
TextExtent measure_text_extent(const DrawContext& ctx, const std::wstring& text,
                               FontRole role, float max_width = 100000.0f,
                               bool wrap = false);

/// 圆角矩形填充；radius 会自动收敛到不超过半高/半宽（D2D1 不接受更大的半径）。
void fill_round(const DrawContext& ctx, const Rect& rect, float radius,
                ID2D1SolidColorBrush* brush);
/// 圆角矩形描边。
void stroke_round(const DrawContext& ctx, const Rect& rect, float radius,
                  ID2D1SolidColorBrush* brush, float width = 1.0f);

// ── 文字输入框的模型 ──────────────────────────────────────────
//
// 控件本体是原生 EDIT（见 window.cpp 的 create_edit），这里只放"它的外观参数"，
// 由一个函数统一创建，免得六个输入框各写一遍 CreateWindowEx。

struct TextField {
    Rect bounds;              // 在父窗口客户区里的位置
    HWND hwnd = nullptr;      // 原生 EDIT 子控件
    bool read_only = false;
    bool multiline = false;
    bool want_vscroll = false;
    float radius = kBoxRadius;

    // clip_round_region 的缓存。sync_native_controls 每帧都会走到，尺寸没变就
    // 别重设区域——SetWindowRgn 会让窗口重算一次可见区，白调用只会闪。
    int clip_width = -1;
    int clip_height = -1;
    float clip_radius = -1.0f;
};

/// 创建叠放在卡片上的原生 EDIT。
///
/// 必须是真正的 EDIT：自绘文本框在中文输入法下拿不到候选窗口的位置和组字串，
/// 会出现"打字看不见候选框""上屏后才刷新"这类问题。这是这套界面里唯一不能
/// 图省事的地方，所以输入框和日志窗都用原生控件。
HWND create_edit(HWND parent, HINSTANCE instance, const Rect& bounds, bool multiline,
                 bool read_only, DWRITE_FONT_WEIGHT weight, float font_size,
                 int margin_x, int margin_y);

/// 把 EDIT 的文本换成 UTF-8（内部会做宽窄转换，并保留滚动位置）。
void set_edit_text(HWND edit, const std::string& text);
/// 读出 EDIT 的全部文本（UTF-8）。
std::string get_edit_text(HWND edit);

/// 把子控件的窗口区域裁成圆角矩形（radius 是物理像素，<= 0 表示恢复成矩形）。
///
/// 原生 EDIT 刷底色时四个角永远是直角，而外框的圆角是父窗口用 Direct2D 画的。
/// 不裁的话 EDIT 的直角会切进圆角边框的弧线里，看上去就是"外圈圆、里面方"。
/// 区域用的是子控件自己的客户区坐标，所以每次尺寸变化都要重设一遍。
void clip_round_region(HWND hwnd, float radius);

// ── 按钮 ──────────────────────────────────────────────────────

struct Button {
    Rect bounds;
    std::string text;
    ButtonKind kind = ButtonKind::Subtle;
    bool enabled = true;
    bool hover = false;
    bool pressed = false;
    float radius = kButtonRadius;
    FontRole role = FontRole::SemiboldSmall;
    std::function<void()> on_click;

    void paint(const DrawContext& ctx, float scale) const;
    void on_mouse_move(float x, float y);
    bool on_mouse_down(float x, float y);
    /// 返回是否真的触发了 on_click（按下与抬起都在按钮内才算点击）。
    bool on_mouse_up(float x, float y);
    void set_text(std::string value) { text = std::move(value); }
    void set_enabled(bool value) {
        if (enabled == value) return;
        enabled = value;
        hover = false;
        pressed = false;
    }
};

// ── 复选框 ────────────────────────────────────────────────────

struct CheckBox {
    Rect bounds;
    std::string text;
    bool* value = nullptr;    // 指向 window.cpp 里的状态字段（与 Tk 的变量等价）
    bool enabled = true;
    bool hover = false;
    float box_size = 18.0f;   // 96 DPI 下的边长，画的时候乘缩放
    std::function<void()> on_toggle;

    void paint(const DrawContext& ctx, float scale) const;
    void on_mouse_move(float x, float y);
    bool hit_box(float x, float y, float scale) const;
    /// 点中时翻转并回调；返回是否处理了这次点击。
    bool on_click(float x, float y, float scale);
};

// ── 进度条 ────────────────────────────────────────────────────

struct ProgressBar {
    Rect bounds;
    float value = 0.0f;       // 0..1

    void set_value(float v);
    void paint(const DrawContext& ctx) const;
};

// ── 细滚动条 ──────────────────────────────────────────────────
//
// 和 gui.py 的 ThinScrollbar 一样是自绘的：系统滚动条在深色主题下颜色对不上，
// 而且带箭头的老样式和 Win11 不搭。内容放得下时干脆不画（visible() 为假）。

struct ScrollBar {
    Rect bounds;
    int content_height = 0;   // 内容总高（像素）
    int view_height = 0;      // 可视高度（像素）
    int offset = 0;           // 当前滚动偏移（像素，0..max_offset）
    bool active = false;      // 悬停或拖动中，滑块用更亮的颜色
    bool dragging = false;
    float drag_grab = 0.0f;   // 按下时鼠标相对滑块顶部的偏移
    std::function<void(int)> on_scroll;   // 参数是新的 offset

    int max_offset() const;
    bool visible() const;
    bool thumb_rect(Rect& out) const;
    void paint(const DrawContext& ctx) const;
    bool scroll_by(int delta);
    void set_offset(int value);
    /// 返回 true 表示这次点击已经被滚动条吃掉（不要再去处理列表的选择）。
    bool on_mouse_down(float x, float y);
    void on_mouse_move(float x, float y);
    void on_mouse_up();
    void on_mouse_leave();
};

// ── 下拉框（模式 / 策略） ─────────────────────────────────────
//
// 不用 COMBOBOX：它的下拉列表是系统画的，深浅主题、字号、圆角都改不动，和这张
// 卡片放在一起会明显"不是一家人"。这里画一个显示当前值的按钮，点击时由
// window.cpp 用 TrackPopupMenu 弹出真正的系统菜单（这样才有系统级的键盘导航、
// 屏幕边缘翻转和 DPI 缩放），选中后回调。

struct Dropdown {
    Rect bounds;
    std::vector<std::string> options;      // 已翻译好的显示文本
    int index = 0;
    bool hover = false;
    std::function<void(int)> on_select;

    std::string current_text() const;
    void paint(const DrawContext& ctx, float scale) const;
    void on_mouse_move(float x, float y);
};

// ── 文件列表 ──────────────────────────────────────────────────

struct FileList {
    Rect bounds;              // 整块列表（含表头）的位置
    Rect header;              // 表头行
    Rect body;                // 数据行区域
    Rect hint;                // 下面那行灰色提示
    float name_width = 0.0f;  // 文件名列宽（伸展）
    float size_width = 0.0f;  // 大小列宽（固定，右对齐）
    float status_width = 0.0f;// 状态列宽（固定）
    int row_height = 26;
    int hover_row = -1;
    ScrollBar scroll;
    std::function<void(int)> on_click;         // 参数：被点中的行号
    std::function<void(int)> on_double_click;  // 参数：被双击的行号
};

/// 列表要用到的三列文本（由 window.cpp 从文件记录里拼好再传进来，
/// 免得 widgets 认识 filesystem 和 i18n）。
struct FileRowText {
    std::string name;
    std::string size;
    std::string status;
    bool selected = false;
};

void paint_file_list(const DrawContext& ctx, const FileList& list,
                     const std::vector<FileRowText>& rows, float scale);

}  // namespace pdfc::gui
