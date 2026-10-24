// -*- coding: utf-8 -*-
//
// 主窗口的实现：布局、自绘、原生子控件、工作线程桥。
//
// 这份代码对应 Python 版 pdfcomp/gui.py 的 App 类，界面的每一个尺寸、配色、
// 文案都从那里逐条搬过来（用户已经看过 Python 版的截图并认可它的外观）。
// 分几个部分看：
//
//   1. 窗口与 DPI：1080×720（逻辑）居中、可缩放、最小 880×560，所有像素乘
//      dpi/96。进程已经被 platform_win.cpp 声明为系统 DPI 感知，系统不会替我们
//      放大窗口，所以这个乘法必须自己来，而且只能乘一次。
//   2. 布局：从外到内一路切矩形的纯计算（compute_layout 算、paint 用），
//      没有嵌套容器。"换语言/换主题整棵树重建"因此就是把子控件销毁、重算一遍、
//      再把保存的状态填回去。
//   3. 绘制：Direct2D 画一切（圆角矩形 + DirectWrite 文字）。只有两类东西是
//      原生子控件——叠在卡片上的输入框，以及日志窗。
//   4. 交互：列表、按钮、复选框、下拉、细滚动条全是自绘的，鼠标消息都在这里
//      分发；键盘靠 IsDialogMessageW 提供 Tab 导航，回车/Esc 自己处理。
//   5. 线程：工作线程只发消息、只在询问时等待，绝不碰窗口、绝不读控件。
//
// 为什么输入框一定要用原生 EDIT：中文输入法（IME）在自绘文本框上拿不到候选
// 窗口的锚点，也拿不到组字中的字符串，表现是"打字时看不见候选框""拼音上屏后
// 光标乱跳"。EDIT 是系统控件，IME 那一整套协议它自己处理好了，我们只负责把
// 底色和文字颜色对齐主题（WM_CTLCOLOREDIT / WM_CTLCOLORSTATIC）。

#include "gui/window.hpp"

// 头文件顺序：标准库 → Windows/Direct2D → 本项目。
// windows.h 必须先于 commctrl.h / shellapi.h / dwmapi.h；MinGW 上 commctrl.h
// 会 #error 要求先包含 windows.h，MSVC 不强制但顺序统一更好读。
#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <utility>

#include <windows.h>
#include <commctrl.h>   // SetWindowSubclass / DefSubclassProc
#include <dwmapi.h>     // DwmSetWindowAttribute（标题栏深色）
#include <shellapi.h>   // ShellExecuteW（双击列表项打开所在文件夹）

#include "common.hpp"
#include "config.hpp"
#include "filedialog.hpp"
#include "i18n.hpp"

// 链接依赖：CMakeLists.txt 里没有列 comctl32（子类化）和 shellapi 对应的
// shell32（ShellExecuteW）。这两个都是 Windows 自带的系统库，不是第三方依赖，
// 用 pragma comment 就地声明，省得为了两个系统库去动构建脚本。
// 如果更希望在 CMakeLists 的 target_link_libraries 里显式列出，删掉这两行、
// 在那里加上 comctl32 即可。
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace pdfc::gui {
namespace {

// ── 96 DPI 下的布局常量 ──────────────────────────────────────
// 数字全部来自 gui.py：容器 padx=18/pady=14、卡片 pad=14、右列固定 392、
// 卡片间距 12、列表行高 26（Tk 的 rowheight）、进度条高 6、复选框行高 24……

/// EndDraw 连着失败这么多帧就认定 Direct2D 在这台机器上用不了，弹窗告知。
/// 一帧失败是常见的启动期现象（渲染目标刚建好、设备还没就绪），重建一次就好。
constexpr int kPaintFailureLimit = 5;

/// HRESULT 的十六进制写法，放进给用户看的说明里
std::string hex_code(HRESULT result) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(result));
    return std::string(buffer);
}

constexpr float kRootPadX = 18.0f;
constexpr float kRootPadY = 14.0f;
constexpr float kRightWidth = 392.0f;   // 右列逻辑宽度
constexpr float kColumnGap = 12.0f;     // 左右列之间
constexpr float kCardPad = 14.0f;
constexpr float kCardGap = 12.0f;       // 同列两张卡片之间
constexpr float kRowHeight = 26.0f;     // 列表行高
constexpr float kListRows = 5.0f;       // 列表可见行数（Tk 的 height=5）
constexpr float kFloatGap = 12.0f;      // 语言按钮与主题按钮之间
constexpr float kSmallGap = 6.0f;
constexpr float kHeaderButtonHeight = 30.0f;
constexpr float kHeadButtonHeight = 28.0f;
constexpr float kNoteGap = 10.0f;       // 复选框与提示文字之间
constexpr float kCheckTopGap = 10.0f;   // 表单与复选框之间
constexpr float kCheckGap = 2.0f;       // 复选框之间
constexpr float kLabelGap = 16.0f;      // 左列标签与右列标签之间的距离
constexpr float kGridPadY = 5.0f;       // 表单每行上下留白
constexpr float kGridPadX = 8.0f;       // 单元格右侧留白
constexpr float kProgressGap = 10.0f;   // 卡片标题与进度条之间
constexpr float kButtonPadXSmall = 12.0f;
constexpr float kStartHeight = 36.0f;
constexpr float kCancelHeight = 32.0f;
constexpr float kInputPadX = 8.0f;      // EDIT 控件里的左右留白
constexpr float kDotsWidth = 34.0f;     // 输出目录右边那个 "…" 按钮
constexpr float kHeadingGap = 8.0f;     // 卡片标题与表单之间
constexpr float kTitleLineHeight = 30.0f;
constexpr float kSubtitleLineHeight = 18.0f;
constexpr float kCheckHeight = 24.0f;
/// 单行输入框的高度（逻辑像素）。表单每行 = 输入框 + 上下留白，所以它也决定了
/// 整张设置卡片有多高。
///
/// 为什么不能直接用"一行文字的高度"：EDIT 控件内部还有自己的边距，框子只有
/// 15 逻辑像素高而字号是 12 时，字会被裁掉一截（看上去就是"输入框错位"）。
constexpr float kInputHeight = 24.0f;
constexpr float kScrollWidth = 10.0f;
constexpr float kMinLogHeight = 140.0f;

/// 压缩设置表单的九个标签（中文原文，绘制时经 tr 翻译），顺序即版面顺序。
///
/// layout_widgets 和 paint_grid 共用这一份：标签列宽要按当前语言实测，写死宽度
/// 会让英文标签被输入框截掉后半截，中文却刚好放得下。
const char* const kGridLabelKeys[] = {
    "目标大小 (MB)", "固定 DPI（可留空）", "输出目录",         "模式",
    "策略",          "最大渲染次数",       "渲染进程数",       "搜索上限 (DPI)",
    "搜索下限 (DPI)",
};

constexpr wchar_t kWindowClass[] = L"PdfCompressGuiWindow";
// 子控件用的控件 ID：只需要唯一，窗口过程靠 HWND 分辨是谁。
constexpr int kEditId = 1001;
constexpr UINT_PTR kEditSubclassId = 1;
constexpr int kTimerDrain = 1;

// 自定义消息。kMsgWorker（工作线程 -> 界面线程）定义在 window.hpp 里。
constexpr UINT kMsgStartJobs = WM_APP + 3;   // EDIT 子类转发过来的"回车=开始"

bool has_ctrl() { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }
bool has_shift() { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }

int pixel(float value) { return static_cast<int>(std::lround(value)); }

COLORREF to_colorref(const D2D1_COLOR_F& color) {
    const auto channel = [](float value) {
        return static_cast<BYTE>(std::lround(std::max(0.0f, std::min(1.0f, value)) * 255.0f));
    };
    return RGB(channel(color.r), channel(color.g), channel(color.b));
}

/// 一次量出 (宽, 高)。走 widgets 的 measure_text_extent：它内部建
/// IDWriteTextLayout 再取 DWRITE_TEXT_METRICS，所以会把字体回退
/// （拉丁字族缺中文字形时链接到微软雅黑，行距会变）算进去——比拿单个字体的
/// ascent+descent+lineGap 再乘系数准，也比自己拼 TextFormat 靠谱。
TextExtent text_measure(const DrawContext& ctx, const std::string& text, FontRole role,
                        float max_width = 100000.0f) {
    if (!ctx.ready() || text.empty()) return TextExtent{};
    return measure_text_extent(ctx, utf8_to_wide(text), role, max_width);
}

/// 已知宽度上限时，这段文字要占多高（提示文字会折行，用得上）。
/// 内部按 wrap=true 量，和 draw_text_utf8_wrapped 的绘制方式一致。
float text_height(const DrawContext& ctx, const std::string& text, float width,
                  FontRole role) {
    if (!ctx.ready() || text.empty()) return 0.0f;
    return measure_text_extent(ctx, utf8_to_wide(text), role, width, /*wrap=*/true).height;
}

/// 把相对路径变成绝对路径（列表去重、比较都用绝对路径，与 gui.py 的
/// os.path.abspath 一致）。
std::string absolute_path(const std::string& path) {
    const std::wstring wide = utf8_to_wide(path);
    if (wide.empty()) return path;
    const DWORD needed = GetFullPathNameW(wide.c_str(), 0, nullptr, nullptr);
    if (needed == 0) return path;
    std::wstring buffer(static_cast<std::size_t>(needed) + 1, L'\0');
    const DWORD written = GetFullPathNameW(wide.c_str(), static_cast<DWORD>(buffer.size()),
                                           buffer.data(), nullptr);
    if (written == 0) return path;
    buffer.resize(written);
    return wide_to_utf8(buffer);
}

/// EDIT 子类：接住回车与 Esc，并把滚轮转给父窗口（父窗口再决定是滚列表还是
/// 滚日志窗）。IBE 相关的组字消息一概不动，那是系统控件自己的事。
LRESULT CALLBACK edit_subclass_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                    UINT_PTR /*id*/, DWORD_PTR ref) {
    (void)ref;
    switch (message) {
        case WM_KEYDOWN:
            if (wparam == VK_RETURN && (GetWindowLongW(hwnd, GWL_STYLE) & ES_MULTILINE) == 0) {
                // IsDialogMessageW 会把单行 EDIT 里的回车当成"按下默认按钮"吃掉，
                // 而这里根本没有默认按钮，所以自己转成"开始压缩"。
                PostMessageW(GetParent(hwnd), kMsgStartJobs, 0, 0);
                return 0;
            }
            if (wparam == VK_ESCAPE) {
                PostMessageW(GetParent(hwnd), WM_KEYDOWN, VK_ESCAPE, 0);
                return 0;
            }
            break;
        case WM_CHAR:
            if ((wparam == VK_RETURN || wparam == VK_TAB) &&
                (GetWindowLongW(hwnd, GWL_STYLE) & ES_MULTILINE) == 0) {
                return 0;   // 别让系统"叮"一声
            }
            break;
        case WM_MOUSEWHEEL:
            // 滚轮默认发给焦点控件：日志窗（多行）自己会滚；其它 EDIT 拿到的
            // 滚轮我们转给父窗口，好让鼠标停在列表上时列表能滚。
            SendMessageW(GetParent(hwnd), WM_MOUSEWHEEL, wparam, lparam);
            if ((GetWindowLongW(hwnd, GWL_STYLE) & ES_MULTILINE) == 0) return 0;
            break;
        case WM_DROPFILES:
            // 拖到输入框/日志窗上时消息落在这个子控件上。拖放的目标是文件列表，
            // 所以原样转给父窗口（HDROP 由父窗口负责 DragFinish，这里不能释放）。
            PostMessageW(GetParent(hwnd), WM_DROPFILES, wparam, lparam);
            return 0;
        case WM_GETDLGCODE:
            if ((GetWindowLongW(hwnd, GWL_STYLE) & ES_MULTILINE) != 0) {
                return DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTMESSAGE;
            }
            return DLGC_WANTCHARS | DLGC_WANTARROWS;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(GetParent(hwnd), nullptr, FALSE);
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

}  // namespace

// ── 构造与销毁 ────────────────────────────────────────────────

App::App() {
    theme_ = &light_theme();
    refresh_status_text();
}

App::~App() {
    // 工作线程必须先收干净：它可能正在等询问的回答，而回答要靠窗口过程置位。
    {
        std::lock_guard<std::mutex> lock(ask_mutex_);
        ask_answered_ = true;
    }
    ask_answer_ready_.notify_all();
    cancel_requested_ = true;
    if (worker_.joinable()) worker_.join();

    destroy_native_controls();
    destroy_brushes();
    if (target_ != nullptr) {
        target_->Release();
        target_ = nullptr;
    }
    if (dwrite_ != nullptr) {
        dwrite_->Release();
        dwrite_ = nullptr;
    }
    if (d2d_factory_ != nullptr) {
        d2d_factory_->Release();
        d2d_factory_ = nullptr;
    }
    fonts_.destroy();

    // 队列里可能还剩没处理的消息（窗口已销毁，消息到不了窗口过程），
    // 载荷是 new 出来的，必须在这里收掉。
    std::lock_guard<std::mutex> lock(queue_mutex_);
    for (Message* message : queue_) delete message;
    queue_.clear();
}

// ── 窗口过程 ──────────────────────────────────────────────────

LRESULT CALLBACK App::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    App* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<App*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self != nullptr) self->hwnd_ = hwnd;
    }
    if (self != nullptr) return self->handle_message(hwnd, message, wparam, lparam);
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

// ── 启动与消息循环 ────────────────────────────────────────────

int App::Run(const std::vector<std::string>& files) {
    // 必须在建窗口之前声明 DPI 感知，否则高 DPI 下整个界面会被系统拉糊
    enable_dpi_awareness();

    Settings settings;
    settings.load();
    set_language(detected_language());
    theme_ = &theme_by_name(settings.get("theme", "light").c_str());
    load_preferences(settings);
    for (const std::string& path : files) add_file(path);

    // 字体与 D2D 工厂要在算布局之前就绪（布局里的行高、按钮宽度都要量文字）。
    // 此时还没有窗口，量不到内容也不会崩：ensure_render_target 里没有 hwnd 就返回。
    const UINT system_dpi = GetDpiForSystem();
    const float scale = static_cast<float>(system_dpi) / 96.0f;

    // 字号是在 FontSet::create 里乘 scale_ 得到的，所以 scale_ 必须先定下来。
    // 这里踩过一次坑：先建字体（那时 scale_ 还是 1）再赋 scale_，等窗口建好时
    // fonts_ready_ 已经是 true，永远不会用正确的缩放重建——结果高 DPI 下自绘的
    // 文字比原生 EDIT 里的字小整整一半（EDIT 用的是自己的 12*scale_）。
    scale_ = scale;
    build_fonts();

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    // CS_DBLCLKS 必须有：没有它系统会把第二次点击继续当成 WM_LBUTTONDOWN，
    // WM_LBUTTONDBLCLK 永远收不到，"双击列表项打开所在文件夹"就成了死功能。
    window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    window_class.lpfnWndProc = &App::window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = nullptr;   // 全部自绘，别让系统擦背景（会闪）
    window_class.lpszClassName = kWindowClass;
    RegisterClassExW(&window_class);

    // 客户区 1080×720（逻辑）→ 乘缩放 → 再按屏幕 94%×86% 收口。
    //
    // 注意这里"按屏幕收口"收的是客户区：gui.py 用的是 root.geometry()，Tk 的
    // winfo_screenwidth 比较的是窗口外框，两边的口径略有差别。这里选择让
    // 客户区（真正放界面的地方）不超过屏幕，宁可外框稍微超出一点点。
    const int logical_width = pixel(1080.0f * scale);
    const int logical_height = pixel(720.0f * scale);
    const int screen_w = GetSystemMetrics(SM_CXSCREEN);
    const int screen_h = GetSystemMetrics(SM_CYSCREEN);

    const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;

    // 窗口边框占了多大：AdjustWindowRectEx 对 (0,0,0,0) 的结果就是纯边框尺寸。
    RECT frame{0, 0, 0, 0};
    AdjustWindowRectEx(&frame, style, FALSE, 0);
    const int frame_w = frame.right - frame.left;
    const int frame_h = frame.bottom - frame.top;

    // 收口收的是**外框**，不是客户区。按客户区收再补边框，小屏幕上外框会超出
    // 屏幕宽度（1280 宽的屏上算出过 1295 宽的外框），右边那列控件直接被切掉。
    const int max_outer_w = static_cast<int>(screen_w * 0.94f);
    const int max_outer_h = static_cast<int>(screen_h * 0.86f);
    const int client_w = std::min(logical_width, std::max(pixel(640.0f * scale), max_outer_w - frame_w));
    const int client_h = std::min(logical_height, std::max(pixel(480.0f * scale), max_outer_h - frame_h));

    RECT client_rect{0, 0, client_w, client_h};
    AdjustWindowRectEx(&client_rect, style, FALSE, 0);
    const int outer_w = std::min<int>(client_rect.right - client_rect.left, screen_w);
    const int outer_h = std::min<int>(client_rect.bottom - client_rect.top, screen_h);

    const int x = std::max(0, (screen_w - outer_w) / 2);
    const int y = std::max(0, std::min((screen_h - outer_h) / 3,
                                       screen_h - outer_h - pixel(40.0f * scale)));

    // 最小尺寸：客户区逻辑 880×560 换算成外框。WS_OVERLAPPEDWINDOW +
    // AdjustWindowRectEx 给的是"最小外框"，WM_GETMINMAXINFO 要的正好是外框，
    // 两边口径一致。绝不能大于初始外框：屏幕很小时"最小 > 初始"会让窗口
    // 一出现就被系统撑大。
    RECT minimum{0, 0, pixel(880.0f * scale), pixel(560.0f * scale)};
    AdjustWindowRectEx(&minimum, style, FALSE, 0);
    min_width_ = std::min<int>(minimum.right - minimum.left, outer_w);
    min_height_ = std::min<int>(minimum.bottom - minimum.top, outer_h);

    const std::wstring title = utf8_to_wide(std::string(kAppName) + " " + kVersion);
    hwnd_ = CreateWindowExW(0, kWindowClass, title.c_str(), style, x, y, outer_w, outer_h,
                            nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) return 1;
    // 接受从资源管理器拖进来的文件（WM_DROPFILES）。子控件在 create_edit 里
    // 各自也注册了一份，拖到输入框上也能落到文件列表里（见 edit_subclass_proc）。
    DragAcceptFiles(hwnd_, TRUE);

    dpi_ = GetDpiForWindow(hwnd_);
    if (dpi_ == 0) dpi_ = system_dpi;
    scale_ = static_cast<float>(dpi_) / 96.0f;
    if (!fonts_ready_) build_fonts();

    compute_layout();
    sync_native_controls();
    apply_titlebar_theme();
    restore_inputs();
    sync_widget_texts();
    refresh_buttons();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    UpdateWindow(hwnd_);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // IsDialogMessageW 提供 Tab / Shift+Tab 在 EDIT 之间跳转，以及把方向键
        // 交给想要它的控件。它不是模态循环，只是替换 TranslateMessage/Dispatch。
        if (!IsDialogMessageW(hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}

// ── 字体与 D2D 资源 ───────────────────────────────────────────

void App::build_fonts() {
    if (dwrite_ == nullptr) {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(&dwrite_)))) {
            return;
        }
    }
    fonts_ready_ = fonts_.create(dwrite_, scale_);
    ctx_.dwrite = dwrite_;
    ctx_.fonts = &fonts_;
    ctx_.theme = theme_;
}

bool App::ensure_render_target() {
    if (target_ != nullptr) return true;
    if (hwnd_ == nullptr) {
        init_error_ = "no window yet";
        return false;
    }
    if (d2d_factory_ == nullptr) {
        const HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_);
        if (FAILED(hr)) {
            init_error_ = "D2D1CreateFactory " + hex_code(hr);
            return false;
        }
    }
    if (dwrite_ == nullptr) build_fonts();
    if (!fonts_ready_) {
        init_error_ = "DirectWrite text formats";
        return false;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);
    const D2D1_SIZE_U size =
        D2D1::SizeU(static_cast<UINT32>(std::max<LONG>(1, client.right)),
                    static_cast<UINT32>(std::max<LONG>(1, client.bottom)));
    // dpiX/dpiY 必须显式写 96，不能像通常那样留 0。
    //
    // 留 0 的语义不是"用默认 96"，而是"用**窗口所在显示器的 DPI**"：这台机器是
    // 150%，D2D 于是把所有绘制坐标又乘了一次 1.5。而布局里的每个尺寸和字号都
    // 已经自己乘过 scale_ 了（见 layout_widgets / build_fonts），结果就是整个
    // 界面被放大 1.5 倍：右边那列被推出窗口，原生 EDIT 控件（用 SetWindowPos
    // 摆的、坐标没被 D2D 放大）和画出来的卡片因此对不上。
    D2D1_RENDER_TARGET_PROPERTIES target_properties = D2D1::RenderTargetProperties();
    target_properties.dpiX = 96.0f;
    target_properties.dpiY = 96.0f;
    const HRESULT hr = d2d_factory_->CreateHwndRenderTarget(
        target_properties, D2D1::HwndRenderTargetProperties(hwnd_, size), &target_);
    if (FAILED(hr) || target_ == nullptr) {
        init_error_ = "CreateHwndRenderTarget " + hex_code(hr);
        return false;
    }
    // ClearType 在这种小字号上比灰度抗锯齿清晰；背景总是不透明的，
    // 不会出现 ClearType 在透明面上的彩边。
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    create_brushes();
    ctx_.rt = target_;
    ctx_.dwrite = dwrite_;
    ctx_.fonts = &fonts_;
    ctx_.theme = theme_;
    return true;
}

void App::discard_render_target() {
    destroy_brushes();
    if (target_ != nullptr) {
        target_->Release();
        target_ = nullptr;
    }
    ctx_.rt = nullptr;
    if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::create_brushes() {
    destroy_brushes();
    if (target_ == nullptr) return;
    const auto make = [this](const D2D1_COLOR_F& color) -> ID2D1SolidColorBrush* {
        ID2D1SolidColorBrush* brush = nullptr;
        target_->CreateSolidColorBrush(color, &brush);
        return brush;
    };
    ctx_.bg = make(theme_->bg);
    ctx_.card = make(theme_->card);
    ctx_.border = make(theme_->border);
    ctx_.text = make(theme_->text);
    ctx_.muted = make(theme_->muted);
    ctx_.accent = make(theme_->accent);
    ctx_.accent_text = make(theme_->accent_text);
    ctx_.hover = make(theme_->hover);
    ctx_.subtle = make(theme_->subtle);
    ctx_.subtle_hover = make(theme_->subtle_hover);
    ctx_.field = make(theme_->field);
    ctx_.field_border = make(theme_->field_border);
    ctx_.selected = make(theme_->selected);
    ctx_.log_bg = make(theme_->log_bg);
}

void App::destroy_brushes() {
    ID2D1SolidColorBrush** brushes[] = {
        &ctx_.bg,       &ctx_.card,         &ctx_.border,       &ctx_.text,
        &ctx_.muted,    &ctx_.accent,       &ctx_.accent_text,  &ctx_.hover,
        &ctx_.subtle,   &ctx_.subtle_hover, &ctx_.field,        &ctx_.field_border,
        &ctx_.selected, &ctx_.log_bg,
    };
    for (ID2D1SolidColorBrush** brush : brushes) {
        if (*brush != nullptr) {
            (*brush)->Release();
            *brush = nullptr;
        }
    }
}

void App::apply_titlebar_theme() {
    if (hwnd_ == nullptr) return;
    // Win11 用属性 20，早期的 Win10 预览版用 19；两个都试，第一个成功就够。
    const BOOL dark = theme_->dark ? TRUE : FALSE;
    for (const DWORD attribute : {DWORD(20), DWORD(19)}) {
        if (SUCCEEDED(DwmSetWindowAttribute(hwnd_, attribute, &dark, sizeof(dark)))) break;
    }
}

// ── 布局 ──────────────────────────────────────────────────────

void App::compute_layout() {
    RECT client{};
    if (hwnd_ != nullptr) GetClientRect(hwnd_, &client);
    const float client_w = static_cast<float>(std::max<LONG>(1, client.right));
    const float client_h = static_cast<float>(std::max<LONG>(1, client.bottom));
    layout_widgets(client_w, client_h);
}

void App::layout_widgets(float client_width, float client_height) {
    const float scale = scale_;
    // 行高一律"量出来"，不写死：字体回退会让中英文混排的行距变，量一下最准
    const float line_small = text_measure(ctx_, "Ag", FontRole::Small).height;
    const float line_title = text_measure(ctx_, "Ag", FontRole::Title).height;
    const float row_h = kRowHeight * scale;
    const float grid_row_h =
        std::max(line_small, kInputHeight * scale) + 2.0f * kGridPadY * scale;
    const float card_pad = kCardPad * scale;
    // 标签列宽按当前语言实测，不写死。
    //
    // 中文标签最短（"模式" 两个字），英文长得多（"Maximum render attempts"），
    // 原来固定成 138 逻辑像素：中文刚好放得下，英文则被输入框盖掉后半截。
    // 量一遍九个标签取最大值，两种语言都不会截断；多出来的一点宽度由输入框
    // 让出来（下面 field_w 会跟着变窄，但有 56 逻辑像素的下限兜着）。
    float label_w = 80.0f * scale;
    for (const char* key : kGridLabelKeys) {
        label_w = std::max(label_w, text_measure(ctx_, tr(key), FontRole::Small).width);
    }
    // 多留一点：DirectWrite 的排版宽度和实际绘制出来能差几个像素（"Output folder"
    // 就差了半个字母），标签一旦被右边的 EDIT 盖住就变成 "Output folde"。
    label_w += 12.0f * scale;
    const float note_w = 352.0f * scale;
    const float gap = kCardGap * scale;

    // ---- 外框：与 gui.py 的 container padx=18 / pady=14 一致 ----
    root_ = make_rect(kRootPadX * scale, kRootPadY * scale,
                      client_width - kRootPadX * scale, client_height - kRootPadY * scale);
    if (root_.width() < 240.0f || root_.height() < 200.0f) return;   // 太小就先不算

    // ---- 顶栏 ----
    const float header_h = line_title + 2.0f * scale + kSubtitleLineHeight * scale;
    Rect rest;
    header_ = take_top(root_, header_h, rest);

    float theme_button_w = text_measure(ctx_, tr("深色"), FontRole::Small).width +
                           2.0f * kButtonPadXSmall * scale;
    float lang_button_w = text_measure(ctx_, "English", FontRole::Small).width +
                          2.0f * kButtonPadXSmall * scale;
    theme_button_w = std::max(theme_button_w, 54.0f * scale);
    lang_button_w = std::max(lang_button_w, 54.0f * scale);
    const float right_block = lang_button_w * 2.0f + theme_button_w +
                              kSmallGap * scale * 2.0f + kFloatGap * scale;
    Rect header_left;
    Rect header_right;
    split_right(header_, right_block, kFloatGap * scale, header_left, header_right);

    {
        Rect below;
        title_rect_ = take_top(header_left, line_title, below);
        subtitle_rect_ = take_top(below, kSubtitleLineHeight * scale, below);
    }
    {
        const float top = header_.top + (header_.height() - kHeaderButtonHeight * scale) * 0.5f;
        const float bottom = top + kHeaderButtonHeight * scale;
        float cursor = header_right.left;
        btn_lang_zh_.bounds = make_rect(cursor, top, cursor + lang_button_w, bottom);
        cursor += lang_button_w + kSmallGap * scale;
        btn_lang_en_.bounds = make_rect(cursor, top, cursor + lang_button_w, bottom);
        cursor += lang_button_w + kFloatGap * scale;
        btn_theme_.bounds = make_rect(cursor, top, cursor + theme_button_w, bottom);
    }

    // ---- 左右两列 ----
    const float right_w = std::min(kRightWidth * scale, rest.width() * 0.55f);
    Rect left_column;
    Rect right_column;
    split_right(rest, right_w, kColumnGap * scale, left_column, right_column);

    // ---- 右列：压缩设置卡片 ----
    //
    // 两列"标签 + 输入框"的宽度是按卡片内宽反推的，反推时必须把两列之间的
    // kLabelGap 也算进去——漏了它，第二列的右边缘就会跑到卡片内边距之外，
    // 输入框直接顶到卡片边框上（看上去就是"框子太长，戳出边框了"）。
    const float card_inner_w = right_column.width() - 2.0f * card_pad;
    // 输入框再窄也得放得下四位数字（"3000"），所以标签最多占到这里为止；
    // 超过就先把标签压窄。这样无论哪种语言的翻译有多长，两列都不会顶出卡片
    // （下面的 field_w 也据此取下限，不会再被下限反推出去）。
    const float min_field_w = 44.0f * scale;
    const float max_label_w =
        std::max(80.0f * scale, (card_inner_w - kLabelGap * scale - 2.0f * kGridPadX * scale -
                                 2.0f * min_field_w) *
                                    0.5f);
    label_w = std::min(label_w, max_label_w);
    const float field_w =
        std::max(min_field_w, (card_inner_w - 2.0f * (label_w + kGridPadX * scale) -
                               kLabelGap * scale) *
                                  0.5f);
    const float note_h = std::max(
        line_small,
        text_height(ctx_,
                    tr("提示：有损渲染会把文字变成图片，最适合扫描件；"
                       "文件只会变小不会变清晰。"),
                    note_w, FontRole::Small));
    const float settings_h = card_pad + line_small + kHeadingGap * scale + 6.0f * grid_row_h +
                             kCheckTopGap * scale +
                             (3.0f * kCheckHeight * scale + 2.0f * kCheckGap * scale) +
                             kNoteGap * scale + note_h + card_pad;
    settings_card_ = make_rect(right_column.left, right_column.top, right_column.right,
                               right_column.top + settings_h);
    settings_body_ = inset(settings_card_, card_pad);

    {
        Rect rest_after_heading;
        heading_rect_ = take_top(settings_body_, line_small, rest_after_heading);
        rest_after_heading = offset(rest_after_heading, 0.0f, kHeadingGap * scale);

        Rect grid;
        Rect after_grid;
        grid = take_top(rest_after_heading, 6.0f * grid_row_h, after_grid);

        // 表单：两列"标签 + 输入框"。列位置在这里算一次，paint 直接用 labels_。
        const float left_x = grid.left;
        const float right_x = grid.left + label_w + kGridPadX * scale;
        const float second_label_x = right_x + field_w + kLabelGap * scale;
        const float second_field_x = second_label_x + label_w + kGridPadX * scale;
        const float span_to_right = second_field_x + field_w - right_x;

        const auto cell = [&](int row, float x, float width) {
            const Rect box{x, grid.top + static_cast<float>(row) * grid_row_h, x + width,
                           grid.top + static_cast<float>(row + 1) * grid_row_h};
            return inset(box, kGridPadY * scale);
        };

        labels_.assign(9, Rect{});
        labels_[0] = cell(0, left_x, label_w);            // 目标大小 (MB)
        labels_[1] = cell(0, second_label_x, label_w);    // 固定 DPI（可留空）
        labels_[2] = cell(1, left_x, label_w);            // 输出目录
        labels_[3] = cell(2, left_x, label_w);            // 模式
        labels_[4] = cell(3, left_x, label_w);            // 策略
        labels_[5] = cell(4, left_x, label_w);            // 最大渲染次数
        labels_[6] = cell(4, second_label_x, label_w);    // 渲染进程数
        labels_[7] = cell(5, left_x, label_w);            // 搜索上限 (DPI)
        labels_[8] = cell(5, second_label_x, label_w);    // 搜索下限 (DPI)

        field_target_.bounds = cell(0, right_x, field_w);
        field_dpi_.bounds = cell(0, second_field_x, field_w);
        field_outdir_.bounds = cell(1, right_x, span_to_right);
        // 输出目录那一格右边还有一个 "…" 按钮（gui.py 里用 frame 贴在输入框右侧）
        const float dots_w = kDotsWidth * scale;
        field_outdir_.bounds.right -= dots_w + kSmallGap * scale;
        btn_outdir_.bounds = make_rect(field_outdir_.bounds.right + kSmallGap * scale,
                                       field_outdir_.bounds.top + 2.0f * scale,
                                       field_outdir_.bounds.right + kSmallGap * scale + dots_w,
                                       field_outdir_.bounds.bottom - 2.0f * scale);
        // 下拉框横跨"输入框 + 第二列标签 + 第二个输入框"（gui.py 里 columnspan=3
        // 的 sticky="ew"），右边缘和其它输入框对齐。以前这里多加了 kGridPadX，
        // 框子直接压到卡片边框上。
        drop_mode_.bounds = cell(2, right_x, span_to_right);
        drop_strategy_.bounds = cell(3, right_x, span_to_right);
        field_retries_.bounds = cell(4, right_x, field_w);
        field_jobs_.bounds = cell(4, second_field_x, field_w);
        field_maxdpi_.bounds = cell(5, right_x, field_w);
        field_mindpi_.bounds = cell(5, second_field_x, field_w);

        // ---- 三个复选框 ----
        float top = after_grid.top + kCheckTopGap * scale;
        const auto place_check = [&](CheckBox& box, float extra) {
            top += extra;
            box.bounds = make_rect(settings_body_.left, top, settings_body_.right,
                                   top + kCheckHeight * scale);
            top += kCheckHeight * scale;
        };
        place_check(check_force_, 0.0f);
        place_check(check_keep_, kCheckGap * scale);
        place_check(check_dry_, kCheckGap * scale);

        note_rect_ = make_rect(settings_body_.left, top + kNoteGap * scale,
                               settings_body_.left + note_w,
                               top + kNoteGap * scale + note_h);
    }

    // ---- 右列：操作卡片 ----
    const float action_h = card_pad * 2.0f + (kStartHeight + 8.0f + kCancelHeight) * scale;
    action_card_ = make_rect(right_column.left, settings_card_.bottom + gap,
                             right_column.right, settings_card_.bottom + gap + action_h);
    action_body_ = inset(action_card_, card_pad);
    {
        const float button_w = std::min(280.0f * scale, action_body_.width());
        const float button_left = action_body_.left + (action_body_.width() - button_w) * 0.5f;
        btn_start_.bounds = make_rect(button_left, action_body_.top, button_left + button_w,
                                      action_body_.top + kStartHeight * scale);
        btn_cancel_.bounds =
            make_rect(button_left, btn_start_.bounds.bottom + 8.0f * scale, button_left + button_w,
                      btn_start_.bounds.bottom + 8.0f * scale + kCancelHeight * scale);

        // 询问条：一行提示文字 + 2~4 个按钮，占掉"开始/取消"的位置。
        // 文字要几行就往下推几行，剩下的高度平分给按钮。
        for (Button& button : btn_ask_) button.bounds = Rect{};
        ask_label_ = Rect{};
        if (ask_button_count_ > 0) {
            // 文字和按钮标题都按当前语言现取（布局每帧都跑，切语言后自动跟上）
            const std::string ask_text =
                ask_visible_.text ? ask_visible_.text() : std::string();
            const std::vector<std::string> ask_labels =
                ask_visible_.labels ? ask_visible_.labels() : std::vector<std::string>{};
            const float measured = std::max(
                line_small,
                text_height(ctx_, ask_text, action_body_.width(), FontRole::Small));
            ask_label_ = make_rect(action_body_.left, action_body_.top, action_body_.right,
                                   action_body_.top + measured);
            float ask_top = ask_label_.bottom + 8.0f * scale;
            const int count = std::min(ask_button_count_, 4);
            const float available = action_card_.bottom - card_pad - ask_top;
            const float each = std::max(24.0f * scale,
                                        (available - (count - 1) * 4.0f * scale) / count);
            for (int index = 0; index < 4; ++index) {
                Button& button = btn_ask_[index];
                button.text = (index < static_cast<int>(ask_labels.size()))
                                  ? ask_labels[static_cast<std::size_t>(index)]
                                  : std::string();
            }
            for (int index = 0; index < count; ++index) {
                const float top_button = ask_top + index * (each + 4.0f * scale);
                btn_ask_[index].bounds =
                    make_rect(button_left, top_button, button_left + button_w,
                              top_button + std::min(each, 30.0f * scale));
            }
        }
    }

    // ---- 左列：文件卡片 ----
    const float list_h = kRowHeight * (kListRows + 1.0f) * scale;   // +1 行表头
    const float files_h = card_pad * 2.0f + kHeadButtonHeight * scale + kHeadingGap * scale +
                          list_h + kHeadingGap * scale + line_small;
    files_card_ = make_rect(left_column.left, left_column.top, left_column.right,
                            left_column.top + files_h);
    files_body_ = inset(files_card_, card_pad);

    {
        const float head_h = kHeadButtonHeight * scale;
        Rect head_rest;
        files_head_ = take_top(files_body_, head_h, head_rest);
        const float button_top = files_head_.top;
        const float button_bottom = button_top + head_h;

        const auto button_width = [&](const char* text, FontRole role) {
            return text_measure(ctx_, tr(text), role).width + 2.0f * kButtonPadXSmall * scale;
        };
        const float add_w = button_width("添加 PDF 文件", FontRole::SemiboldSmall);
        const float remove_w = button_width("移除所选", FontRole::Small);
        const float clear_w = button_width("清空", FontRole::Small);
        float cursor = files_head_.right;
        cursor -= clear_w;
        btn_clear_.bounds = make_rect(cursor, button_top, cursor + clear_w, button_bottom);
        cursor -= kSmallGap * scale + remove_w;
        btn_remove_.bounds = make_rect(cursor, button_top, cursor + remove_w, button_bottom);
        cursor -= kSmallGap * scale + add_w;
        btn_add_.bounds = make_rect(cursor, button_top, cursor + add_w, button_bottom);
    }

    file_list_.bounds =
        make_rect(files_body_.left,
                  files_body_.top + kHeadButtonHeight * scale + kHeadingGap * scale,
                  files_body_.right,
                  files_body_.top + kHeadButtonHeight * scale + kHeadingGap * scale + list_h);
    {
        const float scroll_w = kScrollWidth * scale;
        Rect list_main;
        Rect list_scroll_rect;
        split_right(file_list_.bounds, scroll_w + 2.0f * scale, 2.0f * scale, list_main,
                    list_scroll_rect);
        file_list_.header = make_rect(list_main.left, list_main.top, list_main.right,
                                      list_main.top + kRowHeight * scale);
        file_list_.body = make_rect(list_main.left, file_list_.header.bottom, list_main.right,
                                    list_main.bottom);
        file_list_.row_height = pixel(row_h);
        file_list_.status_width = 130.0f * scale;
        file_list_.size_width = 90.0f * scale;
        file_list_.name_width =
            std::max(60.0f * scale, file_list_.body.width() - file_list_.size_width -
                                        file_list_.status_width);

        list_scroll_.bounds = list_scroll_rect;
        list_scroll_.content_height =
            static_cast<int>(std::lround(static_cast<float>(files_.size()) * row_h));
        list_scroll_.view_height = pixel(file_list_.body.height());
        list_scroll_.set_offset(list_scroll_.offset);
        file_list_.scroll = list_scroll_;
    }
    files_hint_ = make_rect(files_body_.left, file_list_.bounds.bottom + kHeadingGap * scale,
                            files_body_.right,
                            file_list_.bounds.bottom + kHeadingGap * scale + line_small);

    // ---- 左列：进度与日志卡片（吃掉剩下的高度） ----
    float log_bottom = std::max(left_column.bottom, files_card_.bottom + gap + kMinLogHeight * scale);
    log_card_ = make_rect(left_column.left, files_card_.bottom + gap, left_column.right,
                          log_bottom);
    log_body_ = inset(log_card_, card_pad);
    {
        Rect rest_after_head;
        log_head_ = take_top(log_body_, line_small, rest_after_head);
        rest_after_head = offset(rest_after_head, 0.0f, kProgressGap * scale);
        progress_bar_.bounds = take_top(rest_after_head, 6.0f * scale, rest_after_head);
        rest_after_head = offset(rest_after_head, 0.0f, kProgressGap * scale);
        field_log_.bounds = rest_after_head;
        // offset() 把上下两条边一起下移，这里用了两次，底边就累计多走了
        // 2*kProgressGap > kCardPad，日志框的框线会画到卡片外面去。
        // 底边是卡片内缘，不跟着偏移走，收回来。
        field_log_.bounds.bottom = std::min(field_log_.bounds.bottom, log_body_.bottom);
    }
}

// ── 绘制 ──────────────────────────────────────────────────────

void App::paint_card(const Rect& rect, float radius) {
    if (rect.empty()) return;
    fill_round(ctx_, rect, radius, ctx_.card);
    stroke_round(ctx_, rect, radius, ctx_.border, std::max(1.0f, scale_));
}

void App::paint_grid() {
    // 顺序与 gui.py 的 label(...) 调用一致：先左后右、逐行往下
    const std::size_t count =
        std::min<std::size_t>(std::size(kGridLabelKeys), labels_.size());
    for (std::size_t index = 0; index < count; ++index) {
        draw_text_utf8(ctx_, tr(kGridLabelKeys[index]), labels_[index], FontRole::Small,
                       theme_->muted);
    }
}

/// 给每一个原生 EDIT 画外壳（底色 + 边框）。
///
/// 为什么必须由父窗口来画：EDIT 自己只会用 WM_CTLCOLOREDIT 给的画刷刷一遍自己的
/// 矩形，没有边框。浅色主题里输入框底色和卡片底色都是纯白，于是空着的输入框
/// （最典型的是"输出目录"）在界面上就是一片空白——标签孤零零地挂在左边，用户
/// 看不出那里能点、更看不出该往哪里打字。控件创建处那句"边框和圆角由我们自己
/// 在卡片上画"说的就是这里。
///
/// 子控件的矩形在 sync_native_controls 里比 bounds 内缩了一个像素，就是为了给
/// 这条框线让位：父窗口的绘制被 WS_CLIPCHILDREN 挡在子控件矩形之外，不内缩的话
/// 框线会被 EDIT 自己刷的底色盖掉。
void App::paint_field_frames() {
    const float radius = kBoxRadius * scale_;
    const float width = std::max(1.0f, scale_);
    const TextField* fields[] = {&field_target_, &field_dpi_,      &field_outdir_,
                                 &field_retries_, &field_jobs_,    &field_maxdpi_,
                                 &field_mindpi_};
    for (const TextField* field : fields) {
        if (field->bounds.empty()) continue;
        fill_round(ctx_, field->bounds, radius, ctx_.field);
        stroke_round(ctx_, field->bounds, radius, ctx_.field_border, width);
    }
    // 日志框底色和卡片不同（log_bg），框线一样要画，否则"进度与日志"下面那块
    // 灰底白底的分界看不出来。
    if (!field_log_.bounds.empty()) {
        fill_round(ctx_, field_log_.bounds, radius, ctx_.log_bg);
        stroke_round(ctx_, field_log_.bounds, radius, ctx_.field_border, width);
    }
}

namespace {

/// 第一帧就画不出来时把 Direct2D 的错误摆到用户面前。
///
/// 为什么值得为它写一个弹窗：D2D 一旦失败，窗口会是一整片空白——用户看到的是
/// "程序打开了，但什么都没有"，比直接报错还难理解，也没有任何线索可查。
void report_paint_failure(const std::string& detail) {
    const std::string message =
        tr("[失败] 图形界面无法绘制（Direct2D 报告 {detail}）。"
           "这台机器上的显示驱动可能不支持硬件加速；请改用命令行版本。",
           {{"detail", detail}});
    const std::wstring wide = utf8_to_wide(message);
    MessageBoxW(nullptr, wide.c_str(), L"PDF Compressor",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

}  // namespace

void App::paint() {
    if (in_paint_) return;
    if (!ensure_render_target()) {
        // 渲染目标都建不起来时连 EndDraw 都到不了，重试逻辑必须放在这里，
        // 否则窗口会一直白着，而且一句解释都没有。
        ++paint_failures_;
        if (paint_failures_ < kPaintFailureLimit) {
            if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (!reported_paint_failure_) {
            reported_paint_failure_ = true;
            report_paint_failure(init_error_);
        }
        return;
    }
    in_paint_ = true;

    compute_layout();
    sync_native_controls();

    ctx_.theme = theme_;
    ID2D1HwndRenderTarget* target = target_;
    target->BeginDraw();
    target->SetTransform(D2D1::Matrix3x2F::Identity());
    target->Clear(theme_->bg);
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    // ---- 顶栏 ----
    draw_text_utf8(ctx_, kAppName, title_rect_, FontRole::Title, theme_->text);
    draw_text_utf8(ctx_,
                   tr("把 PDF 压到指定大小；也可以按固定分辨率重新渲染。支持多选。"),
                   subtitle_rect_, FontRole::Subtitle, theme_->muted);

    // ---- 四张卡片的底 ----
    paint_card(files_card_, kCardRadius * scale_);
    paint_card(log_card_, kCardRadius * scale_);
    paint_card(settings_card_, kCardRadius * scale_);
    paint_card(action_card_, kCardRadius * scale_);

    // ---- 输入框的外壳（要压在卡片上、又在 EDIT 子控件之下） ----
    paint_field_frames();

    // ---- 文件卡片 ----
    draw_text_utf8(ctx_, tr("文件"), files_head_, FontRole::Heading, theme_->text);
    draw_text_utf8(ctx_, tr("双击列表项可打开它所在的文件夹"), files_hint_, FontRole::Small,
                   theme_->muted);
    btn_add_.paint(ctx_, scale_);
    btn_remove_.paint(ctx_, scale_);
    btn_clear_.paint(ctx_, scale_);
    paint_file_list(ctx_, file_list_, list_rows(), scale_);
    if (files_.empty()) {
        // 五行空白看着像界面没画完。这里放一句指路的话，顺便把"能拖进来"这个
        // 唯一没有对应控件的操作说明白。
        draw_text_utf8(ctx_, tr("把 PDF 文件拖到这里，或点上面的「添加 PDF 文件」"),
                       file_list_.body, FontRole::Small, theme_->muted, TextAlign::Center);
    }

    // ---- 进度与日志卡片 ----
    draw_text_utf8(ctx_, tr("进度与日志"), log_head_, FontRole::Heading, theme_->text);
    draw_text_utf8(ctx_, status_text_, log_head_, FontRole::Small, theme_->muted,
                   TextAlign::Right);
    progress_bar_.paint(ctx_);
    // 日志窗是原生 EDIT，底色由 paint_field_frames 一起铺好；EDIT 是直角矩形、
    // 卡片是圆角，两者之间的缝就靠那层底色填上。

    // ---- 压缩设置卡片 ----
    draw_text_utf8(ctx_, tr("压缩设置"), heading_rect_, FontRole::Heading, theme_->text);
    paint_grid();
    drop_mode_.paint(ctx_, scale_);
    drop_strategy_.paint(ctx_, scale_);
    btn_outdir_.paint(ctx_, scale_);
    check_force_.paint(ctx_, scale_);
    check_keep_.paint(ctx_, scale_);
    check_dry_.paint(ctx_, scale_);
    draw_text_utf8_wrapped(ctx_,
                           tr("提示：有损渲染会把文字变成图片，最适合扫描件；"
                              "文件只会变小不会变清晰。"),
                           note_rect_, FontRole::Small, theme_->muted);

    // ---- 操作卡片：平时是"开始/取消"，询问时换成提示行 + 若干按钮 ----
    if (ask_button_count_ > 0) {
        const std::string ask_text =
            ask_visible_.text ? ask_visible_.text() : std::string();
        draw_text_utf8_wrapped(ctx_, ask_text, ask_label_, FontRole::Small, theme_->text);
        for (int index = 0; index < ask_button_count_ && index < 4; ++index) {
            btn_ask_[index].paint(ctx_, scale_);
        }
    } else {
        btn_start_.paint(ctx_, scale_);
        btn_cancel_.paint(ctx_, scale_);
    }

    // ---- 顶栏右侧按钮 ----
    btn_lang_zh_.paint(ctx_, scale_);
    btn_lang_en_.paint(ctx_, scale_);
    btn_theme_.paint(ctx_, scale_);

    const HRESULT result = target->EndDraw();
    in_paint_ = false;
    if (SUCCEEDED(result)) {
        paint_failures_ = 0;
        return;
    }

    // 一帧画不出来时最要紧的是**再画一帧**：EndDraw 失败之后渲染目标的状态是
    // 未定义的，D2D 官方要求丢掉它重建，而 Windows 不会自动再送一个 WM_PAINT
    // 过来——不主动 Invalidate 的话，窗口就永远停在半白（甚至全白）的状态。
    // 这台机器上首次绘制就失败过，正是这个原因让它一直白屏。
    ++paint_failures_;
    discard_render_target();
    if (paint_failures_ < kPaintFailureLimit) {
        if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    // 连着好几帧都失败才认定是真的用不了：Direct2D 失败时窗口一片空白，
    // 用户看到的是"程序打开了但什么都没有"，比报错还难理解。只弹一次。
    if (!reported_paint_failure_) {
        reported_paint_failure_ = true;
        report_paint_failure("EndDraw " + hex_code(result));
    }
}

// ── 原生子控件 ────────────────────────────────────────────────

void App::destroy_native_controls() {
    TextField* fields[] = {&field_target_, &field_dpi_,   &field_outdir_, &field_retries_,
                           &field_jobs_,   &field_maxdpi_, &field_mindpi_, &field_log_};
    for (TextField* field : fields) {
        if (field->hwnd != nullptr) {
            RemoveWindowSubclass(field->hwnd, edit_subclass_proc, kEditSubclassId);
            DestroyWindow(field->hwnd);
            field->hwnd = nullptr;
        }
    }
    if (field_brush_ != nullptr) {
        DeleteObject(field_brush_);
        field_brush_ = nullptr;
    }
    if (log_brush_ != nullptr) {
        DeleteObject(log_brush_);
        log_brush_ = nullptr;
    }
    // 画刷没了，主题标记也要一起清：下一次 sync 必须先重建画刷，再建控件。
    brush_theme_ = nullptr;
    log_text_cache_.clear();
}

void App::sync_native_controls() {
    if (hwnd_ == nullptr) return;
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    // ── 画刷必须赶在控件创建之前 ──────────────────────────────
    // EDIT 一建出来就会擦一次自己的背景，擦的时候用的正是 WM_CTLCOLOR* 返回的
    // 画刷。画刷晚一步创建就返回空，控件按系统默认（白）刷一遍——而且只要它的
    // 内容不再变化，就一直不会重刷。空日志窗是最典型的受害者：切到深色主题后
    // 那块白底会一直留着（内容为空时连 SetWindowText 都不会调用）。
    //
    // 顺带解决另一个浪费：以前每次 sync（也就是每一帧）都删了重建两个画刷。
    const bool theme_changed = (brush_theme_ != theme_) || field_brush_ == nullptr ||
                               log_brush_ == nullptr;
    if (theme_changed) {
        if (field_brush_ != nullptr) DeleteObject(field_brush_);
        if (log_brush_ != nullptr) DeleteObject(log_brush_);
        field_brush_ = CreateSolidBrush(to_colorref(theme_->field));
        log_brush_ = CreateSolidBrush(to_colorref(theme_->log_bg));
        brush_theme_ = theme_;
    }

    const auto ensure = [&](TextField& field, bool multiline, bool read_only,
                            DWRITE_FONT_WEIGHT weight, float font_size, int margin_x,
                            int margin_y) {
        if (field.hwnd == nullptr) {
            field.hwnd = create_edit(hwnd_, instance, field.bounds, multiline, read_only,
                                     weight, font_size, margin_x, margin_y);
            if (field.hwnd != nullptr) {
                // 子类化是为了接住回车/Esc、把滚轮转给父窗口，以及让焦点变化
                // 能触发父窗口重画（输入框的底色是父窗口画的）。
                SetWindowSubclass(field.hwnd, edit_subclass_proc, kEditSubclassId,
                                  reinterpret_cast<DWORD_PTR>(this));
            }
            // 新建的 EDIT 没有圆角区域，缓存必须作废：否则换语言/换主题重建
            // （destroy_native_controls + sync_native_controls）之后尺寸恰好没变，
            // 会被缓存判定成"已经设过"而跳过，输入框四个角又变回直角。
            field.clip_width = -1;
            field.clip_height = -1;
            field.clip_radius = -1.0f;
        }
        if (field.hwnd == nullptr || field.bounds.empty()) return;
        // 往里收：外面的框线是父窗口画的（paint_field_frames），而
        // WS_CLIPCHILDREN 会把父窗口的绘制挡在子控件矩形之外，不收就会把框线盖掉。
        //
        // 多让 1 像素是给取整留的余量：pixel() 会把 left+inset 和 width-2*inset
        // 各自四舍五入，两处误差叠起来能让 EDIT 宽出 1 像素、压住描边的内半边，
        // 拐角处的框线就会断一截。
        const float inset = std::max(1.0f, scale_) + 1.0f;
        const int field_width = std::max(1, pixel(field.bounds.width() - 2.0f * inset));
        const int field_height = std::max(1, pixel(field.bounds.height() - 2.0f * inset));
        SetWindowPos(field.hwnd, nullptr, pixel(field.bounds.left + inset),
                     pixel(field.bounds.top + inset), field_width, field_height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        // 单行框的内边距跟着框宽走：设置卡片里的数字框只有六七十像素宽，
        // 固定留白 12 像素就吃掉三分之一，"3000" 放不下会横向滚动，第一个字
        // 被切掉一半（看起来像 500）。框窄时把留白压到 2~4 像素。
        if (!multiline) {
            const int wanted_pad = std::min(margin_x, std::max(2, field_width / 6));
            SendMessageW(field.hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                         MAKELPARAM(wanted_pad, wanted_pad));
        }
        // 再把 EDIT 自己裁成圆角。它是原生控件、底色是矩形，四个直角会正好切进
        // 父窗口画的圆角边框里——不裁的话看上去就是"外圈圆、里面方"。
        //
        // 半径不能简单写成 radius - inset：描边是"先把矩形内缩半个线宽、再按原半径
        // 画弧"，所以那条弧的圆心在离角 0.5*线宽 + radius 的地方；而 EDIT 的裁剪弧
        // 圆心在 inset + r。两者要同心就得 r = radius - inset + 0.5*线宽。
        // 少了这半个线宽，拐角处会出现两条相距不到一像素的弧线——单看不明显，
        // 放在日志窗那种大圆角上就是一眼能看出的"没有对齐"。
        const float stroke_half = 0.5f * std::max(1.0f, scale_);
        const float field_radius =
            std::max(0.0f, field.radius * scale_ - inset + stroke_half);
        if (field_width != field.clip_width || field_height != field.clip_height ||
            std::fabs(field_radius - field.clip_radius) > 0.01f) {
            clip_round_region(field.hwnd, field_radius);
            field.clip_width = field_width;
            field.clip_height = field_height;
            field.clip_radius = field_radius;
        }
    };

    const DWRITE_FONT_WEIGHT normal = DWRITE_FONT_WEIGHT_NORMAL;
    const int pad_x = pixel(kInputPadX * scale_);
    ensure(field_target_, false, false, normal, 12.0f * scale_, pad_x, 0);
    ensure(field_dpi_, false, false, normal, 12.0f * scale_, pad_x, 0);
    ensure(field_outdir_, false, false, normal, 12.0f * scale_, pad_x, 0);
    ensure(field_retries_, false, false, normal, 12.0f * scale_, pad_x, 0);
    ensure(field_jobs_, false, false, normal, 12.0f * scale_, pad_x, 0);
    ensure(field_maxdpi_, false, false, normal, 12.0f * scale_, pad_x, 0);
    ensure(field_mindpi_, false, false, normal, 12.0f * scale_, pad_x, 0);
    ensure(field_log_, true, true, normal, 12.0f * scale_, pad_x, pixel(8.0f * scale_));

    // 日志内容只在变化时整体重写（每次重写都会丢滚动位置、也要闪一下）
    if (field_log_.hwnd != nullptr && log_text_cache_ != log_lines_) {
        log_text_cache_ = log_lines_;
        std::string text;
        for (const std::string& line : log_lines_) {
            text += line;
            text += "\r\n";
        }
        // ES_READONLY 的控件要用 EM_SETREADONLY 短暂解锁才能写入
        SendMessageW(field_log_.hwnd, EM_SETREADONLY, FALSE, 0);
        SetWindowTextW(field_log_.hwnd, utf8_to_wide(text).c_str());
        SendMessageW(field_log_.hwnd, EM_SETREADONLY, TRUE, 0);
        // 滚到底：EM_SETSEL 到末尾 + EM_SCROLLCARET 是最稳的组合
        const int length = GetWindowTextLengthW(field_log_.hwnd);
        SendMessageW(field_log_.hwnd, EM_SETSEL, static_cast<WPARAM>(length),
                     static_cast<LPARAM>(length));
        SendMessageW(field_log_.hwnd, EM_SCROLLCARET, 0, 0);
    }

    // 换主题时每个原生控件都得强制重刷一次：底色是它们自己拿画刷刷的，换了
    // 画刷不会自动重刷（内容没变就更不会）。不补这一步，日志窗会在切到深色
    // 之后一直是白底，直到用户碰它一下。
    if (theme_changed) {
        TextField* fields[] = {&field_target_, &field_dpi_,    &field_outdir_,
                               &field_retries_, &field_jobs_,   &field_maxdpi_,
                               &field_mindpi_,  &field_log_};
        for (TextField* field : fields) {
            if (field->hwnd != nullptr) InvalidateRect(field->hwnd, nullptr, TRUE);
        }
    }
}

// ── 交互状态 ──────────────────────────────────────────────────

void App::set_hover_states(float x, float y) {
    Button* buttons[] = {&btn_add_,    &btn_remove_, &btn_clear_,  &btn_lang_zh_,
                         &btn_lang_en_, &btn_theme_,  &btn_outdir_, &btn_start_,
                         &btn_cancel_};
    for (Button* button : buttons) button->on_mouse_move(x, y);
    for (Button& button : btn_ask_) button.on_mouse_move(x, y);
    drop_mode_.on_mouse_move(x, y);
    drop_strategy_.on_mouse_move(x, y);
    check_force_.on_mouse_move(x, y);
    check_keep_.on_mouse_move(x, y);
    check_dry_.on_mouse_move(x, y);
    list_scroll_.on_mouse_move(x, y);

    list_hover_row_ = -1;
    if (file_list_.body.contains(x, y)) {
        const int index = static_cast<int>(
            (y - file_list_.body.top + static_cast<float>(list_scroll_.offset)) /
            static_cast<float>(file_list_.row_height));
        if (index >= 0 && index < static_cast<int>(files_.size())) list_hover_row_ = index;
    }
    // 列表画的是 file_list_ 里那份副本（它对 widgets 是只读入参）
    file_list_.hover_row = list_hover_row_;
}

std::optional<std::size_t> App::row_at(float x, float y) const {
    if (!file_list_.body.contains(x, y) || file_list_.row_height <= 0) return std::nullopt;
    const int index = static_cast<int>(
        (y - file_list_.body.top + static_cast<float>(list_scroll_.offset)) /
        static_cast<float>(file_list_.row_height));
    if (index < 0 || index >= static_cast<int>(files_.size())) return std::nullopt;
    return static_cast<std::size_t>(index);
}

// ── 下拉（TrackPopupMenu） ────────────────────────────────────

int App::track_dropdown(const Dropdown& dropdown) {
    // 不用 COMBOBOX：它的下拉列表是系统画的，深浅主题、字号、圆角都改不动，
    // 放在这张卡片上会明显"不是一家人"。这里画一个显示当前值的按钮，点击时
    // 弹真正的系统菜单——键盘导航、屏幕边缘翻转、DPI 缩放都是系统行为。
    if (dropdown.options.empty()) return -1;
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return -1;
    for (std::size_t index = 0; index < dropdown.options.size(); ++index) {
        // 菜单项 ID = 下标 + 1（0 不能用作命令 ID）
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(index) + 1,
                    utf8_to_wide(dropdown.options[index]).c_str());
    }
    if (dropdown.index >= 0 && dropdown.index < static_cast<int>(dropdown.options.size())) {
        CheckMenuRadioItem(menu, 1, static_cast<UINT>(dropdown.options.size()),
                           static_cast<UINT>(dropdown.index) + 1, MF_BYCOMMAND);
    }
    TPMPARAMS params{};
    params.cbSize = sizeof(params);
    params.rcExclude = RECT{pixel(dropdown.bounds.left), pixel(dropdown.bounds.top),
                            pixel(dropdown.bounds.right), pixel(dropdown.bounds.bottom)};
    const int command = static_cast<int>(TrackPopupMenuEx(
        menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
        pixel(dropdown.bounds.left), pixel(dropdown.bounds.bottom), hwnd_, &params));
    DestroyMenu(menu);
    if (command <= 0) return -1;
    return command - 1;
}

// ── 消息泵与工作线程消息 ──────────────────────────────────────

void App::post(Message* message) {
    if (message == nullptr) return;
    // 所有权约定：工作线程 new 出来，界面线程 drain_messages 里 delete。
    // 万一 PostMessage 失败（窗口正在销毁），就当场从队列里摘掉并 delete。
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push_back(message);
    }
    if (hwnd_ == nullptr || !PostMessageW(hwnd_, kMsgWorker, 0, 0)) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        const auto found = std::find(queue_.begin(), queue_.end(), message);
        if (found != queue_.end()) {
            queue_.erase(found);
            delete message;
        }
    }
}

void App::drain_messages() {
    std::vector<Message*> batch;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        batch.swap(queue_);
    }
    bool needs_repaint = false;
    for (Message* message : batch) {
        if (message == nullptr) continue;
        switch (message->kind) {
            case MessageKind::Log:
                write_log(message->text);
                needs_repaint = true;
                break;
            case MessageKind::Progress:
                progress_ = (message->total > 0)
                                ? static_cast<float>(message->done) /
                                      static_cast<float>(message->total)
                                : 1.0f;
                progress_bar_.set_value(progress_);
                // 数字先格式化成字符串，再交给带占位符的 tr
                set_status("正在渲染 {done}/{total} 页",
                           {{"done", std::to_string(message->done)},
                            {"total", std::to_string(message->total)}});
                needs_repaint = true;
                break;
            case MessageKind::Row:
                if (message->row < files_.size()) {
                    FileItem& item = files_[message->row];
                    item.status = message->status;
                    item.note = message->note;
                }
                needs_repaint = true;
                break;
            case MessageKind::Ask:
                enter_ask(message);
                needs_repaint = true;
                break;
            case MessageKind::Finished:
                handle_finished(message->succeeded, message->failed);
                needs_repaint = true;
                break;
        }
        delete message;
    }
    if (needs_repaint && hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
}

// ── 工作线程 ──────────────────────────────────────────────────

void App::start_jobs() {
    if (running_ || files_.empty()) return;

    capture_inputs();
    Inputs inputs;
    std::string error;
    if (!collect_inputs(inputs, error)) {
        // 参数不对只写日志 + 改状态文字，不弹模态框（和 gui.py 一致）
        write_log(tr("[失败] {error}", {{"error", error}}));
        set_status("参数有误");
        return;
    }
    save_preferences();

    // collect_inputs 先把 inputs_ 整个复制到 out，再**只往 out 里**写解析结果
    // （target / dpi / jobs / retries 这些数字字段）。inputs_ 自己始终只有文本，
    // 数字字段是空的。以前这里接着 make_options(..., inputs_)，于是：
    //   * 目标大小永远被判成"没填"，点「开始压缩」必然报
    //     "需要给出目标大小(MB)，或使用 --dpi 指定固定分辨率"；
    //   * 最大渲染次数、渲染进程数、搜索上下限也一并被无视，全走默认值。
    // 把校验过的这份写回去，后面所有用到 inputs_ 的地方才是用户真正填的值。
    inputs_ = inputs;

    for (std::size_t index = 0; index < files_.size(); ++index) {
        FileItem& item = files_[index];
        item.status = RowStatus::Waiting;
        item.note.clear();
        if (item.size == 0) item.size = file_size(item.path);
    }
    cancel_requested_ = false;
    cancel_pending_ = false;
    running_ = true;
    progress_ = 0.0f;
    progress_bar_.set_value(0.0f);
    set_status("开始…");
    refresh_buttons();

    // 参数在工作线程启动之前就固化：线程里不再碰任何控件、也不读任何成员文本。
    const JobOptions options = make_options(files_.front().path, inputs_);
    std::vector<std::string> paths;
    paths.reserve(files_.size());
    for (const FileItem& item : files_) paths.push_back(item.path);

    if (worker_.joinable()) worker_.join();
    worker_ = std::thread(&App::worker_main, this, std::move(paths), options);
    if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::request_cancel() {
    if (!running_) return;
    cancel_requested_ = true;
    cancel_pending_ = true;
    set_status("正在取消…");
    // 还在等回答的询问要用默认值唤醒，否则工作线程会永远卡在条件变量上
    {
        std::lock_guard<std::mutex> lock(ask_mutex_);
        if (ask_pending_) {
            ask_answered_ = true;
            ask_answer_flag_ = ask_request_.cancel_default;
            ask_answer_choice_ = ask_request_.cancel_choice;
        }
    }
    ask_answer_ready_.notify_all();
    refresh_buttons();
}

void App::worker_main(std::vector<std::string> paths, JobOptions options) {
    // 这个函数跑在工作线程上。规矩只有一条：不碰任何 HWND、不读任何控件，
    // 所有输出都通过 post() 回到界面线程。
    struct Bridge {
        App* app = nullptr;

        void emit(const std::string& line) const {
            auto* message = new Message();
            message->kind = MessageKind::Log;
            message->text = line;
            app->post(message);
        }
        void progress(int done, int total, double elapsed) const {
            auto* message = new Message();
            message->kind = MessageKind::Progress;
            message->done = done;
            message->total = total;
            message->elapsed = elapsed;
            app->post(message);
        }
        bool cancelled() const { return app->cancel_requested_.load(); }
        bool force_grow(std::int64_t size, std::int64_t target) const {
            return app->ask_force_grow(size, target);
        }
        bool accept_lossless(std::int64_t size, std::int64_t target) const {
            return app->ask_accept_lossless(size, target);
        }
        std::string manual(const Trial& trial, std::optional<double> hint) const {
            return app->ask_manual(trial, hint);
        }
    };

    Bridge bridge;
    bridge.app = this;
    UiCallbacks ui;
    ui.emit = [&bridge](const std::string& line) { bridge.emit(line); };
    ui.progress = [&bridge](int done, int total, double elapsed) {
        bridge.progress(done, total, elapsed);
    };
    ui.cancelled = [&bridge] { return bridge.cancelled(); };
    // 界面里可以提问。窗口关闭时 ask_* 会用默认值立刻返回，不会把线程卡住。
    ui.interactive = true;
    ui.ask_force_grow = [&bridge](std::int64_t size, std::int64_t target) {
        return bridge.force_grow(size, target);
    };
    ui.ask_accept_lossless = [&bridge](std::int64_t size, std::int64_t target) {
        return bridge.accept_lossless(size, target);
    };
    ui.ask_manual = [&bridge](const Trial& trial, std::optional<double> hint) {
        return bridge.manual(trial, hint);
    };

    int succeeded = 0;
    int failed = 0;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (cancel_requested_.load()) break;

        {
            auto* message = new Message();
            message->kind = MessageKind::Row;
            message->row = index;
            message->status = RowStatus::Working;
            post(message);
        }
        {
            auto* blank = new Message();
            blank->kind = MessageKind::Log;
            post(blank);
            auto* rule = new Message();
            rule->kind = MessageKind::Log;
            rule->text = std::string(60, '=');
            post(rule);
        }

        int code = kExitFail;
        try {
            JobOptions one = options;
            one.input = paths[index];
            code = run_job(one, ui);
        } catch (const std::exception& error) {
            // 引擎里的错误本来都已经变成退出码了，这里只是兜底
            auto* message = new Message();
            message->kind = MessageKind::Log;
            message->text = tr("[失败] 发生错误：{error}", {{"error", error.what()}});
            post(message);
        } catch (...) {
            auto* message = new Message();
            message->kind = MessageKind::Log;
            message->text = tr("[失败] 发生错误：{error}", {{"error", std::string("?")}});
            post(message);
        }

        const std::string name = path_basename(paths[index]);
        if (code == kExitInterrupt) {
            auto* message = new Message();
            message->kind = MessageKind::Row;
            message->row = index;
            message->status = RowStatus::Cancelled;
            post(message);
            auto* log = new Message();
            log->kind = MessageKind::Log;
            log->text = tr("[中断] 已跳过：{name}", {{"name", name}});
            post(log);
            break;
        }
        if (code == kExitOk) {
            ++succeeded;
            auto* message = new Message();
            message->kind = MessageKind::Row;
            message->row = index;
            message->status = RowStatus::Done;
            message->note = result_note(options, paths[index]);
            post(message);
        } else {
            ++failed;
            auto* message = new Message();
            message->kind = MessageKind::Row;
            message->row = index;
            message->status = RowStatus::Failed;
            post(message);
        }
        // 只有引擎真的按中断退出才算取消：如果用户是在这一份做完之后才点的
        // 取消，结果已经写出来了，就该记成完成（和 gui.py 一致）。
        if (cancel_requested_.load()) break;
    }

    auto* finished = new Message();
    finished->kind = MessageKind::Finished;
    finished->succeeded = succeeded;
    finished->failed = failed;
    post(finished);
}

void App::handle_finished(int succeeded, int failed) {
    running_ = false;
    if (cancel_pending_) {
        // 用户确实取消过：状态文字按"已取消"收场（进度条停在当前值）
        set_status("已取消");
    } else if (failed > 0 && succeeded == 0) {
        set_status("失败");
    } else {
        progress_ = 1.0f;
        progress_bar_.set_value(1.0f);
        set_status("完成：成功 {done} 个，失败 {failed} 个",
                   {{"done", std::to_string(succeeded)},
                    {"failed", std::to_string(failed)}});
    }
    cancel_requested_ = false;
    cancel_pending_ = false;
    refresh_buttons();
}

// ── 询问条 ────────────────────────────────────────────────────

void App::enter_ask(Message* message) {
    // 界面线程看到问题：把它摆到操作卡片上，等用户点按钮。
    // ask_visible_ 是给绘制线程看的那一份副本（绘制不能拿着锁，见 header 注释）。
    {
        std::lock_guard<std::mutex> lock(ask_mutex_);
        ask_request_.active = true;
        ask_request_.text = message->ask_text;
        ask_request_.labels = message->ask_labels;
        ask_request_.values = message->ask_values;
        ask_request_.choices = message->ask_choices;
        ask_request_.cancel_default = message->ask_cancel_default;
        ask_request_.cancel_choice = message->ask_cancel_choice;
        ask_request_.use_choice = message->ask_use_choice;
        ask_pending_ = true;

        ask_visible_.text = message->ask_text;
        ask_visible_.labels = message->ask_labels;
    }
    // 按钮文字不在这里定格：布局阶段每帧按当前语言重新取（见 layout_widgets），
    // 否则询问挂着的时候切语言，按钮会一直是旧语言。
    const std::vector<std::string> labels =
        ask_visible_.labels ? ask_visible_.labels() : std::vector<std::string>{};
    ask_button_count_ = std::min<int>(static_cast<int>(labels.size()), 4);
    for (int index = 0; index < 4; ++index) {
        Button& button = btn_ask_[index];
        button.enabled = true;
        button.hover = false;
        button.pressed = false;
        button.role = (index == 0) ? FontRole::SemiboldSmall : FontRole::Small;
        button.kind = (index == 0) ? ButtonKind::Accent : ButtonKind::Subtle;
    }
    // 重新算布局：询问条的高度取决于文字有几行
    compute_layout();
}

void App::answer_ask(std::size_t index) {
    std::lock_guard<std::mutex> lock(ask_mutex_);
    if (!ask_pending_) return;
    const std::vector<std::string> labels =
        ask_request_.labels ? ask_request_.labels() : std::vector<std::string>{};
    if (index >= labels.size()) return;
    ask_answered_ = true;
    if (ask_request_.use_choice) {
        ask_answer_choice_ = (index < ask_request_.choices.size())
                                 ? ask_request_.choices[index]
                                 : ask_request_.cancel_choice;
    } else {
        ask_answer_flag_ = (index < ask_request_.values.size())
                               ? ask_request_.values[index]
                               : ask_request_.cancel_default;
    }
    ask_pending_ = false;
    ask_request_.active = false;
    ask_button_count_ = 0;
    ask_answer_ready_.notify_all();
    compute_layout();
}

bool App::ask_force_grow(std::int64_t size, std::int64_t target) {
    // 工作线程在这里等界面回答。注意：要用的东西先复制到局部变量——那条消息
    // 由界面线程负责 delete，等我们回到这里它可能已经没了。
    auto* message = new Message();
    message->kind = MessageKind::Ask;
    // 文案存成 lambda 而不是拼好的字符串：用户可能在这个询问挂着的时候切语言，
    // 而工作线程正卡在下面的条件变量上，没法重发一份新文案。
    message->ask_text = [size, target] {
        return tr("无损优化后为 {size}，已经小于目标 {target:.2f} MB。"
                  "继续做有损渲染只会让文件变大，不会提高画质。要继续吗？",
                  {{"size", human_size(static_cast<double>(size))},
                   {"target", fmt_fixed(to_mb(static_cast<double>(target)), 2)}});
    };
    message->ask_labels = [] {
        return std::vector<std::string>{tr("继续渲染"), tr("保留无损结果")};
    };
    message->ask_values = {true, false};
    message->ask_cancel_default = false;
    message->ask_use_choice = false;
    post(message);

    std::unique_lock<std::mutex> lock(ask_mutex_);
    ask_answer_ready_.wait(lock, [this] { return ask_answered_; });
    ask_answered_ = false;
    return ask_answer_flag_;
}

bool App::ask_accept_lossless(std::int64_t size, std::int64_t /*target*/) {
    auto* message = new Message();
    message->kind = MessageKind::Ask;
    message->ask_text = [size] {
        return tr("是否直接接受无损优化结果 {size}？",
                  {{"size", human_size(static_cast<double>(size))}});
    };
    message->ask_labels = [] {
        return std::vector<std::string>{tr("接受"), tr("继续调整")};
    };
    message->ask_values = {true, false};
    // 窗口关掉就按"接受"收场：已经算出来的无损结果比丢掉它更有价值
    message->ask_cancel_default = true;
    message->ask_use_choice = false;
    post(message);

    std::unique_lock<std::mutex> lock(ask_mutex_);
    ask_answer_ready_.wait(lock, [this] { return ask_answered_; });
    ask_answered_ = false;
    return ask_answer_flag_;
}

std::string App::ask_manual(const Trial& trial, std::optional<double> hint_dpi) {
    auto* message = new Message();
    message->kind = MessageKind::Ask;
    message->ask_text = [trial] {
        return tr("当前结果 {size}，相当于 {dpi:.0f} DPI / JPEG {quality}。下一步怎么做？",
                  {{"size", human_size(static_cast<double>(trial.size))},
                   {"dpi", fmt_fixed(trial.dpi(), 0)},
                   {"quality", std::to_string(trial.quality)}});
    };
    message->ask_labels = [] {
        return std::vector<std::string>{tr("接受"), tr("更清晰（更大）"), tr("更小"),
                                        tr("保留最佳并退出")};
    };
    message->ask_choices = {"y", "larger", "smaller", "q"};
    message->ask_use_choice = true;
    // 取消（或窗口关闭）时统一走 "q"：保留已经找到的最佳结果并退出
    message->ask_cancel_choice = "q";
    message->ask_cancel_default = false;
    post(message);

    // 引擎的日志里已经给过 DPI 建议，这里不再重复显示，但保留参数以备将来用
    (void)hint_dpi;
    std::unique_lock<std::mutex> lock(ask_mutex_);
    ask_answer_ready_.wait(lock, [this] { return ask_answered_; });
    ask_answered_ = false;
    return ask_answer_choice_.empty() ? std::string("q") : ask_answer_choice_;
}

// ── 语言与主题 ────────────────────────────────────────────────

void App::toggle_theme() {
    theme_ = theme_->dark ? &light_theme() : &dark_theme();
    remember_theme(theme_->name);
    rebuild();
}

void App::set_language(Language language) {
    if (language == pdfc::language()) return;
    pdfc::set_language(language);
    remember_language(language);
    rebuild();
}

void App::rebuild() {
    // 换语言或换主题：整棵树重建。这样任何一个控件都不可能漏掉翻译或漏掉配色，
    // 比逐个改属性可靠得多。要保住三样东西——文件列表、日志内容、用户填的参数。
    capture_inputs();
    const int saved_scroll = list_scroll_.offset;
    const std::string saved_focus = list_focus_path_;
    const std::unordered_set<std::string> saved_selection = selected_;

    destroy_native_controls();
    fonts_.destroy();
    fonts_ready_ = false;
    build_fonts();
    ctx_.theme = theme_;
    // 换主题后所有画刷的颜色都变了，必须重建（渲染目标本身可以留着）
    if (target_ != nullptr) create_brushes();
    apply_titlebar_theme();
    compute_layout();
    sync_native_controls();
    restore_inputs();
    sync_widget_texts();
    // 状态文字也是翻译出来的，必须重新拼一遍，否则切完语言它还停在旧语言
    // （最典型的是跑完一次任务后的 "Ready" / "完成：成功 1 个" 混搭）。
    refresh_status_text();
    refresh_buttons();

    selected_ = saved_selection;
    list_focus_path_ = saved_focus;
    list_scroll_.set_offset(saved_scroll);
    if (hwnd_ != nullptr) {
        InvalidateRect(hwnd_, nullptr, TRUE);
        UpdateWindow(hwnd_);
    }
}

// ── 设置读写 ──────────────────────────────────────────────────

void App::load_preferences(const Settings& settings) {
    inputs_.target_text = settings.get("target_size", "8");
    inputs_.dpi_text = settings.get("dpi", "");
    inputs_.outdir = settings.get("output_dir", "");
    inputs_.retries_text = settings.get("max_retries", std::to_string(kDefaultRetries));
    inputs_.max_dpi_text = settings.get("max_dpi", fmt_general(kDefaultMaxDpi));
    inputs_.min_dpi_text = settings.get("min_dpi", fmt_general(kDefaultMinDpi));
    inputs_.jobs_text = settings.get("jobs", "0");
    inputs_.force = settings.get_bool("force_render", false);
    inputs_.keep_box = settings.get_bool("keep_page_box", false);
    inputs_.dry_run = settings.get_bool("dry_run", false);
    inputs_.mode =
        (to_lower(settings.get("mode", "auto")) == "manual") ? Mode::Manual : Mode::Auto;
    inputs_.strategy = (to_lower(settings.get("strategy", "closest")) == "smaller")
                           ? Strategy::Smaller
                           : Strategy::Closest;
}

void App::save_preferences() {
    capture_inputs();
    Settings settings;
    settings.load();
    settings.set("theme", std::string(theme_->name));
    settings.set("language", std::string(language_code(pdfc::language())));
    settings.set("target_size", inputs_.target_text);
    settings.set("dpi", inputs_.dpi_text);
    settings.set("output_dir", inputs_.outdir);
    settings.set("mode", inputs_.mode == Mode::Manual ? "manual" : "auto");
    settings.set("strategy", inputs_.strategy == Strategy::Smaller ? "smaller" : "closest");
    settings.set("max_retries", inputs_.retries_text);
    settings.set("max_dpi", inputs_.max_dpi_text);
    settings.set("min_dpi", inputs_.min_dpi_text);
    settings.set("jobs", inputs_.jobs_text);
    settings.set("force_render", inputs_.force);
    settings.set("keep_page_box", inputs_.keep_box);
    settings.set("dry_run", inputs_.dry_run);
    settings.save();
}

void App::capture_inputs() {
    // 控件里的文字是唯一真相：重建界面之前、保存设置之前都读一次
    const auto read = [](HWND edit) { return trim(get_edit_text(edit)); };
    const auto or_default = [](std::string value, const std::string& fallback) {
        return value.empty() ? fallback : value;
    };
    inputs_.target_text = read(field_target_.hwnd);
    inputs_.dpi_text = read(field_dpi_.hwnd);
    inputs_.outdir = read(field_outdir_.hwnd);
    inputs_.retries_text =
        or_default(read(field_retries_.hwnd), std::to_string(kDefaultRetries));
    inputs_.jobs_text = or_default(read(field_jobs_.hwnd), "0");
    inputs_.max_dpi_text = or_default(read(field_maxdpi_.hwnd), fmt_general(kDefaultMaxDpi));
    inputs_.min_dpi_text = or_default(read(field_mindpi_.hwnd), fmt_general(kDefaultMinDpi));
    // "目标大小"留空就是"没填"：如果"固定 DPI"也空着，收集参数时会报
    // "请填写“目标大小”，或填写“固定分辨率”"；只填了 DPI 时留空是合法的。
    // （不要在这里兜底成 8——那样两个都清空会变成"压到 8MB"，而不是提示用户。）
    if (check_force_.value != nullptr) inputs_.force = *check_force_.value;
    if (check_keep_.value != nullptr) inputs_.keep_box = *check_keep_.value;
    if (check_dry_.value != nullptr) inputs_.dry_run = *check_dry_.value;
}

void App::restore_inputs() {
    // 三个复选框的 value 指向 inputs_ 的字段，必须在 restore 之前绑好
    check_force_.value = &inputs_.force;
    check_keep_.value = &inputs_.keep_box;
    check_dry_.value = &inputs_.dry_run;

    set_edit_text(field_target_.hwnd, inputs_.target_text);
    set_edit_text(field_dpi_.hwnd, inputs_.dpi_text);
    set_edit_text(field_outdir_.hwnd, inputs_.outdir);
    set_edit_text(field_retries_.hwnd, inputs_.retries_text);
    set_edit_text(field_jobs_.hwnd, inputs_.jobs_text);
    set_edit_text(field_maxdpi_.hwnd, inputs_.max_dpi_text);
    set_edit_text(field_mindpi_.hwnd, inputs_.min_dpi_text);
}

void App::sync_widget_texts() {
    // 所有文案都在这里按当前语言重新取一遍（tr 会按当前语言查表）。
    // 换语言 = rebuild，所以这些文字永远和当前语言一致。
    const bool zh = (pdfc::language() == Language::Zh);
    btn_lang_zh_.text = "中文";
    btn_lang_en_.text = "English";
    btn_lang_zh_.kind = zh ? ButtonKind::Accent : ButtonKind::Subtle;
    btn_lang_zh_.role = zh ? FontRole::SemiboldSmall : FontRole::Small;
    btn_lang_en_.kind = zh ? ButtonKind::Subtle : ButtonKind::Accent;
    btn_lang_en_.role = zh ? FontRole::Small : FontRole::SemiboldSmall;
    // 主题按钮显示的是"切过去之后那个主题"的名字：现在浅色就写"深色"
    btn_theme_.text = theme_->dark ? tr("浅色") : tr("深色");
    btn_theme_.kind = ButtonKind::Subtle;
    btn_theme_.role = FontRole::Small;

    btn_add_.text = tr("添加 PDF 文件");
    btn_add_.kind = ButtonKind::Accent;
    btn_add_.role = FontRole::SemiboldSmall;
    btn_remove_.text = tr("移除所选");
    btn_remove_.kind = ButtonKind::Subtle;
    btn_remove_.role = FontRole::Small;
    btn_clear_.text = tr("清空");
    btn_clear_.kind = ButtonKind::Subtle;
    btn_clear_.role = FontRole::Small;
    btn_outdir_.text = "…";
    btn_outdir_.kind = ButtonKind::Subtle;
    btn_outdir_.role = FontRole::Small;
    btn_start_.text = tr("开始压缩");
    btn_start_.kind = ButtonKind::Accent;
    btn_start_.role = FontRole::Semibold;
    btn_cancel_.text = tr("取消");
    btn_cancel_.kind = ButtonKind::Subtle;
    btn_cancel_.role = FontRole::Ui;

    drop_mode_.options = {tr("自动搜索"), tr("手动调整")};
    drop_mode_.index = (inputs_.mode == Mode::Manual) ? 1 : 0;
    drop_strategy_.options = {tr("最接近目标"), tr("必须小于目标")};
    drop_strategy_.index = (inputs_.strategy == Strategy::Smaller) ? 1 : 0;

    check_force_.text = tr("强制有损渲染（即使文件会变大）");
    check_keep_.text = tr("保留原始页面尺寸");
    check_dry_.text = tr("只预览计划，不实际压缩");
}

void App::refresh_buttons() {
    const bool idle = !running_;
    btn_start_.set_enabled(idle && !files_.empty());
    btn_cancel_.set_enabled(!idle && !cancel_pending_);
    btn_add_.set_enabled(idle);
    btn_remove_.set_enabled(idle && !selected_.empty());
    btn_clear_.set_enabled(idle && !files_.empty());
}

void App::set_status(const char* key,
                     std::vector<std::pair<std::string, std::string>> args) {
    status_key_ = key == nullptr ? std::string() : std::string(key);
    status_args_ = std::move(args);
    refresh_status_text();
}

void App::refresh_status_text() {
    // 状态文字必须整体重新翻译，不能只把已有的译文换个语言——旧译文里没有
    // 占位符可填，而且换过语言之后再换回去也回不到原文。
    status_text_ = tr(status_key_.c_str(), status_args_);
    if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
}

// ── 文件列表 ──────────────────────────────────────────────────

void App::add_file(const std::string& path) {
    const std::string full = absolute_path(path);
    for (const FileItem& item : files_) {
        if (item.path == full) return;   // 同一份文件不重复入列
    }
    FileItem item;
    item.path = full;
    item.status = RowStatus::Waiting;
    item.size = file_size(full);
    files_.push_back(std::move(item));
}

void App::add_files_dialog() {
    try {
        const std::vector<std::string> chosen =
            choose_files(reinterpret_cast<void*>(hwnd_), initial_dir());
        for (const std::string& path : chosen) add_file(path);
    } catch (const std::exception& error) {
        write_log(tr("[失败] 无法打开文件对话框：{error}", {{"error", error.what()}}));
    } catch (...) {
        write_log(tr("[失败] 无法打开文件对话框：{error}", {{"error", std::string("?")}}));
    }
    update_list_scroll();
    refresh_buttons();
    if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::choose_output_dir() {
    // 输出目录那个 "…" 按钮。对话框由 filedialog.cpp 提供（Windows 上是
    // IFileOpenDialog 的文件夹模式），这里只管把结果写回输入框。
    try {
        const std::string folder =
            choose_folder(reinterpret_cast<void*>(hwnd_), initial_dir());
        if (!folder.empty()) {
            inputs_.outdir = folder;
            set_edit_text(field_outdir_.hwnd, folder);
        }
    } catch (const std::exception& error) {
        write_log(tr("[失败] 无法打开文件夹对话框：{error}", {{"error", error.what()}}));
    } catch (...) {
        write_log(tr("[失败] 无法打开文件夹对话框：{error}", {{"error", std::string("?")}}));
    }
    if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::remove_selected() {
    if (selected_.empty()) return;
    files_.erase(std::remove_if(files_.begin(), files_.end(),
                                [this](const FileItem& item) {
                                    return selected_.count(item.path) != 0;
                                }),
                 files_.end());
    selected_.clear();
    list_hover_row_ = -1;
    if (list_scroll_.offset > list_scroll_.max_offset()) {
        list_scroll_.set_offset(list_scroll_.max_offset());
    }
    update_list_scroll();
    refresh_buttons();
    if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::clear_files() {
    files_.clear();
    selected_.clear();
    list_hover_row_ = -1;
    list_scroll_.set_offset(0);
    update_list_scroll();
    refresh_buttons();
    if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::update_list_scroll() {
    // 行数变了要重算滚动范围；布局里的行高是现成的
    if (file_list_.row_height <= 0) return;
    list_scroll_.content_height = static_cast<int>(
        std::lround(static_cast<double>(files_.size()) * file_list_.row_height));
    list_scroll_.view_height = pixel(file_list_.body.height());
    list_scroll_.set_offset(list_scroll_.offset);
    file_list_.scroll = list_scroll_;
    file_list_.hover_row = list_hover_row_;
}

void App::select_row(int row, bool additive) {
    if (row < 0 || row >= static_cast<int>(files_.size())) return;
    const std::string& path = files_[static_cast<std::size_t>(row)].path;
    if (!additive) selected_.clear();
    if (additive && selected_.count(path) != 0) {
        selected_.erase(path);
    } else {
        selected_.insert(path);
    }
    list_focus_path_ = path;
    list_anchor_ = row;

    // 键盘导航时要把选中的那一行滚进可视区，否则方向键按下去"没反应"
    const int row_h = std::max(1, file_list_.row_height);
    const int top = row * row_h;
    const int bottom = top + row_h;
    if (top < list_scroll_.offset) {
        list_scroll_.set_offset(top);
    } else if (bottom > list_scroll_.offset + list_scroll_.view_height) {
        list_scroll_.set_offset(bottom - list_scroll_.view_height);
    }
    update_list_scroll();
    refresh_buttons();
    if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
}

std::vector<FileRowText> App::list_rows() const {
    std::vector<FileRowText> rows;
    rows.reserve(files_.size());
    for (const FileItem& item : files_) {
        FileRowText row;
        row.name = path_basename(item.path);
        // 大小是入列时记下的原始大小；取不到就空着（不显示 "0 B" 这种误导）
        row.size = item.size > 0 ? human_size(static_cast<double>(item.size)) : std::string();
        switch (item.status) {
            case RowStatus::Waiting:
                row.status = tr("等待");
                break;
            case RowStatus::Working:
                row.status = tr("压缩中…");
                break;
            case RowStatus::Done:
                row.status = tr("完成") + (item.note.empty() ? std::string()
                                                            : " " + item.note);
                break;
            case RowStatus::Failed:
                row.status = tr("失败");
                break;
            case RowStatus::Cancelled:
                row.status = tr("已取消");
                break;
        }
        row.selected = selected_.count(item.path) != 0;
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string App::initial_dir() const {
    // 与 gui.py 的 _initial_dir 一致：有文件就取第一个文件所在目录，
    // 否则用输出目录，再否则用当前工作目录。
    if (!files_.empty()) return path_dirname(files_.front().path);
    if (!inputs_.outdir.empty()) return inputs_.outdir;
    std::vector<wchar_t> buffer(MAX_PATH);
    const DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(buffer.size()), buffer.data());
    if (length > 0 && length < buffer.size()) return wide_to_utf8(buffer.data());
    return {};
}

void App::open_selected_location() {
    // 双击列表项 = 打开它所在的文件夹（不是打开文件本身），与 gui.py 一致
    std::string target = list_focus_path_;
    if (target.empty() && !selected_.empty()) target = *selected_.begin();
    if (target.empty()) return;
    const std::string folder = path_dirname(target);
    if (folder.empty()) return;
    // 用资源管理器打开目录；ShellExecuteW 不阻塞，也不会弹控制台
    const HINSTANCE result = ShellExecuteW(hwnd_, L"open", utf8_to_wide(folder).c_str(),
                                           nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        // 打开失败不是致命错误，写一行日志就够
        write_log(tr("[失败] {error}", {{"error", folder}}));
    }
}

// ── 日志 ──────────────────────────────────────────────────────

void App::write_log(const std::string& line) {
    log_lines_.push_back(line);
    // 和 gui.py 一样：超过 4000 行就把最老的 1000 行丢掉，
    // 不然长时间压缩会把内存和重绘成本都堆起来。
    if (log_lines_.size() > 4000) {
        log_lines_.erase(log_lines_.begin(), log_lines_.begin() + 1000);
    }
    // 日志窗的重写交给 sync_native_controls（它按 log_text_cache_ 判断要不要动），
    // 这里只要保证它会跑一遍：paint 里会调用它，所以这里不用另外通知。
}

// ── 参数收集 ──────────────────────────────────────────────────

bool App::parse_number(const std::string& text, const std::string& name, bool allow_empty,
                       std::optional<double>& out, std::string& error) {
    if (text.empty()) {
        if (allow_empty) {
            out.reset();
            return true;
        }
        error = tr("{name} 不能为空", {{"name", name}});
        return false;
    }
    try {
        std::size_t consumed = 0;
        const double value = std::stod(text, &consumed);
        // 只接受"整串都是数字"：免得 "8MB" 这种被静默当成 8
        while (consumed < text.size() && std::isspace(static_cast<unsigned char>(text[consumed]))) {
            ++consumed;
        }
        if (consumed != text.size()) throw std::invalid_argument("trailing");
        out = value;
        return true;
    } catch (...) {
        error = tr("{name} 需要是一个数字，收到“{value}”",
                   {{"name", name}, {"value", text}});
        return false;
    }
}

bool App::collect_inputs(Inputs& out, std::string& error) const {
    // 校验顺序与文案和 gui.py 的 collect_options 逐条对应
    out = inputs_;
    if (!parse_number(inputs_.target_text, tr("目标大小"), true, out.target, error)) {
        return false;
    }
    if (!parse_number(inputs_.dpi_text, tr("固定分辨率"), true, out.dpi, error)) {
        return false;
    }
    if (!out.target.has_value() && !out.dpi.has_value()) {
        error = tr("请填写“目标大小”，或填写“固定分辨率”");
        return false;
    }
    if (out.target.has_value() && *out.target <= 0.0) {
        error = tr("目标大小必须大于 0");
        return false;
    }
    std::optional<double> max_dpi;
    if (!parse_number(inputs_.max_dpi_text, tr("搜索上限"), false, max_dpi, error)) {
        return false;
    }
    std::optional<double> min_dpi;
    if (!parse_number(inputs_.min_dpi_text, tr("搜索下限"), false, min_dpi, error)) {
        return false;
    }
    out.max_dpi = max_dpi.value_or(kDefaultMaxDpi);
    out.min_dpi = min_dpi.value_or(kDefaultMinDpi);
    if (out.min_dpi >= out.max_dpi) {
        error = tr("搜索下限必须小于搜索上限");
        return false;
    }
    std::optional<double> jobs;
    if (!parse_number(inputs_.jobs_text, tr("渲染进程数"), false, jobs, error)) return false;
    std::optional<double> retries;
    if (!parse_number(inputs_.retries_text, tr("最大渲染次数"), false, retries, error)) {
        return false;
    }
    out.jobs = static_cast<int>(jobs.value_or(0.0));
    out.retries = std::max(1, static_cast<int>(retries.value_or(kDefaultRetries)));
    out.outdir = trim(inputs_.outdir);
    return true;
}

JobOptions App::make_options(const std::string& path, const Inputs& inputs) const {
    JobOptions options;
    options.input = path;
    options.target_size = inputs.target;
    options.dpi = inputs.dpi;
    options.output_dir = inputs.outdir;
    options.mode = inputs.mode;
    options.strategy = inputs.strategy;
    options.max_retries = inputs.retries;
    options.max_dpi = inputs.max_dpi;
    options.min_dpi = inputs.min_dpi;
    options.jobs = inputs.jobs;
    options.force_render = inputs.force;
    options.keep_page_box = inputs.keep_box;
    options.dry_run = inputs.dry_run;
    return options;
}

std::string App::result_note(const JobOptions& options, const std::string& path) const {
    // 结果文件名是 "<主名>_compressed.pdf"，放在输出目录（空 = 与输入同目录）。
    // 拿不到大小就不写备注，免得显示成 "→ 0 B"。
    const std::string base = path_stem(path);
    const std::string folder = options.output_dir.empty() ? path_dirname(path)
                                                          : options.output_dir;
    const std::string output = path_join(folder, base + "_compressed.pdf");
    const std::int64_t size = file_size(output);
    if (size <= 0) return {};
    return "→ " + human_size(static_cast<double>(size));
}

// ── 消息处理 ──────────────────────────────────────────────────

LRESULT App::handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            if (info != nullptr && min_width_ > 0 && min_height_ > 0) {
                info->ptMinTrackSize.x = min_width_;
                info->ptMinTrackSize.y = min_height_;
            }
            return 0;
        }

        case WM_DROPFILES: {
            // 从资源管理器拖进来的文件。整窗都愿意接：子控件（输入框、日志窗）
            // 收到时会在子类化里原样转给这里，HDROP 也由这里统一 DragFinish。
            const HDROP drop = reinterpret_cast<HDROP>(wparam);
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
            bool added = false;
            for (UINT index = 0; index < count; ++index) {
                const UINT length = DragQueryFileW(drop, index, nullptr, 0);
                if (length == 0) continue;
                std::wstring buffer(static_cast<std::size_t>(length) + 1, L'\0');
                if (DragQueryFileW(drop, index, buffer.data(), length + 1) == 0) continue;
                buffer.resize(length);
                const std::string path = wide_to_utf8(buffer);
                // 只收 .pdf：拖进一个 txt 或者文件夹，列表中会多出一条点开必然
                // 失败的行，不如直接忽略。
                const std::string name = to_lower(path_basename(path));
                if (name.size() <= 4 || name.compare(name.size() - 4, 4, ".pdf") != 0) {
                    continue;
                }
                add_file(path);
                added = true;
            }
            DragFinish(drop);
            if (added) {
                update_list_scroll();
                refresh_buttons();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_SIZE: {
            compute_layout();
            sync_native_controls();
            update_list_scroll();
            // HWND 渲染目标不会自己跟着窗口长：创建时多大就永远多大，拉大窗口后
            // 新露出来的那块区域会一直是空的（拖动过程中尤其明显）。
            if (target_ != nullptr && wparam != SIZE_MINIMIZED) {
                RECT client{};
                GetClientRect(hwnd, &client);
                target_->Resize(D2D1::SizeU(static_cast<UINT32>(std::max<LONG>(1, client.right)),
                                            static_cast<UINT32>(std::max<LONG>(1, client.bottom))));
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_DPICHANGED: {
            // 拖到另一个 DPI 的显示器上：字体和所有像素尺寸都要按新 DPI 重建
            dpi_ = HIWORD(wparam);
            if (dpi_ == 0) dpi_ = static_cast<UINT>(LOWORD(wparam));
            scale_ = static_cast<float>(dpi_) / 96.0f;
            fonts_.destroy();
            fonts_ready_ = false;
            build_fonts();
            if (fonts_ready_ && target_ != nullptr) {
                // 让渲染目标跟上新的客户区尺寸
                RECT client{};
                GetClientRect(hwnd, &client);
                target_->Resize(D2D1::SizeU(static_cast<UINT32>(std::max<LONG>(1, client.right)),
                                            static_cast<UINT32>(std::max<LONG>(1, client.bottom))));
            }
            compute_layout();
            sync_native_controls();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;   // 背景全部由 D2D 画，省掉一次整窗填充（也避免闪烁）

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            paint();
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_TIMER:
            if (wparam == kTimerDrain) {
                drain_messages();
                if (shutting_down_) {
                    // 关闭流程：等工作线程收尾（它可能还在等询问的默认值），
                    // 收到之后再退出消息循环
                    KillTimer(hwnd, kTimerDrain);
                    if (worker_.joinable()) worker_.join();
                    PostQuitMessage(0);
                }
            }
            return 0;

        case kMsgWorker:
            drain_messages();
            return 0;

        case kMsgStartJobs:
            start_jobs();
            return 0;

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            // 输入框与日志窗是原生 EDIT：底色和文字颜色只能靠这条消息对齐主题
            const HDC dc = reinterpret_cast<HDC>(wparam);
            const HWND control = reinterpret_cast<HWND>(lparam);
            const bool is_log = (control == field_log_.hwnd);
            SetTextColor(dc, to_colorref(theme_->text));
            SetBkColor(dc, to_colorref(is_log ? theme_->log_bg : theme_->field));
            return reinterpret_cast<LRESULT>(is_log ? log_brush_ : field_brush_);
        }

        case WM_MOUSEMOVE: {
            const float x = static_cast<float>(GET_X_LPARAM(lparam));
            const float y = static_cast<float>(GET_Y_LPARAM(lparam));
            set_hover_states(x, y);
            if (hot_scrollbar_ != nullptr) hot_scrollbar_->on_mouse_move(x, y);
            if (hwnd != nullptr) {
                InvalidateRect(hwnd, nullptr, FALSE);
                // 鼠标离开窗口时要收到 WM_MOUSELEAVE，否则悬停状态会一直亮着
                TRACKMOUSEEVENT track{};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = hwnd;
                TrackMouseEvent(&track);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            set_hover_states(-1.0f, -1.0f);
            list_hover_row_ = -1;
            list_scroll_.on_mouse_leave();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            const float x = static_cast<float>(GET_X_LPARAM(lparam));
            const float y = static_cast<float>(GET_Y_LPARAM(lparam));
            set_hover_states(x, y);

            // 询问条上的按钮最先判：它盖在"开始/取消"的位置上
            if (ask_button_count_ > 0) {
                for (int index = 0; index < ask_button_count_ && index < 4; ++index) {
                    if (btn_ask_[index].on_mouse_down(x, y)) {
                        hot_button_ = &btn_ask_[index];
                        return 0;
                    }
                }
            }
            if (list_scroll_.visible() && list_scroll_.on_mouse_down(x, y)) {
                hot_scrollbar_ = &list_scroll_;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            Button* buttons[] = {&btn_add_,    &btn_remove_, &btn_clear_,  &btn_lang_zh_,
                                 &btn_lang_en_, &btn_theme_,  &btn_outdir_, &btn_start_,
                                 &btn_cancel_};
            for (Button* button : buttons) {
                if (button->on_mouse_down(x, y)) {
                    hot_button_ = button;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            if (check_force_.on_click(x, y, scale_) || check_keep_.on_click(x, y, scale_) ||
                check_dry_.on_click(x, y, scale_)) {
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (drop_mode_.bounds.contains(x, y)) {
                const int picked = track_dropdown(drop_mode_);
                if (picked >= 0) {
                    drop_mode_.index = picked;
                    inputs_.mode = (picked == 1) ? Mode::Manual : Mode::Auto;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (drop_strategy_.bounds.contains(x, y)) {
                const int picked = track_dropdown(drop_strategy_);
                if (picked >= 0) {
                    drop_strategy_.index = picked;
                    inputs_.strategy = (picked == 1) ? Strategy::Smaller : Strategy::Closest;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (const std::optional<std::size_t> row = row_at(x, y)) {
                select_row(static_cast<int>(*row), has_ctrl());
                return 0;
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            const float x = static_cast<float>(GET_X_LPARAM(lparam));
            const float y = static_cast<float>(GET_Y_LPARAM(lparam));
            if (hot_scrollbar_ != nullptr) {
                hot_scrollbar_->on_mouse_up();
                ReleaseCapture();
                hot_scrollbar_ = nullptr;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (hot_button_ != nullptr) {
                Button* button = hot_button_;
                hot_button_ = nullptr;
                // 点击动作在这里统一分发：Button 只负责"按下和抬起都在按钮内"，
                // 具体做什么由窗口决定（这样重建界面换一批回调也不用改控件）。
                if (button->on_mouse_up(x, y)) {
                    if (button == &btn_add_) {
                        add_files_dialog();
                    } else if (button == &btn_remove_) {
                        remove_selected();
                    } else if (button == &btn_clear_) {
                        clear_files();
                    } else if (button == &btn_lang_zh_) {
                        set_language(Language::Zh);
                    } else if (button == &btn_lang_en_) {
                        set_language(Language::En);
                    } else if (button == &btn_theme_) {
                        toggle_theme();
                    } else if (button == &btn_outdir_) {
                        choose_output_dir();
                    } else if (button == &btn_start_) {
                        start_jobs();
                    } else if (button == &btn_cancel_) {
                        request_cancel();
                    } else {
                        for (int index = 0; index < 4; ++index) {
                            if (button == &btn_ask_[index]) {
                                answer_ask(static_cast<std::size_t>(index));
                                break;
                            }
                        }
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            // 兜底：把按下状态收干净（比如按在按钮上、拖出去再松开）
            set_hover_states(x, y);
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            const float x = static_cast<float>(GET_X_LPARAM(lparam));
            const float y = static_cast<float>(GET_Y_LPARAM(lparam));
            if (const std::optional<std::size_t> row = row_at(x, y)) {
                select_row(static_cast<int>(*row), false);
                open_selected_location();
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            const int steps = -GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
            // 滚轮消息的坐标是屏幕坐标
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &point);
            const float x = static_cast<float>(point.x);
            const float y = static_cast<float>(point.y);
            if (file_list_.bounds.contains(x, y) || !list_scroll_.visible()) {
                if (list_scroll_.scroll_by(steps * file_list_.row_height * 3)) {
                    file_list_.scroll = list_scroll_;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }

        case WM_SETCURSOR: {
            if (LOWORD(lparam) != HTCLIENT) break;
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd, &point);
            const float x = static_cast<float>(point.x);
            const float y = static_cast<float>(point.y);
            const bool hand = btn_add_.bounds.contains(x, y) ||
                              btn_remove_.bounds.contains(x, y) ||
                              btn_clear_.bounds.contains(x, y) ||
                              btn_lang_zh_.bounds.contains(x, y) ||
                              btn_lang_en_.bounds.contains(x, y) ||
                              btn_theme_.bounds.contains(x, y) ||
                              btn_outdir_.bounds.contains(x, y) ||
                              btn_start_.bounds.contains(x, y) ||
                              btn_cancel_.bounds.contains(x, y) ||
                              drop_mode_.bounds.contains(x, y) ||
                              drop_strategy_.bounds.contains(x, y) ||
                              check_force_.bounds.contains(x, y) ||
                              check_keep_.bounds.contains(x, y) ||
                              check_dry_.bounds.contains(x, y) ||
                              row_at(x, y).has_value();
            SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }

        case WM_KEYDOWN: {
            if (wparam == VK_RETURN) {
                if (ask_button_count_ > 0) {
                    answer_ask(0);   // 回车 = 第一个（强调色）按钮
                } else {
                    start_jobs();
                }
                return 0;
            }
            if (wparam == VK_ESCAPE) {
                request_cancel();
                return 0;
            }
            // 列表的方向键导航：只有前台窗口（也就是列表"持有焦点"）时才处理
            const bool list_focused = (GetFocus() == hwnd);
            if (list_focused && !files_.empty()) {
                int current = -1;
                for (std::size_t index = 0; index < files_.size(); ++index) {
                    if (files_[index].path == list_focus_path_) {
                        current = static_cast<int>(index);
                        break;
                    }
                }
                const int page = std::max(1, list_scroll_.view_height /
                                                std::max(1, file_list_.row_height));
                if (wparam == VK_DOWN) {
                    select_row(std::min<int>(static_cast<int>(files_.size()) - 1, current + 1),
                               false);
                    return 0;
                }
                if (wparam == VK_UP) {
                    select_row(std::max(0, current < 0 ? 0 : current - 1), false);
                    return 0;
                }
                if (wparam == VK_NEXT) {
                    select_row(std::min<int>(static_cast<int>(files_.size()) - 1, current + page),
                               false);
                    return 0;
                }
                if (wparam == VK_PRIOR) {
                    select_row(std::max(0, current - page), false);
                    return 0;
                }
                if (wparam == VK_HOME) {
                    select_row(0, false);
                    return 0;
                }
                if (wparam == VK_END) {
                    select_row(static_cast<int>(files_.size()) - 1, false);
                    return 0;
                }
                if (wparam == 'A' && has_ctrl()) {
                    selected_.clear();
                    for (const FileItem& item : files_) selected_.insert(item.path);
                    refresh_buttons();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                if (wparam == VK_DELETE) {
                    remove_selected();
                    return 0;
                }
                if (wparam == VK_SPACE) {
                    if (const std::optional<std::size_t> row = row_at(
                            file_list_.body.left + 1.0f,
                            file_list_.body.top + 1.0f)) {
                        (void)row;
                    }
                    return 0;
                }
            }
            return 0;
        }

        case WM_GETDLGCODE:
            // 列表用方向键选行，别让 IsDialogMessageW 把方向键吃掉
            return DLGC_WANTARROWS | DLGC_WANTCHARS;

        case WM_COMMAND:
            // EDIT 的 EN_CHANGE 会走到这里；界面状态在 start_jobs/保存设置时
            // 统一从控件读，所以这里不需要做什么。
            return 0;

        case WM_CLOSE:
            shutting_down_ = true;
            save_preferences();
            cancel_requested_ = true;
            {
                // 有没回答的询问就用默认值收场，否则工作线程会永远等下去
                std::lock_guard<std::mutex> lock(ask_mutex_);
                ask_answered_ = true;
                ask_answer_flag_ = ask_request_.cancel_default;
                ask_answer_choice_ = ask_request_.cancel_choice.empty()
                                         ? std::string("q")
                                         : ask_request_.cancel_choice;
            }
            ask_answer_ready_.notify_all();
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            shutting_down_ = true;
            // 不能在这里等线程：工作线程可能还在等我们的回答，等它就要靠
            // 消息循环继续跑。所以起一个定时器，由 WM_TIMER 收尾并退出循环。
            SetTimer(hwnd, kTimerDrain, 20, nullptr);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace pdfc::gui
