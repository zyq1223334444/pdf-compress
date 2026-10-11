// -*- coding: utf-8 -*-
//
// 压缩流程编排的实现。
//
// 结构照着 Python 版 pdfcomp/core.py 的 run_job 一段一段搬过来，连消息顺序都
// 保持原样，这样两版的终端输出可以逐行对照，出问题时一眼就能看出是哪一步
// 走岔了。唯一的结构性差异在"取消"：Python 用信号抛异常，C++ 用
// ui.stopped() 轮询 —— 渲染跑在子进程里，异常跨不了进程，只能靠轮询 + 杀子
// 进程（见 render_scale 的分块分支）。

#include "engine.hpp"

#include <algorithm>
// std::abs 的浮点重载由 <cmath> 提供；只靠 <cstdlib> 在部分编译器上拿到的是
// 整数版本，误差比较会悄悄变成整数比较。
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common.hpp"
#include "i18n.hpp"
#include "pdf.hpp"
#include "process.hpp"
#include "worker.hpp"

#if defined(_WIN32)
#  include <io.h>
#else
#  include <unistd.h>
#endif

namespace pdfc {
namespace {

// ── 与 Python 版逐字相同的常量 ─────────────────────────────────
constexpr std::int64_t kMaxPixelsPerPage = 40000000;   // 40 Mpx ≈ 120 MB RGB
constexpr double kLosslessMinGain = 0.01;              // 无损收益 <1% 不写副本
constexpr int kSequentialPageLimit = 4;                // 页数不超过它就不分块
constexpr const char* kTempPrefix = ".pdf_compress_";

/// Ctrl+C 只置一个标志，由 run_job 在可以安全停下的地方轮询。
/// Python 版是在信号处理里抛异常；C++ 这边渲染跑在子进程里，异常跨不了进程，
/// 只能"轮询 + 杀子进程"。
volatile std::sig_atomic_t g_interrupted = 0;

/// 临时目录的 RAII：跟 Python 的 finally: shutil.rmtree(run_dir) 等价。
/// run_job 的出口太多（成功/失败/中断各有好几个），用析构函数最不容易漏。
class TempDirGuard {
public:
    explicit TempDirGuard(const std::string& path) : path_(path) {}
    ~TempDirGuard() { remove_tree(path_); }
    TempDirGuard(const TempDirGuard&) = delete;
    TempDirGuard& operator=(const TempDirGuard&) = delete;

private:
    std::string path_;
};

/// 非预期失败的统一出口。Python 的 run_job 最外层是
/// `except Exception as exc: emit(tr("[失败] 发生错误：{error}"))`，
/// 渲染/写盘这类"没想到会失败"的错误都走这一条。
int report_failure(UiCallbacks& ui, const std::string& error) {
    ui.say(tr("[失败] 发生错误：{error}", {{"error", error}}));
    return kExitFail;
}

// ── 渲染结果 ──────────────────────────────────────────────────

enum class RenderOutcome { Ok, Failed, Interrupted };

std::string strategy_name(Strategy strategy) {
    // 这个字符串会出现在消息里（{strategy} 占位符），Python 版直接放的是
    // "closest"/"smaller" 这两个命令行取值，所以不翻译。
    return strategy == Strategy::Smaller ? "smaller" : "closest";
}

/// 相对原文件的变化（变大也是合理结果，不要写成"负的变小"）。
std::string size_change(std::int64_t size, std::int64_t original) {
    if (original <= 0) return tr("大小未知");
    const double ratio = static_cast<double>(size) / static_cast<double>(original);
    if (ratio < 1.0) {
        return tr("比原文件小 {percent:.1f}%",
                  {{"percent", fmt_fixed((1.0 - ratio) * 100.0, 1)}});
    }
    if (ratio > 1.0) {
        return tr("比原文件大 {percent:.1f}%",
                  {{"percent", fmt_fixed((ratio - 1.0) * 100.0, 1)}});
    }
    return tr("与原文件相同");
}

std::string pad_number(int value, int width) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%0*d", width, value);
    return buffer;
}

/// Windows 上 ifstream/ofstream/filesystem 的窄字符路径按本地代码页解释，
/// 中文路径会错，所以统一走 filesystem::path（UTF-8 → 宽字符构造）。
std::filesystem::path to_path(const std::string& text) {
#if defined(_WIN32)
    return std::filesystem::path(utf8_to_wide(text));
#else
    return std::filesystem::path(text);
#endif
}

/// 绝对化并规范化，用来判断"两个路径是不是同一个文件"。
/// 把路径变成绝对、干净的写法，**保留大小写**：它会被打进给用户看的消息里，
/// 也用来拼输出文件名，把小写强加给用户（"D:\报告.PDF" 显示成 "d:\报告.pdf"）
/// 是没道理的。
std::string normalized_path(const std::string& path) {
    std::error_code code;
    std::filesystem::path absolute = std::filesystem::absolute(to_path(path), code);
    if (code) absolute = to_path(path);
    absolute = absolute.lexically_normal();
#if defined(_WIN32)
    return wide_to_utf8(absolute.wstring());
#else
    return absolute.string();
#endif
}

/// 只在"这两个路径是不是同一个文件"的场合用：Windows 上路径大小写不敏感，
/// 同一个文件写成 C:\A.pdf 与 c:\a.pdf 系统看来是一个，若判成不同，finalize
/// 会去"把文件复制到它自己身上"而失败。
std::string path_comparison_key(const std::string& path) {
    std::string text = normalized_path(path);
#if defined(_WIN32)
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
#endif
    return text;
}

/// 把最佳结果放到最终路径上。
///
/// 无损结果可能就是输入文件本身（收益太低没写副本），这时只能复制，不能移动
/// —— 把用户的原始文件搬走是不可接受的。
bool finalize(const Trial& best, const std::string& final_path, std::string& error) {
    if (path_comparison_key(best.path) == path_comparison_key(final_path)) return true;
    if (best.is_input) {
        if (!copy_file(best.path, final_path)) {
            error = tr("无法写出文件：{path}", {{"path", final_path}});
            return false;
        }
        return true;
    }
    // 渲染结果是临时文件，改名最快；跨盘时才退回"复制 + 删除"
    std::error_code code;
    std::filesystem::rename(to_path(best.path), to_path(final_path), code);
    if (!code) return true;
    if (!copy_file(best.path, final_path)) {
        error = tr("无法写出文件：{path}", {{"path", final_path}});
        return false;
    }
    remove_file(best.path);
    return true;
}

void report_final(UiCallbacks& ui, const Trial& best, std::int64_t target, Strategy strategy,
                  std::int64_t original, int attempts, const std::vector<Trial>& trials) {
    const double deviation =
        static_cast<double>(best.size - target) / static_cast<double>(target);
    ui.say(tr("[结果] 最终大小 {size}（目标 {target:.2f} MB，误差 {deviation:+.1%}），"
              "{change}，共渲染 {attempts} 次",
              {{"size", human_size(static_cast<double>(best.size))},
               {"target", fmt_fixed(to_mb(static_cast<double>(target)), 2)},
               {"deviation", fmt_percent(deviation, 1)},
               {"change", size_change(best.size, original)},
               {"attempts", std::to_string(attempts)}}));
    if (is_accepted(best.size, target, strategy)) return;

    ui.say(tr("[警告] 未落在 {strategy} 策略要求的区间内，已输出最接近目标的结果",
              {{"strategy", strategy_name(strategy)}}));

    // smaller 策略下"勉强超过目标一点点"的结果会被放弃，转而输出小得多的
    // 文件；这里明确指出另一种选择，避免用户以为工具做不到。
    const Trial* closest = nullptr;
    for (const Trial& trial : trials) {
        if (closest == nullptr ||
            std::abs(trial.size - target) < std::abs(closest->size - target)) {
            closest = &trial;
        }
    }
    if (closest != nullptr && closest != &best &&
        std::abs(closest->size - target) < std::abs(best.size - target) / 2) {
        ui.say(tr("[提示] 另有更接近目标的结果 {size}（误差 {deviation:+.1%}），"
                  "但不满足 smaller 的“必须小于目标”要求；"
                  "如需它可以改用 --strategy closest",
                  {{"size", human_size(static_cast<double>(closest->size))},
                   {"deviation", fmt_percent(
                                     static_cast<double>(closest->size - target) /
                                         static_cast<double>(target),
                                     1)}}));
    }
}

/// 无损结果已小于目标时，决定是否仍要用有损渲染把文件做大。
///
/// 非交互环境默认不渲染（除非显式给出 --force-render）：把文件做大不会提高
/// 画质，只是体积变大，悄悄这么干是坑用户。
bool decide_force_grow(UiCallbacks& ui, const JobOptions& options, std::int64_t size,
                       std::int64_t target) {
    ui.say(tr("[提示] 无损优化后为 {size}，已经小于目标 {target:.2f} MB。",
              {{"size", human_size(static_cast<double>(size))},
               {"target", fmt_fixed(to_mb(static_cast<double>(target)), 2)}}));
    ui.say(tr("[提示] 继续做有损渲染只会让文件变大，不会提高画质或清晰度"
              "（分辨率、细节不会增加），仅仅是文件体积变大。"));
    if (options.force_render) {
        ui.say(tr("[提示] 已指定 --force-render，继续有损渲染以逼近目标大小"));
        return true;
    }
    if (ui.can_ask()) {
        return ui.ask_force_grow ? ui.ask_force_grow(size, target) : false;
    }
    ui.say(tr("[提示] 非交互环境，默认保留无损结果（如需强行逼近目标，请加 --force-render）"));
    return false;
}

/// 中断路径：保留已生成的最佳结果，返回 130。
int interrupted_path(UiCallbacks& ui, const std::vector<Trial>& trials, std::int64_t target,
                     Strategy strategy, const std::string& final_path,
                     std::int64_t original_size, double started) {
    ui.say("");
    ui.say(tr("[中断] 用户中断，正在保存已生成的最佳结果..."));
    const Trial* best =
        pick_best(trials, target > 0 ? target : original_size, strategy);
    if (best != nullptr) {
        std::string error;
        if (finalize(*best, final_path, error)) {
            ui.say(tr("[中断] 已保留最佳结果：{path}（{size}）",
                      {{"path", final_path},
                       {"size", human_size(static_cast<double>(best->size))}}));
        } else {
            ui.say(tr("[中断] 保存最佳结果失败：{error}", {{"error", error}}));
        }
    } else {
        ui.say(tr("[中断] 尚未生成任何结果，未输出文件"));
    }
    ui.say(tr("[中断] 用时 {elapsed:.1f}s，退出码 {code}",
              {{"elapsed", fmt_fixed(monotonic_seconds() - started, 1)},
               {"code", std::to_string(kExitInterrupt)}}));
    return kExitInterrupt;
}

/// 把页面均分成连续块，块数 ≤ 进程数（与 Python 的 _split_chunks 一致）。
std::vector<std::vector<RenderItem>> split_chunks(const std::vector<RenderItem>& items,
                                                  int workers) {
    std::vector<std::vector<RenderItem>> chunks;
    if (items.empty()) return chunks;
    const int usable =
        std::max(1, std::min(workers, static_cast<int>(items.size())));
    const std::size_t per_chunk =
        (items.size() + static_cast<std::size_t>(usable) - 1) /
        static_cast<std::size_t>(usable);
    for (std::size_t start = 0; start < items.size(); start += per_chunk) {
        const std::size_t end = std::min(start + per_chunk, items.size());
        chunks.emplace_back(items.begin() + static_cast<std::ptrdiff_t>(start),
                            items.begin() + static_cast<std::ptrdiff_t>(end));
    }
    return chunks;
}

bool read_text_file(const std::string& path, std::string& text) {
    std::ifstream stream(to_path(path), std::ios::binary);
    if (!stream) return false;
    text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return true;
}

/// 渲染压缩：每页渲染成 JPEG 再拼成新 PDF，结果写到 output。
///
/// scale 只决定图像像素数（DPI = 72 × scale），页面物理尺寸保持不变。
/// 与 Python 版一样：页数不多或进程数为 1 时顺序渲染，否则分块交给子进程。
RenderOutcome render_scale(const JobOptions& options, const std::string& input, double scale,
                           const std::string& output, int quality, int workers,
                           const std::string& temp_dir, UiCallbacks& ui,
                           std::int64_t& size, std::string& error) {
    size = 0;
    PagePlan plan;
    if (!plan_pages(input, scale, kMaxPixelsPerPage, plan, error)) {
        return RenderOutcome::Failed;
    }

    if (!plan.clipped.empty()) {
        const auto& first = plan.clipped.front();
        ui.say(tr("[警告] 第 {page} 页尺寸过大，已自动把分辨率从 {from_dpi:.0f} DPI "
                  "降到 {to_dpi:.0f} DPI（单页像素上限 {max_mpx:.0f} Mpx，避免内存溢出）",
                  {{"page", std::to_string(first.first)},
                   {"from_dpi", fmt_fixed(scale_to_dpi(scale), 0)},
                   {"to_dpi", fmt_fixed(scale_to_dpi(first.second), 0)},
                   {"max_mpx", fmt_fixed(static_cast<double>(kMaxPixelsPerPage) / 1e6, 0)}}));
    }

    const int total_pages = static_cast<int>(plan.items.size());

    // ── 顺序渲染 ──────────────────────────────────────────────
    if (total_pages <= kSequentialPageLimit || workers <= 1) {
        // 先看一眼要不要取消：Python 在每页渲染前都查一次，这里没有单页回调，
        // 只能在动手之前查一次（页数 ≤ 4，代价可以忽略）。
        if (ui.stopped()) return RenderOutcome::Interrupted;
        int done = 0;
        std::string render_error;
        // 进度里的 elapsed 传 0：Python 版的顺序分支也是 progress(done, total)，
        // 不带时间戳，两边输出才能逐行对上。
        const bool ok = render_pages(
            input, plan.items, quality, options.keep_page_box, output, render_error,
            [&done, total_pages, &ui]() {
                ++done;
                ui.report_progress(done, total_pages, 0.0);
            });
        if (!ok) {
            error = render_error;
            return RenderOutcome::Failed;
        }
        if (ui.stopped()) return RenderOutcome::Interrupted;
        size = file_size(output);
        if (size <= 0) {
            error = tr("渲染结果为空文件");
            return RenderOutcome::Failed;
        }
        return RenderOutcome::Ok;
    }

    // ── 分块并行渲染 ──────────────────────────────────────────
    // 块数不超过进程数，所以可以一次全部启起来、等全部结束，结果与"完成一块
    // 再补一块"完全一样，实现却简单得多。
    const std::vector<std::vector<RenderItem>> chunks = split_chunks(plan.items, workers);
    if (chunks.empty()) {
        error = tr("没有可渲染的页面");
        return RenderOutcome::Failed;
    }
    // 已经要求取消了就别再启子进程了（Python 的 submit_ready 也是先查 cancel）
    if (ui.stopped()) return RenderOutcome::Interrupted;

    std::vector<ChildProcess> children;
    std::vector<std::string> chunk_paths;
    std::vector<int> chunk_pages;
    children.reserve(chunks.size());
    chunk_paths.reserve(chunks.size());
    chunk_pages.reserve(chunks.size());

    for (std::size_t index = 0; index < chunks.size(); ++index) {
        const std::string name = "chunk_" + pad_number(static_cast<int>(index), 4);
        const std::string chunk_path = path_join(temp_dir, name + ".pdf");
        const std::string spec_path = path_join(temp_dir, name + ".spec");

        ChunkSpec spec;
        spec.input = input;
        spec.output = chunk_path;
        spec.jpeg_quality = quality;
        spec.keep_page_box = options.keep_page_box;
        spec.items = chunks[index];

        std::string problem;
        if (!write_chunk_spec(spec_path, spec, problem)) {
            error = problem;
            for (ChildProcess& child : children) terminate_child(child);
            for (ChildProcess& child : children) close_child(child);
            return RenderOutcome::Failed;
        }
        ChildProcess child;
        // 把语言显式传给子进程：它自己也会去查设置/区域，但命令行上的 --lang
        // 只对父进程生效，不传的话英文会话里子进程的报错会变回中文。
        if (!spawn_child({"--worker", spec_path, "--lang", language_code(language())}, child,
                         problem)) {
            error = problem;
            for (ChildProcess& running : children) terminate_child(running);
            for (ChildProcess& running : children) close_child(running);
            return RenderOutcome::Failed;
        }
        children.push_back(child);
        chunk_paths.push_back(chunk_path);
        chunk_pages.push_back(static_cast<int>(chunks[index].size()));
    }

    const double started = monotonic_seconds();
    int done_pages = 0;
    bool interrupted = false;
    std::string first_error;
    for (std::size_t index = 0; index < children.size(); ++index) {
        if (ui.stopped()) {
            // 取消语义：已经在跑的块就地终止，不再等它们把整份文档渲染完
            interrupted = true;
            for (std::size_t rest = index; rest < children.size(); ++rest) {
                terminate_child(children[rest]);
            }
            break;
        }
        int exit_code = 0;
        std::string problem;
        if (!wait_child(children[index], exit_code, problem)) {
            if (first_error.empty()) first_error = problem;
        } else if (exit_code != 0) {
            if (first_error.empty()) {
                // 子进程把说明写在 <输出>.err 里（它不能写标准输出）
                std::string detail;
                if (read_text_file(chunk_error_path(chunk_paths[index]), detail) &&
                    !detail.empty()) {
                    first_error = detail;
                } else {
                    first_error = tr("渲染子进程异常退出（退出码 {code}）",
                                     {{"code", std::to_string(exit_code)}});
                }
            }
        }
        if (first_error.empty()) {
            // 子进程不报单页进度，父进程按"完成的块所含页数"累加
            done_pages += chunk_pages[index];
            ui.report_progress(done_pages, total_pages, monotonic_seconds() - started);
        }
    }
    for (ChildProcess& child : children) close_child(child);

    if (interrupted) return RenderOutcome::Interrupted;
    if (!first_error.empty()) {
        error = first_error;
        return RenderOutcome::Failed;
    }
    if (!merge_pdfs(chunk_paths, output, error)) return RenderOutcome::Failed;
    size = file_size(output);
    if (size <= 0) {
        error = tr("渲染结果为空文件");
        return RenderOutcome::Failed;
    }
    return RenderOutcome::Ok;
}

/// 手动模式：参数由用户逐步调整（无效输入不会触发重新渲染）。
int manual_flow(UiCallbacks& ui, const JobOptions& options, const std::string& input_path,
                std::vector<Trial>& trials, const std::string& run_dir,
                const std::string& final_path, std::int64_t target,
                std::int64_t original_size, int workers, double smin, double smax,
                double started) {
    const Trial lossless = trials.front();
    ui.say(tr("[手动] 无损优化结果：{size}（目标 {target:.2f} MB）",
              {{"size", human_size(static_cast<double>(lossless.size))},
               {"target", fmt_fixed(to_mb(static_cast<double>(target)), 2)}}));

    // 非交互环境问不了，按原来的默认值处理：直接接受无损结果
    const bool accept_lossless =
        ui.can_ask() ? (ui.ask_accept_lossless ? ui.ask_accept_lossless(lossless.size, target)
                                              : true)
                     : true;
    if (accept_lossless) {
        std::string error;
        if (!finalize(lossless, final_path, error)) return report_failure(ui, error);
        ui.say(tr("[成功] 已保存无损优化结果：{path}", {{"path", final_path}}));
        return kExitOk;
    }

    double scale = std::min(std::max(1.0, smin), smax);
    int quality = kQualityDefault;
    int attempt = 0;
    for (;;) {
        ++attempt;
        const std::string path =
            path_join(run_dir, "manual" + pad_number(attempt, 2) + ".pdf");
        ui.say(tr("[渲染] 第 {attempt} 次：{dpi:.0f} DPI（scale={scale:.3f}），JPEG {quality}",
                  {{"attempt", std::to_string(attempt)},
                   {"dpi", fmt_fixed(scale_to_dpi(scale), 0)},
                   {"scale", fmt_fixed(scale, 3)},
                   {"quality", std::to_string(quality)}}));

        std::int64_t size = 0;
        std::string error;
        const RenderOutcome outcome = render_scale(options, input_path, scale, path, quality,
                                                   workers, run_dir, ui, size, error);
        if (outcome == RenderOutcome::Interrupted) {
            return interrupted_path(ui, trials, target, options.strategy, final_path,
                                    original_size, started);
        }
        if (outcome == RenderOutcome::Failed) return report_failure(ui, error);

        Trial trial;
        trial.scale = scale;
        trial.quality = quality;
        trial.size = size;
        trial.path = path;
        trials.push_back(trial);

        const double deviation =
            static_cast<double>(size - target) / static_cast<double>(target);
        ui.say(tr("[渲染] 当前大小 {size}（目标 {target:.2f} MB，误差 {deviation:+.1%}）",
                  {{"size", human_size(static_cast<double>(size))},
                   {"target", fmt_fixed(to_mb(static_cast<double>(target)), 2)},
                   {"deviation", fmt_percent(deviation, 1)}}));

        // 先算建议，再读输入：无效输入只会重新提问，不会重新渲染
        const std::optional<double> hint =
            predict_scale(trials, aim_size(target, options.strategy), smin, smax);
        // 回调没设时按 Python 版 Ui 基类的默认值走（ask_manual -> "y"）
        const std::string answer =
            ui.ask_manual ? ui.ask_manual(trials.back(), hint) : std::string("y");
        if (answer == "y" || answer == "yes" || answer == "是") {
            std::string finalize_error;
            if (!finalize(trials.back(), final_path, finalize_error)) {
                return report_failure(ui, finalize_error);
            }
            ui.say(tr("[成功] 已保存：{path}", {{"path", final_path}}));
            report_final(ui, trials.back(), target, options.strategy, original_size, attempt,
                         trials);
            return kExitOk;
        }
        if (answer == "q" || answer == "quit" || answer == "exit") {
            const Trial* best = pick_best(trials, target, options.strategy);
            if (best != nullptr) {
                std::string finalize_error;
                if (!finalize(*best, final_path, finalize_error)) {
                    return report_failure(ui, finalize_error);
                }
                ui.say(tr("[成功] 已保存最佳结果：{path}（{size}）",
                          {{"path", final_path},
                           {"size", human_size(static_cast<double>(best->size))}}));
            }
            return kExitOk;
        }

        // 分辨率顶到上下限之后就改用 JPEG 质量当副旋钮
        const bool larger = answer == "larger";
        const int quality_step = larger ? 5 : -5;
        if (scale >= smax || (scale > smax * 0.95 && answer == "larger")) {
            scale = smax;
            quality = std::max(kQualityMin, std::min(kQualityMax, quality + quality_step));
            ui.say(tr("[提示] 分辨率已达上限 {dpi:.0f} DPI，改为调整 JPEG 质量",
                      {{"dpi", fmt_fixed(scale_to_dpi(smax), 0)}}));
        } else if (scale <= smin || (scale < smin * 1.05 && answer == "smaller")) {
            scale = smin;
            quality = std::max(kQualityMin, std::min(kQualityMax, quality + quality_step));
            ui.say(tr("[提示] 分辨率已达下限 {dpi:.0f} DPI，改为调整 JPEG 质量",
                      {{"dpi", fmt_fixed(scale_to_dpi(smin), 0)}}));
        } else {
            const double step = larger ? 0.15 : -0.15;
            scale = std::max(smin, std::min(smax, scale + step));
        }
    }
}

// ── 控制台界面的输入辅助 ──────────────────────────────────────

/// 读一行输入；EOF 或中断返回 nullopt，由调用方套默认值。
/// 提示语不换行（Python 的 input(prompt) 也是这样）。
std::optional<std::string> read_answer(const std::string& question) {
    std::fputs(question.c_str(), stdout);
    std::fflush(stdout);
    std::string answer;
    if (!std::getline(std::cin, answer)) return std::nullopt;
    return answer;
}

}  // namespace

UiCallbacks console_ui() {
    UiCallbacks ui;

    ui.emit = [](const std::string& line) { emit(line); };

    // 只在完成页数变化时打印：分块渲染时同一页数可能被报两次
    ui.progress = [](int done, int total, double elapsed) {
        static int last_done = -1;
        if (done == last_done) return;
        last_done = done;
        const double percent =
            total > 0 ? static_cast<double>(done) / static_cast<double>(total) * 100.0
                      : 100.0;
        emit(tr("[渲染] 进度 {done}/{total} 页（{percent:.0f}%），已用 {elapsed:.1f}s",
                {{"done", std::to_string(done)},
                 {"total", std::to_string(total)},
                 {"percent", fmt_fixed(percent, 0)},
                 {"elapsed", fmt_fixed(elapsed, 1)}}));
    };

    // Ctrl+C：Python 版靠信号抛 Cancelled，这里只置一个标志，由 run_job 在
    // 每个可以安全停下的地方轮询。提问时按 Ctrl+C 也只会让 getline 失败，
    // 于是走"回答默认值"那条路——与 Python 的行为一致。
    std::signal(SIGINT, [](int) { g_interrupted = 1; });
    ui.cancelled = [] { return g_interrupted != 0; };

#if defined(_WIN32)
    ui.interactive = _isatty(_fileno(stdin)) != 0 && _isatty(_fileno(stdout)) != 0;
#else
    ui.interactive = ::isatty(STDIN_FILENO) != 0 && ::isatty(STDOUT_FILENO) != 0;
#endif

    ui.ask_force_grow = [](std::int64_t size, std::int64_t target) {
        (void)size;
        const std::optional<std::string> answer = read_answer(
            tr("是否使用有损渲染强行把文件做大到 {target:.2f} MB 附近？"
               "（不会提高画质，只会变大）[y/N]: ",
               {{"target", fmt_fixed(to_mb(static_cast<double>(target)), 2)}}));
        if (!answer.has_value()) return false;
        const std::string text = to_lower(trim(*answer));
        return text == "y" || text == "yes" || text == "是" || text == "1";
    };

    ui.ask_accept_lossless = [](std::int64_t size, std::int64_t target) {
        (void)size;
        (void)target;
        const std::optional<std::string> answer =
            read_answer(tr("是否直接接受无损优化结果？(y/n): "));
        if (!answer.has_value()) return true;
        const std::string text = to_lower(trim(*answer));
        return !(text == "n" || text == "no" || text == "否" || text == "0");
    };

    ui.ask_manual = [](const Trial& trial, std::optional<double> hint_dpi) {
        // 提示语由界面层打印：ask_manual 的签名里带着建议分辨率，图形界面
        // 也就能把它显示在对话框里，引擎层不必知道屏幕长什么样。
        if (hint_dpi.has_value()) {
            emit(tr("[提示] 若要命中目标，建议分辨率约 {hint:.0f} DPI（当前 {now:.0f} DPI）",
                    {{"hint", fmt_fixed(*hint_dpi, 0)},
                     {"now", fmt_fixed(trial.dpi(), 0)}}));
        }
        emit(tr("[提示] 输入 y=接受当前结果，larger=提高分辨率/质量，"
                "smaller=降低分辨率/质量，q=退出并保留最佳结果"));
        for (;;) {
            const std::optional<std::string> answer =
                read_answer(tr("请选择 (y/larger/smaller/q): "));
            if (!answer.has_value()) return std::string("y");
            const std::string text = to_lower(trim(*answer));
            if (text == "y" || text == "yes" || text == "是" || text == "larger" ||
                text == "smaller" || text == "q" || text == "quit" || text == "exit") {
                return text;
            }
            emit(tr("[提示] 无效输入，请输入 y / larger / smaller / q"));
        }
    };

    return ui;
}

int run_job(const JobOptions& options, UiCallbacks& ui) {
    const std::string input_path = normalized_path(options.input);
    if (!is_regular_file(input_path)) {
        ui.say(tr("[失败] 文件不存在：{path}", {{"path", input_path}}));
        return kExitFail;
    }

    int total_pages = 0;
    std::string error;
    if (!probe_document(input_path, total_pages, error)) {
        ui.say(tr("[失败] {error}", {{"error", error}}));
        return kExitFail;
    }

    // ── 参数 ──────────────────────────────────────────────────
    std::optional<double> fixed_scale;
    std::int64_t target = 0;
    if (options.dpi.has_value()) {
        if (*options.dpi <= 0.0) {
            ui.say(tr("[失败] --dpi 必须大于 0"));
            return kExitFail;
        }
        fixed_scale = dpi_to_scale(*options.dpi);
        if (options.target_size.has_value() && *options.target_size != 0.0) {
            ui.say(tr("[提示] 已指定 --dpi {dpi:g}，忽略目标大小 {size:g} MB",
                      {{"dpi", fmt_general(*options.dpi)},
                       {"size", fmt_general(*options.target_size)}}));
        }
        target = options.target_size.has_value() ? mb_to_bytes(*options.target_size) : 0;
    } else {
        if (!options.target_size.has_value()) {
            ui.say(tr("[失败] 需要给出目标大小(MB)，或使用 --dpi 指定固定分辨率"));
            return kExitFail;
        }
        if (*options.target_size <= 0.0) {
            ui.say(tr("[失败] 目标大小必须大于 0"));
            return kExitFail;
        }
        target = mb_to_bytes(*options.target_size);
    }

    const double dpi_max = options.max_dpi;
    const double dpi_min = options.min_dpi;
    if (dpi_min >= dpi_max) {
        ui.say(tr("[失败] --min-dpi({min:g}) 必须小于 --max-dpi({max:g})",
                  {{"min", fmt_general(dpi_min)}, {"max", fmt_general(dpi_max)}}));
        return kExitFail;
    }
    const double smin = dpi_to_scale(dpi_min);
    const double smax = dpi_to_scale(dpi_max);

    std::string output_dir =
        options.output_dir.empty() ? path_dirname(input_path) : options.output_dir;
    if (output_dir.empty()) output_dir = ".";
    if (!make_directories(output_dir)) {
        ui.say(tr("[失败] 无法创建输出目录：{error}", {{"error", output_dir}}));
        return kExitFail;
    }

    const std::string final_path =
        path_join(output_dir, path_stem(input_path) + "_compressed.pdf");
    const std::int64_t original_size = file_size(input_path);
    const int workers = resolve_workers(options.jobs);

    ui.say(tr("[任务] 输入 {name}（{size}，{pages} 页）",
              {{"name", path_basename(input_path)},
               {"size", human_size(static_cast<double>(original_size))},
               {"pages", std::to_string(total_pages)}}));
    if (fixed_scale.has_value()) {
        ui.say(tr("[任务] 固定分辨率 {dpi:g} DPI（scale={scale:.3f}），进程数 {workers}",
                  {{"dpi", fmt_general(*options.dpi)},
                   {"scale", fmt_fixed(*fixed_scale, 3)},
                   {"workers", std::to_string(workers)}}));
    } else {
        ui.say(tr("[任务] 目标 {target:.2f} MB，策略 {strategy}，"
                  "分辨率范围 {min:g}~{max:g} DPI，进程数 {workers}",
                  {{"target", fmt_fixed(to_mb(static_cast<double>(target)), 2)},
                   {"strategy", strategy_name(options.strategy)},
                   {"min", fmt_general(dpi_min)},
                   {"max", fmt_general(dpi_max)},
                   {"workers", std::to_string(workers)}}));
    }

    if (options.dry_run) {
        ui.say(tr("[提示] --dry-run：仅显示计划，不做实际压缩"));
        return kExitOk;
    }

    std::string temp_error;
    const std::string run_dir = make_temp_dir(output_dir, kTempPrefix, &temp_error);
    if (run_dir.empty()) {
        ui.say(tr("[失败] 无法在输出目录里创建临时目录：{error}", {{"error", temp_error}}));
        return kExitFail;
    }
    // finally 语义：无论从哪条路返回（成功、失败、中断），临时目录都要删掉
    const TempDirGuard temp_guard(run_dir);

    std::vector<Trial> trials;
    int attempts = 0;
    const double started = monotonic_seconds();

    // ── 固定 DPI：一次渲染结束 ────────────────────────────────
    if (fixed_scale.has_value()) {
        const std::string path = path_join(run_dir, "fixed.pdf");
        ui.say(tr("[渲染] 第 1/1 次：{dpi:g} DPI（scale={scale:.3f}），"
                  "JPEG {quality}（--dpi 固定分辨率，不做搜索）",
                  {{"dpi", fmt_general(*options.dpi)},
                   {"scale", fmt_fixed(*fixed_scale, 3)},
                   {"quality", std::to_string(kQualityDefault)}}));

        std::int64_t size = 0;
        const RenderOutcome outcome =
            render_scale(options, input_path, *fixed_scale, path, kQualityDefault, workers,
                         run_dir, ui, size, error);
        if (outcome == RenderOutcome::Interrupted) {
            // 与 Python 一致：中断时这一次的结果不算数（trials 里还没有它）
            return interrupted_path(ui, trials, target, options.strategy, final_path,
                                    original_size, started);
        }
        if (outcome == RenderOutcome::Failed) return report_failure(ui, error);

        Trial trial;
        trial.scale = *fixed_scale;
        trial.quality = kQualityDefault;
        trial.size = size;
        trial.path = path;
        trials.push_back(trial);
        attempts = 1;

        if (!finalize(trials.front(), final_path, error)) {
            return report_failure(ui, error);
        }
        ui.say(tr("[成功] 已按 {dpi:g} DPI 输出：{path}",
                  {{"dpi", fmt_general(*options.dpi)}, {"path", final_path}}));
        ui.say(tr("[结果] 文件大小 {size}（{change}），页数 {pages}，用时 {elapsed:.1f}s",
                  {{"size", human_size(static_cast<double>(size))},
                   {"change", size_change(size, original_size)},
                   {"pages", std::to_string(total_pages)},
                   {"elapsed", fmt_fixed(monotonic_seconds() - started, 1)}}));
        if (target > 0) {
            report_final(ui, trials.front(), target, options.strategy, original_size,
                         attempts, trials);
        }
        return kExitOk;
    }

    // ── 第一阶段：无损优化 ────────────────────────────────────
    ui.say(tr("[无损] 正在做无损优化（不改变画质）..."));
    std::vector<unsigned char> optimized;
    if (!optimize_lossless(input_path, optimized, error)) {
        ui.say(tr("[失败] {error}", {{"error", error}}));
        return kExitFail;
    }
    const std::int64_t optimized_size = static_cast<std::int64_t>(optimized.size());
    const double gain =
        1.0 - static_cast<double>(optimized_size) / static_cast<double>(original_size);
    ui.say(tr("[无损] {before} → {after}（收益 {gain:+.1%}）",
              {{"before", human_size(static_cast<double>(original_size))},
               {"after", human_size(static_cast<double>(optimized_size))},
               {"gain", fmt_percent(gain, 1)}}));

    if (is_accepted(optimized_size, target, options.strategy)) {
        const std::string path = path_join(run_dir, "lossless.pdf");
        if (!write_file(path, optimized.data(), optimized.size())) {
            return report_failure(ui, tr("无法写出临时文件：{path}", {{"path", path}}));
        }
        Trial trial;
        trial.size = optimized_size;
        trial.path = path;
        if (!finalize(trial, final_path, error)) {
            return report_failure(ui, error);
        }
        ui.say(tr("[成功] 无损优化后即符合策略，已保存：{path}", {{"path", final_path}}));
        ui.say(tr("[结果] 文件大小 {size}（目标 {target:.2f} MB，误差 {deviation:+.1%}）",
                  {{"size", human_size(static_cast<double>(optimized_size))},
                   {"target", fmt_fixed(to_mb(static_cast<double>(target)), 2)},
                   {"deviation",
                    fmt_percent(static_cast<double>(optimized_size - target) /
                                    static_cast<double>(target),
                                1)}}));
        return kExitOk;
    }

    if (gain >= kLosslessMinGain) {
        const std::string path = path_join(run_dir, "lossless.pdf");
        if (!write_file(path, optimized.data(), optimized.size())) {
            return report_failure(ui, tr("无法写出临时文件：{path}", {{"path", path}}));
        }
        Trial trial;
        trial.size = optimized_size;
        trial.path = path;
        trials.push_back(trial);
    } else {
        ui.say(tr("[无损] 收益可忽略（<1%），跳过写出临时副本"));
        // 无损没有收益时不写副本：直接把原文件当成无损结果参与挑选
        Trial trial;
        trial.size = original_size;
        trial.path = input_path;
        trial.is_input = true;
        trials.push_back(trial);
    }
    optimized.clear();
    optimized.shrink_to_fit();

    // ── 无损结果已小于目标：要用户决定是否强行做大 ────────────
    // 注意用严格小于：恰好等于目标时，smaller 策略仍需要更小，应继续搜索
    if (optimized_size < target) {
        if (!decide_force_grow(ui, options, optimized_size, target)) {
            if (!finalize(trials.front(), final_path, error)) {
                ui.say(tr("[失败] {error}", {{"error", error}}));
                return kExitFail;
            }
            ui.say(tr("[成功] 已保留无损优化结果：{path}", {{"path", final_path}}));
            ui.say(
                tr("[结果] 文件大小 {size}（目标 {target:.2f} MB，误差 {deviation:+.1%}）",
                   {{"size", human_size(static_cast<double>(trials.front().size))},
                    {"target", fmt_fixed(to_mb(static_cast<double>(target)), 2)},
                    {"deviation",
                     fmt_percent(static_cast<double>(trials.front().size - target) /
                                     static_cast<double>(target),
                                 1)}}));
            return kExitOk;
        }
    }

    // ── 第二阶段：渲染压缩 ────────────────────────────────────
    const auto [low, high] = accept_range(target, options.strategy);
    ui.say(tr("[渲染] 需要命中的区间：{low:.2f} ~ {high:.2f} MB（策略 {strategy}）",
              {{"low", fmt_fixed(to_mb(low), 2)},
               {"high", fmt_fixed(to_mb(high), 2)},
               {"strategy", strategy_name(options.strategy)}}));

    if (options.mode == Mode::Manual) {
        return manual_flow(ui, options, input_path, trials, run_dir, final_path, target,
                           original_size, workers, smin, smax, started);
    }

    const int max_attempts = std::max(1, options.max_retries);
    Decision decision = plan_next(trials, target, options.strategy, smin, smax);
    while (attempts < max_attempts && !decision.stop) {
        if (ui.stopped()) {
            return interrupted_path(ui, trials, target, options.strategy, final_path,
                                    original_size, started);
        }
        ++attempts;
        const double scale = decision.scale;
        const int quality = decision.quality;
        const std::string path =
            path_join(run_dir, "try" + pad_number(attempts, 2) + ".pdf");

        ui.say(tr("[渲染] 第 {attempt}/{total} 次：{dpi:.0f} DPI（scale={scale:.3f}），"
                  "JPEG {quality}",
                  {{"attempt", std::to_string(attempts)},
                   {"total", std::to_string(max_attempts)},
                   {"dpi", fmt_fixed(scale_to_dpi(scale), 0)},
                   {"scale", fmt_fixed(scale, 3)},
                   {"quality", std::to_string(quality)}}));

        std::int64_t size = 0;
        const RenderOutcome outcome = render_scale(options, input_path, scale, path, quality,
                                                   workers, run_dir, ui, size, error);
        if (outcome == RenderOutcome::Interrupted) {
            return interrupted_path(ui, trials, target, options.strategy, final_path,
                                    original_size, started);
        }
        if (outcome == RenderOutcome::Failed) return report_failure(ui, error);

        Trial trial;
        trial.scale = scale;
        trial.quality = quality;
        trial.size = size;
        trial.path = path;
        trials.push_back(trial);

        ui.say(tr("[渲染] 得到 {size}（{ratio:.3f} 倍目标，误差 {deviation:+.1%}）",
                  {{"size", human_size(static_cast<double>(size))},
                   {"ratio", fmt_fixed(static_cast<double>(size) /
                                           static_cast<double>(target),
                                       3)},
                   {"deviation",
                    fmt_percent(static_cast<double>(size - target) /
                                    static_cast<double>(target),
                                1)}}));

        if (is_accepted(size, target, options.strategy)) {
            if (!finalize(trials.back(), final_path, error)) {
                return report_failure(ui, error);
            }
            ui.say(tr("[成功] 已命中目标：{path}", {{"path", final_path}}));
            report_final(ui, trials.back(), target, options.strategy, original_size,
                         attempts, trials);
            return kExitOk;
        }

        decision = plan_next(trials, target, options.strategy, smin, smax);
        if (!decision.stop) {
            ui.say(tr("[搜索] 下一次预计：{dpi:.0f} DPI，JPEG {quality}",
                      {{"dpi", fmt_fixed(scale_to_dpi(decision.scale), 0)},
                       {"quality", std::to_string(decision.quality)}}));
        }
    }

    if (decision.stop) {
        ui.say(tr("[警告] {reason}", {{"reason", decision.reason}}));
    } else {
        ui.say(tr("[警告] 已达到最大尝试次数（{max}），未能落入目标区间",
                  {{"max", std::to_string(max_attempts)}}));
    }

    const Trial* best = pick_best(trials, target, options.strategy);
    if (best == nullptr) {
        ui.say(tr("[失败] 没有生成任何可用结果"));
        return kExitFail;
    }
    if (!finalize(*best, final_path, error)) {
        return report_failure(ui, error);
    }
    ui.say(tr("[成功] 已输出最接近目标的结果：{path}", {{"path", final_path}}));
    report_final(ui, *best, target, options.strategy, original_size, attempts, trials);
    if (best->rendered()) {
        ui.say(tr("[提示] 该结果相当于 {dpi:.0f} DPI / JPEG {quality}；"
                  "如可接受其他大小，可据此调整目标值",
                  {{"dpi", fmt_fixed(best->dpi(), 0)},
                   {"quality", std::to_string(best->quality)}}));
    }
    return kExitOk;
}

}  // namespace pdfc
