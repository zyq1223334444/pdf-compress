// -*- coding: utf-8 -*-
//
// 主窗口：界面骨架、自绘布局、消息循环，以及工作线程与界面之间的桥。
//
// 这个头文件只给 gui.cpp 用（run_gui 是唯一的出口），所以实现细节都放在 private
// 里，不对外暴露任何东西。界面逻辑全部落在 App 这一个类上，理由是这样"换语言 /
// 换主题整棵树重建"最省事：状态全在一处，重建就是把子控件销毁、重新布局、
// 再把状态填回去——不重建状态就一定会漏翻译或漏配色。
//
// 线程模型（改这里之前先读完）：
//   工作线程只做三件事——调 engine 的 run_job、把消息 new 出来 PostMessage 回
//   界面线程、在询问时阻塞等待。它绝不触碰任何 HWND、也绝不读控件内容：所有
//   参数在启动线程之前就已经从控件里读好，所有文字都靠消息传回来。
//   PostMessage 的载荷用裸指针 + 明确的所有权约定（见 window.cpp 的 post()），
//   窗口销毁后由析构统一回收，不会泄漏。
//
// 询问为什么要阻塞而不是弹模态对话框：
//   引擎的提问是同步接口（拿到用户的选择才能继续），但模态对话框会嵌套一层
//   消息循环，工作线程和界面线程的状态立刻变得很难推理（关闭窗口时对话框还在、
//   取消时对话框没关掉之类）。这里改成"工作线程在条件变量上等，界面线程把询问
//   条画在操作卡片里"：界面永远是同一个消息循环，关闭窗口时只需用默认值把
//   等待方唤醒，不可能卡死。
//
// 询问相关的三个变量为什么要分两份（ask_request_ / ask_visible_）：
//   ask_request_ 由 ask_mutex_ 保护，是"权威状态"；ask_visible_ 是给它做的一份
//   只读副本，绘制时直接读它，免得为了画一个按钮去抢锁（绘制里还要调
//   DirectWrite，持锁时间会长得没必要）。两份都在界面线程上写，所以不会撕裂。

#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <windowsx.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "config.hpp"
#include "engine.hpp"
#include "gui/theme.hpp"
#include "gui/widgets.hpp"
#include "i18n.hpp"
#include "options.hpp"
#include "search.hpp"

namespace pdfc::gui {

/// 工作线程 -> 界面线程的消息号（PostMessage 用）。
/// 载荷是 new 出来的 Message，由窗口过程负责 delete。
inline constexpr UINT kMsgWorker = WM_APP + 1;

class App {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    /// 建窗口、摆位置、跑消息循环；返回进程退出码。
    int Run(const std::vector<std::string>& files);

private:
    // ── 文件队列 ──────────────────────────────────────────────
    enum class RowStatus { Waiting, Working, Done, Failed, Cancelled };

    struct FileItem {
        std::string path;
        RowStatus status = RowStatus::Waiting;
        std::string note;          // "→ 1.02 MB" 这类附加说明
        std::int64_t size = 0;     // 入列时记下的原始大小（0 = 取不到）
    };

    // ── 工作线程 -> 界面线程的消息 ────────────────────────────
    enum class MessageKind { Log, Progress, Row, Ask, Finished };

    struct Message {
        MessageKind kind = MessageKind::Log;

        std::string text;                 // Log：整行日志
        int done = 0;                     // Progress
        int total = 0;                    // Progress
        double elapsed = 0.0;             // Progress

        std::size_t row = 0;              // Row：files_ 的下标
        RowStatus status = RowStatus::Waiting;
        std::string note;                 // Row

        int succeeded = 0;                // Finished
        int failed = 0;                   // Finished

        // Ask：询问条上的文字与按钮（文案已经在工作线程里翻译好）
        std::string ask_text;
        std::vector<std::string> ask_labels;
        std::vector<bool> ask_values;             // 是非题：按钮 -> true/false
        std::vector<std::string> ask_choices;     // 手动模式：按钮 -> "y"/"q"…
        bool ask_use_choice = false;              // 用 ask_choices 还是 ask_values
        bool ask_cancel_default = false;          // 取消/关闭时按哪个值收场
        std::string ask_cancel_choice = "q";
    };

    // ── 询问条的权威状态（由 ask_mutex_ 保护） ────────────────
    struct AskState {
        bool active = false;
        std::string text;
        std::vector<std::string> labels;
        std::vector<bool> values;
        std::vector<std::string> choices;
        bool use_choice = false;
        bool cancel_default = false;
        std::string cancel_choice = "q";
    };

    // ── 参数收集的结果（校验文案与 gui.py 逐字一致） ──────────
    struct Inputs {
        std::string target_text = "8";
        std::string dpi_text;
        std::string outdir;
        std::string retries_text;
        std::string jobs_text;
        std::string max_dpi_text;
        std::string min_dpi_text;
        std::optional<double> target;
        std::optional<double> dpi;
        double max_dpi = kDefaultMaxDpi;
        double min_dpi = kDefaultMinDpi;
        int jobs = 0;
        int retries = kDefaultRetries;
        Mode mode = Mode::Auto;
        Strategy strategy = Strategy::Closest;
        bool force = false;
        bool keep_box = false;
        bool dry_run = false;
    };

    // ── 消息循环各环节 ────────────────────────────────────────
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    bool ensure_render_target();
    void discard_render_target();
    void create_brushes();
    void destroy_brushes();
    void apply_titlebar_theme();

    void build_fonts();
    void compute_layout();
    void layout_widgets(float client_width, float client_height);
    void paint();
    void paint_card(const Rect& rect, float radius);
    void paint_grid();
    void paint_field_frames();
    void sync_native_controls();
    void destroy_native_controls();

    // ── 语言与主题 ────────────────────────────────────────────
    void toggle_theme();
    void set_language(Language language);
    void rebuild();                    // 换语言/换主题：整棵树重建
    void save_preferences();
    void load_preferences(const Settings& settings);
    void sync_widget_texts();
    void set_status(const std::string& text);

    // ── 文件列表 ──────────────────────────────────────────────
    void add_files_dialog();
    void choose_output_dir();
    void add_file(const std::string& path);
    void remove_selected();
    void clear_files();
    void open_selected_location();
    void select_row(int row, bool additive);
    void update_list_scroll();
    std::optional<std::size_t> row_at(float x, float y) const;
    std::string initial_dir() const;
    std::vector<FileRowText> list_rows() const;

    // ── 日志 ──────────────────────────────────────────────────
    void write_log(const std::string& line);

    // ── 开始 / 取消 ───────────────────────────────────────────
    void start_jobs();
    void request_cancel();
    void worker_main(std::vector<std::string> paths, JobOptions options);
    void handle_finished(int succeeded, int failed);

    // ── 参数 ──────────────────────────────────────────────────
    bool collect_inputs(Inputs& out, std::string& error) const;
    static bool parse_number(const std::string& text, const std::string& name,
                             bool allow_empty, std::optional<double>& out,
                             std::string& error);
    JobOptions make_options(const std::string& path, const Inputs& inputs) const;
    std::string result_note(const JobOptions& options, const std::string& path) const;
    /// 把控件里的文字读进 inputs_（重建界面之前、保存设置之前、开始压缩之前）。
    void capture_inputs();
    /// 把 inputs_ 写回控件（建好控件之后、重建之后）。
    void restore_inputs();

    // ── 询问 ──────────────────────────────────────────────────
    void post(Message* message);
    void drain_messages();
    void enter_ask(Message* message);
    void answer_ask(std::size_t index);
    // 引擎回调的三段包装：会阻塞工作线程直到用户点了按钮（或窗口关闭）
    bool ask_force_grow(std::int64_t size, std::int64_t target);
    bool ask_accept_lossless(std::int64_t size, std::int64_t target);
    std::string ask_manual(const Trial& trial, std::optional<double> hint_dpi);

    /// 用 TrackPopupMenu 弹出下拉项；返回选中的下标，取消返回 -1。
    int track_dropdown(const Dropdown& dropdown);

    void refresh_buttons();
    void set_hover_states(float x, float y);

    // ── 状态 ──────────────────────────────────────────────────
    HWND hwnd_ = nullptr;
    int min_width_ = 0;
    int min_height_ = 0;
    float scale_ = 1.0f;
    UINT dpi_ = 96;

    const Theme* theme_ = nullptr;
    FontSet fonts_;
    bool fonts_ready_ = false;

    ID2D1Factory* d2d_factory_ = nullptr;
    IDWriteFactory* dwrite_ = nullptr;
    ID2D1HwndRenderTarget* target_ = nullptr;
    DrawContext ctx_;
    HBRUSH field_brush_ = nullptr;   // WM_CTLCOLOR 用（按主题重建）
    HBRUSH log_brush_ = nullptr;

    bool shutting_down_ = false;
    bool in_paint_ = false;
    /// EndDraw 连着失败几帧才认定 Direct2D 真的用不了。只失败一两帧是常见的
    /// 启动期现象（渲染目标刚建好、设备还没就绪），重建一次就好了。
    int paint_failures_ = 0;
    /// 渲染目标建不起来时，这里记着是哪一步、什么错误码，弹窗时用得上
    std::string init_error_;
    /// 判定为真的用不了之后只弹一次说明，免得每帧都弹
    bool reported_paint_failure_ = false;

    std::vector<FileItem> files_;
    std::unordered_set<std::string> selected_;   // 选中的行（按路径，与顺序无关）
    int list_hover_row_ = -1;
    int list_anchor_ = -1;
    std::string list_focus_path_;

    std::vector<std::string> log_lines_;
    std::vector<std::string> log_text_cache_;    // 日志窗里当前是什么（省掉无谓重写）
    std::atomic<bool> cancel_requested_{false};
    bool cancel_pending_ = false;                // 已点取消、正在等线程收尾
    bool running_ = false;
    std::thread worker_;

    std::mutex queue_mutex_;
    std::vector<Message*> queue_;

    std::mutex ask_mutex_;
    std::condition_variable ask_answer_ready_;
    AskState ask_request_;                       // 工作线程提问后写这里
    AskState ask_visible_;                       // 绘制用的只读副本（见文件头说明）
    bool ask_pending_ = false;                   // 界面线程看到问题后置位
    bool ask_answered_ = false;
    bool ask_answer_flag_ = false;
    std::string ask_answer_choice_;

    // ── 不随重建丢失的界面状态 ────────────────────────────────
    Inputs inputs_;
    std::string status_text_;
    float progress_ = 0.0f;
    int ask_button_count_ = 0;

    // ── 布局结果 ──────────────────────────────────────────────
    Rect root_;
    Rect header_;
    Rect title_rect_;
    Rect subtitle_rect_;
    Rect files_card_;
    Rect files_body_;
    Rect files_head_;
    Rect files_hint_;
    Rect log_card_;
    Rect log_body_;
    Rect log_head_;
    Rect settings_card_;
    Rect settings_body_;
    Rect heading_rect_;
    Rect note_rect_;
    Rect action_card_;
    Rect action_body_;
    Rect ask_label_;
    std::vector<Rect> labels_;         // 表单标签（顺序见 layout_widgets / paint_grid）

    TextField field_target_;
    TextField field_dpi_;
    TextField field_outdir_;
    TextField field_retries_;
    TextField field_jobs_;
    TextField field_maxdpi_;
    TextField field_mindpi_;
    TextField field_log_;

    Button btn_add_;
    Button btn_remove_;
    Button btn_clear_;
    Button btn_lang_zh_;
    Button btn_lang_en_;
    Button btn_theme_;
    Button btn_outdir_;
    Button btn_start_;
    Button btn_cancel_;
    Button btn_ask_[4];

    Dropdown drop_mode_;
    Dropdown drop_strategy_;

    CheckBox check_force_;
    CheckBox check_keep_;
    CheckBox check_dry_;

    ProgressBar progress_bar_;
    FileList file_list_;
    ScrollBar list_scroll_;
    Button* hot_button_ = nullptr;        // 正在被按住的按钮
    ScrollBar* hot_scrollbar_ = nullptr;
};

}  // namespace pdfc::gui
