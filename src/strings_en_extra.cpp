// -*- coding: utf-8 -*-
//
// C++ 版独有的英文文案。
//
// strings_en.cpp 是从 Python 版的 lang_en.py 生成的，那份表有它自己的完整性测试
// （"表里不能有 Python 代码用不到的词条"），所以 C++ 侧多出来的文案不能硬塞进去，
// 否则 Python 的测试会失败。这里单独放一小张手写的补充表，i18n.cpp 会把两张表
// 合并起来查。
//
// 什么该放这里：C++ 实现有、Python 实现没有的文案（例如手写参数解析器的报错，
// Python 那边是 argparse 生成的）。凡是两边都有的文案，一律走生成的那张表。

#include "i18n.hpp"

namespace pdfc {
namespace {

const std::pair<const char*, const char*> kExtraTable[] = {
    // 参数校验（Python 版由 argparse 报错，文案不同）
    {"参数 {option} 的取值无效：{value}", "invalid value for {option}: {value}"},
    {"参数 {option} 后面需要一个值", "{option} needs a value"},
    {"无法识别的参数：{value}", "unrecognised argument: {value}"},
    {"目标大小需要是一个数字，收到“{value}”",
     "the target size must be a number, got \"{value}\""},

    // 选项校验（Python 版里这几条带 "[失败] " 前缀，C++ 版由调用方加前缀）
    {"--dpi 必须大于 0", "--dpi must be greater than 0"},
    {"--max-retries 必须大于 0", "--max-retries must be greater than 0"},
    {"--min-dpi({min:g}) 必须小于 --max-dpi({max:g})",
     "--min-dpi({min:g}) must be smaller than --max-dpi({max:g})"},
    {"需要给出目标大小(MB)，或使用 --dpi 指定固定分辨率",
     "a target size in MB is required, or use --dpi for a fixed resolution"},

    // 没有图形界面的构建
    {"[失败] 这个构建没有图形界面，请使用命令行：pdf_compress <输入PDF> <目标大小MB>",
     "[failed] this build has no graphical interface; use the command line: "
     "pdf_compress <input.pdf> <target MB>"},

    // 图形界面画不出来（Direct2D 失败）
    {"[失败] 图形界面无法绘制（Direct2D 报告 {detail}）。"
     "这台机器上的显示驱动可能不支持硬件加速；请改用命令行版本。",
     "[failed] the graphical interface cannot draw (Direct2D reported {detail}). The display "
     "driver on this machine may not support hardware acceleration; please use the "
     "command-line version instead."},

    // ── 错误路径 ────────────────────────────────────────────────
    // 这些原本是直接写死的中文字面量。它们是"只在出问题时才出现"的文案，人测
    // 不到，英文用户看到的就会是半句英文加半句中文；tools/check_i18n.py 的第 4
    // 条检查专门盯这一类。
    {"无法写出文件：{path}", "cannot write the file: {path}"},
    {"无法写出临时文件：{path}", "cannot write the temporary file: {path}"},
    {"渲染结果为空文件", "the rendered result is an empty file"},
    {"没有可渲染的页面", "there are no pages to render"},
    {"渲染失败", "rendering failed"},
    {"[失败] 无法在输出目录里创建临时目录：{error}",
     "[failed] cannot create the temporary working directory inside the output directory: "
     "{error}"},
    {"渲染子进程异常退出（退出码 {code}）",
     "a render subprocess exited abnormally (exit code {code})"},

    // 渲染子进程读规格文件时的报错。这些文字由子进程写进 <输出>.pdf.err，
    // 父进程再读出来显示，所以必须跟着 --lang 走（engine.cpp 会把语言传给子进程）。
    {"页表格式错误", "malformed page list"},
    {"页号不是合法数字", "the page number is not a valid integer"},
    {"页面比例不是合法数字", "the page scale is not a valid number"},
    {"路径里不能有换行", "a path cannot contain a line break"},
    {"无法写出渲染规格文件：{path}", "cannot write the render spec file: {path}"},
    {"无法读取渲染规格文件：{path}", "cannot read the render spec file: {path}"},
    {"渲染规格文件格式错误：{path}", "malformed render spec file: {path}"},
    {"渲染规格文件缺少必需的字段：{path}",
     "the render spec file is missing required fields: {path}"},

    // 文件列表为空时的指引。空列表本来是一整块白底，看不出界面到底画完没有，
    // 也看不出"文件可以直接拖进来"这个没有对应按钮的操作。
    {"把 PDF 文件拖到这里，或点上面的「添加 PDF 文件」",
     "Drop PDF files here, or click \"Add PDF files\" above"},

    // settings.ini 的文件头注释。用户会直接打开这个文件，中文注释同样是漏翻。
    {"# PDF Compressor 的用户设置。\n"
     "# 由程序自动生成，删掉它就会回到出厂状态（语言按系统区域猜）。\n",
     "# PDF Compressor user settings.\n"
     "# Generated automatically. Delete this file to go back to the defaults;\n"
     "# the interface language then follows the system locale again.\n"},
};

}  // namespace

const std::vector<std::pair<const char*, const char*>>& english_extra_table() {
    static const std::vector<std::pair<const char*, const char*>> table(
        kExtraTable, kExtraTable + sizeof(kExtraTable) / sizeof(kExtraTable[0]));
    return table;
}

}  // namespace pdfc
