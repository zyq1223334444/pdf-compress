// -*- coding: utf-8 -*-
//
// 命令行入口：参数解析、帮助文本、启动方式分派。
//
// 参数解析是手写的，没有引第三方库：选项只有十来个，而手写才能把错误信息
// 也一并本地化（argparse 那类库只给英文），并支持 `--opt=value` 与
// `--opt value` 两种写法。

#include "cli.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "common.hpp"
#include "config.hpp"
#include "engine.hpp"
#include "gui/gui.hpp"
#include "i18n.hpp"
#include "options.hpp"
#include "shellmenu.hpp"
#include "worker.hpp"

namespace pdfc {

// ── 帮助文本 ──────────────────────────────────────────────────
//
// 中英各一份完整文本，而不是从同一份模板生成：这两种语言的排版习惯差别不小
// （中文用全角括号、英文要断行更早），各写一份反而更好读。两份的选项顺序必须
// 完全一致，改一个记得改另一个。

const char* help_zh() {
    return R"HELP(
PDF Compressor —— 把 PDF 压到指定大小，或按指定分辨率重新渲染

用法:
  pdf_compress                       打开图形界面（不带参数时的默认行为）
  pdf_compress <文件.pdf> ...        打开图形界面，并载入这些文件
  pdf_compress <输入PDF> <目标大小MB> [选项]
  pdf_compress <输入PDF> --dpi 150 [选项]

选项:
  -o, --output-dir <目录>   输出目录（默认与输入文件同目录）
  --mode {auto,manual}      auto=自动搜索（默认），manual=手动逐步调整
  --strategy {closest,smaller}
                            closest=最接近目标（误差≤5%，可大于或小于，默认）
                            smaller=必须小于目标（误差≤10%）
  --max-retries <次数>      自动模式最大渲染次数（默认 8，正常 3 次内命中）
  --dpi <数值>              直接按指定分辨率渲染一次（可省略目标大小）
  --max-dpi <数值>          自动搜索的分辨率上限（默认 3000）
  --min-dpi <数值>          自动搜索的分辨率下限（默认 7.2）
  -j, --jobs <数值>         渲染进程数（默认=物理核心数，0 表示自动）
  --force-render            无损结果已小于目标时，也强制有损渲染把文件做大
  --keep-page-box           保留原始页面尺寸（MediaBox），不因缩放改变纸张大小
  --dry-run                 只显示计划，不做实际压缩
  --gui                     强制打开图形界面

界面语言:
  --lang {zh,en}            只对本次运行指定语言（不写入设置）
  -h, --help                显示帮助，语言跟随当前设置
  --help-cn                 显示中文帮助（--help-zh 同义）
  --help-en                 显示英文帮助

资源管理器右键菜单（仅 Windows）:
  --install-menu            添加右键菜单（需要管理员权限，只对 .pdf 生效）
  --uninstall-menu          移除右键菜单

  -V, --version             显示版本号

说明:
  - 有损渲染会丢弃文本与矢量信息，输出是图片集合，最适合扫描件/图片型 PDF。
  - scale=分辨率/72。渲染只改变图像像素数（DPI），不再改变页面物理尺寸。
  - 分辨率越高、JPEG 质量越高，文件越大。搜索会用实测数据推算所需参数，
    通常 2~3 次即可命中目标；若目标本身不可达会直接告知可做到的范围。
  - 单页渲染像素上限 40 Mpx（约 120 MB 内存/进程），超出会自动降低该页分辨率。
  - 按 Ctrl+C 可中断：会保留已生成的最佳结果，并在结束时清理全部临时文件。
)HELP";
}

const char* help_en() {
    return R"HELP(
PDF Compressor - squeeze a PDF down to a target size, or re-render it at a fixed DPI

Usage:
  pdf_compress                       Open the graphical interface (the default
                                     behaviour when there are no arguments)
  pdf_compress <file.pdf> ...        Open the graphical interface with these files
  pdf_compress <input.pdf> <target MB> [options]
  pdf_compress <input.pdf> --dpi 150 [options]

Options:
  -o, --output-dir <dir>    Output directory (default: next to the input file)
  --mode {auto,manual}      auto = search automatically (default),
                            manual = adjust step by step
  --strategy {closest,smaller}
                            closest: within 5% of the target (default)
                            smaller: below the target, within 10%
  --max-retries <n>         Maximum render attempts in auto mode (default: 8;
                            it normally hits the target within 3)
  --dpi <value>             Render once at this fixed resolution (the target size
                            may then be omitted)
  --max-dpi <value>         Upper resolution bound for the search (default: 3000)
  --min-dpi <value>         Lower resolution bound for the search (default: 7.2)
  -j, --jobs <n>            Worker processes (default: physical core count;
                            0 = auto)
  --force-render            Render even when the lossless result is already
                            smaller than the target (makes the file bigger, NOT
                            better)
  --keep-page-box           Keep the original page size (MediaBox) instead of the
                            crop box
  --dry-run                 Show the plan only
  --gui                     Force the graphical interface

Interface language:
  --lang {zh,en}            Language for this run only (not saved to settings)
  -h, --help                Show help, using the currently configured language
  --help-cn                 Show Chinese help (--help-zh is the same thing)
  --help-en                 Show English help

File Explorer context menu (Windows only):
  --install-menu            Add the context menu entry (needs administrator
                            rights; only applies to .pdf files)
  --uninstall-menu          Remove the context menu entry

  -V, --version             Show the version

Notes:
  - Lossy rendering throws away text and vector data: the output is a set of
    images. It suits scanned / image-only PDFs best.
  - scale = DPI / 72. Rendering only changes the pixel count (DPI); the physical
    page size is left alone.
  - Higher DPI and higher JPEG quality mean a bigger file. The search predicts
    the parameters it needs from measurements, so it normally hits the target
    within 2-3 renders, and it tells you the achievable range when the target
    cannot be reached at all.
  - A single page is limited to 40 Mpx of rendering (about 120 MB of memory per
    process); pages above that get a lower resolution automatically.
  - Ctrl+C interrupts: the best result so far is kept and every temporary file
    is cleaned up on the way out.
)HELP";
}

// ── 参数解析 ──────────────────────────────────────────────────

namespace {

/// 取下一个值：既支持 `--opt value` 也支持 `--opt=value`。
bool take_value(const std::vector<std::string>& args, std::size_t& index,
                const std::string& inline_value, std::string& out) {
    if (!inline_value.empty()) {
        out = inline_value;
        return true;
    }
    if (index + 1 >= args.size()) return false;
    out = args[++index];
    return true;
}

bool parse_number(const std::string& text, double& out) {
    try {
        std::size_t consumed = 0;
        out = std::stod(text, &consumed);
        return consumed == text.size();
    } catch (...) {
        return false;
    }
}

bool parse_integer(const std::string& text, int& out) {
    try {
        std::size_t consumed = 0;
        out = std::stoi(text, &consumed);
        return consumed == text.size();
    } catch (...) {
        return false;
    }
}

std::string bad_value(const std::string& option, const std::string& value) {
    return tr("参数 {option} 的取值无效：{value}", {{"option", option},
                                                    {"value", value}});
}

std::string missing_value(const std::string& option) {
    return tr("参数 {option} 后面需要一个值", {{"option", option}});
}

}  // namespace

ParsedArgs parse_args(int argc, char** argv) {
    ParsedArgs parsed;
    std::vector<std::string> positionals;

    // 跳过 argv[0]（程序名）
    std::vector<std::string> args;
    for (int index = 1; index < argc; ++index) args.emplace_back(argv[index]);

    for (std::size_t index = 0; index < args.size(); ++index) {
        std::string token = args[index];
        std::string inline_value;
        const std::size_t equals = token.find('=');
        if (token.size() > 2 && token[0] == '-' && equals != std::string::npos) {
            inline_value = token.substr(equals + 1);
            token = token.substr(0, equals);
        }

        std::string value;
        if (token == "-h" || token == "--help") {
            parsed.show_help = true;
        } else if (token == "--help-cn" || token == "--help-zh") {
            parsed.show_help = true;
            parsed.help_force_zh = true;
        } else if (token == "--help-en") {
            parsed.show_help = true;
            parsed.help_force_en = true;
        } else if (token == "-V" || token == "--version") {
            parsed.show_version = true;
        } else if (token == "--install-menu") {
            parsed.install_menu = true;
        } else if (token == "--uninstall-menu") {
            parsed.uninstall_menu = true;
        } else if (token == "--quiet") {
            parsed.quiet = true;
        } else if (token == "--gui") {
            parsed.gui = true;
        } else if (token == "--force-render") {
            parsed.options.force_render = true;
        } else if (token == "--keep-page-box") {
            parsed.options.keep_page_box = true;
        } else if (token == "--dry-run") {
            parsed.options.dry_run = true;
        } else if (token == "--lang") {
            if (!take_value(args, index, inline_value, value)) {
                parsed.error = missing_value(token);
                return parsed;
            }
            Language language;
            if (!parse_language(value, language)) {
                parsed.error = bad_value(token, value);
                return parsed;
            }
            parsed.has_language_flag = true;
            parsed.language_flag = language;
        } else if (token == "-o" || token == "--output-dir") {
            if (!take_value(args, index, inline_value, value)) {
                parsed.error = missing_value(token);
                return parsed;
            }
            parsed.options.output_dir = value;
        } else if (token == "--mode") {
            if (!take_value(args, index, inline_value, value)) {
                parsed.error = missing_value(token);
                return parsed;
            }
            if (value == "auto") parsed.options.mode = Mode::Auto;
            else if (value == "manual") parsed.options.mode = Mode::Manual;
            else {
                parsed.error = bad_value(token, value);
                return parsed;
            }
        } else if (token == "--strategy") {
            if (!take_value(args, index, inline_value, value)) {
                parsed.error = missing_value(token);
                return parsed;
            }
            if (value == "closest") parsed.options.strategy = Strategy::Closest;
            else if (value == "smaller") parsed.options.strategy = Strategy::Smaller;
            else {
                parsed.error = bad_value(token, value);
                return parsed;
            }
        } else if (token == "--max-retries") {
            if (!take_value(args, index, inline_value, value)) {
                parsed.error = missing_value(token);
                return parsed;
            }
            int number = 0;
            if (!parse_integer(value, number)) {
                parsed.error = bad_value(token, value);
                return parsed;
            }
            parsed.options.max_retries = number;
        } else if (token == "--dpi") {
            if (!take_value(args, index, inline_value, value)) {
                parsed.error = missing_value(token);
                return parsed;
            }
            double number = 0;
            if (!parse_number(value, number)) {
                parsed.error = bad_value(token, value);
                return parsed;
            }
            parsed.options.dpi = number;
        } else if (token == "--max-dpi") {
            if (!take_value(args, index, inline_value, value)) {
                parsed.error = missing_value(token);
                return parsed;
            }
            double number = 0;
            if (!parse_number(value, number)) {
                parsed.error = bad_value(token, value);
                return parsed;
            }
            parsed.options.max_dpi = number;
        } else if (token == "--min-dpi") {
            if (!take_value(args, index, inline_value, value)) {
                parsed.error = missing_value(token);
                return parsed;
            }
            double number = 0;
            if (!parse_number(value, number)) {
                parsed.error = bad_value(token, value);
                return parsed;
            }
            parsed.options.min_dpi = number;
        } else if (token == "-j" || token == "--jobs") {
            if (!take_value(args, index, inline_value, value)) {
                parsed.error = missing_value(token);
                return parsed;
            }
            int number = 0;
            if (!parse_integer(value, number)) {
                parsed.error = bad_value(token, value);
                return parsed;
            }
            parsed.options.jobs = number;
        } else if (!token.empty() && token[0] == '-') {
            parsed.error = tr("无法识别的参数：{value}", {{"value", token}});
            return parsed;
        } else {
            positionals.push_back(token);
        }
    }

    parsed.files = positionals;
    if (!positionals.empty()) {
        parsed.options.input = positionals[0];
        if (positionals.size() >= 2) {
            double number = 0;
            if (!parse_number(positionals[1], number)) {
                parsed.error = tr("目标大小需要是一个数字，收到“{value}”",
                                  {{"value", positionals[1]}});
                return parsed;
            }
            parsed.options.target_size = number;
        }
    }
    return parsed;
}

// ── 启动方式分派 ──────────────────────────────────────────────

namespace {

/// 参数全是存在的文件吗（用来判断"打开界面并载入这些文件"）。
bool all_existing_files(const std::vector<std::string>& files) {
    if (files.empty()) return false;
    for (const std::string& path : files) {
        if (!is_regular_file(path)) return false;
    }
    return true;
}

void print_help(bool force_zh, bool force_en) {
    if (force_zh) {
        set_language(Language::Zh);
    } else if (force_en) {
        set_language(Language::En);
    }
    emit(language() == Language::En ? help_en() : help_zh());
}

}  // namespace

int run(int argc, char** argv) {
    std::vector<std::string> raw;
    for (int index = 1; index < argc; ++index) raw.emplace_back(argv[index]);

    // 语言先定：帮助文本和后面所有输出都依赖它
    Language forced = Language::Zh;
    bool has_forced = false;
    for (std::size_t index = 0; index < raw.size(); ++index) {
        const std::string& token = raw[index];
        if (token == "--lang" && index + 1 < raw.size()) {
            has_forced = parse_language(raw[index + 1], forced);
        } else if (starts_with(token, "--lang=")) {
            has_forced = parse_language(token.substr(7), forced);
        }
    }
    set_language(has_forced ? forced : detected_language());

    // 子进程入口：不打印、不提问，渲染完就走
    if (raw.size() >= 2 && raw[0] == "--worker") {
        return run_worker(raw[1]);
    }

    ParsedArgs parsed = parse_args(argc, argv);

    if (parsed.show_help) {
        print_help(parsed.help_force_zh, parsed.help_force_en);
        return kExitOk;
    }
    if (parsed.show_version) {
        emit(std::string(kAppName) + " " + kVersion);
        return kExitOk;
    }
    if (!parsed.error.empty()) {
        emit(tr("[失败] 参数错误：{message}", {{"message", parsed.error}}));
        emit(tr("用 --help 查看用法。"));
        return 2;
    }
    if (parsed.install_menu) return install_shell_menu(parsed.quiet);
    if (parsed.uninstall_menu) return uninstall_shell_menu(parsed.quiet);

    // 图形界面：不带参数，或者参数全是存在的文件（例如右键菜单启动、
    // 把 PDF 拖到 exe 上），或者显式 --gui。只要出现任何选项就说明用户
    // 要的是命令行压缩 —— `pdf_compress a.pdf 5` 绝不能弹界面。
    bool only_paths = !raw.empty();
    for (const std::string& token : raw) {
        if (!token.empty() && token[0] == '-') {
            only_paths = false;
            break;
        }
    }
    const bool wants_gui =
        parsed.gui || raw.empty() || (only_paths && all_existing_files(parsed.files));
    if (wants_gui) {
        if (!gui_available()) {
            emit(tr("[失败] 这个构建没有图形界面，请使用命令行："
                    "pdf_compress <输入PDF> <目标大小MB>"));
            return kExitFail;
        }
        return run_gui(parsed.files);
    }

    if (parsed.options.input.empty()) {
        emit(tr("[失败] 需要给出目标大小(MB)，或使用 --dpi 指定固定分辨率"));
        emit(tr("用 --help 查看用法。"));
        return 2;
    }

    const std::string problem = validate_options(parsed.options);
    if (!problem.empty()) {
        emit(tr("[失败] {error}", {{"error", problem}}));
        return kExitFail;
    }

    UiCallbacks ui = console_ui();
    return run_job(parsed.options, ui);
}

}  // namespace pdfc
