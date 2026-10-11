// -*- coding: utf-8 -*-
//
// 渲染子进程的实现：读规格文件、渲染一块页面、写出临时 PDF。
//
// 规格文件格式（UTF-8、LF、每行一个 key=value）：
//     input=<输入 PDF 全路径>
//     output=<临时 PDF 全路径>
//     quality=<JPEG 质量>
//     keep_page_box=<0|1>
//     pages=<页号>:<scale>,<页号>:<scale>,...
//
// 分隔符特意不用空格：路径里可能有空格和中文，用空格做分隔符迟早会出事。
// 冒号与逗号只出现在 pages 里，而路径不会出现在 pages 里，所以也不冲突。

#include "worker.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "common.hpp"
#include "i18n.hpp"
#include "search.hpp"

namespace pdfc {
namespace {

/// Windows 上 ifstream/ofstream 的窄字符路径按本地代码页解释，中文路径会错，
/// 所以统一走 filesystem::path（由 UTF-8 转宽字符构造）。
std::filesystem::path to_path(const std::string& text) {
#if defined(_WIN32)
    return std::filesystem::path(utf8_to_wide(text));
#else
    return std::filesystem::path(text);
#endif
}

bool read_text_file(const std::string& path, std::string& text) {
    std::ifstream stream(to_path(path), std::ios::binary);
    if (!stream) return false;
    text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return true;
}

bool write_text_file(const std::string& path, const std::string& text) {
    std::ofstream stream(to_path(path), std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    return !stream.fail();
}

/// 值里出现换行会破坏"一行一个键"的结构；换行也不可能是合法的文件路径，
/// 所以直接拒掉，免得写出一份自己都读不回来的规格。
bool has_line_break(const std::string& text) {
    return text.find('\n') != std::string::npos || text.find('\r') != std::string::npos;
}

bool parse_pages(const std::string& text, std::vector<RenderItem>& items,
                 std::string& error) {
    items.clear();
    if (text.empty()) return true;      // 空块合法（例如全被取消），渲染出空 PDF
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::string entry =
            comma == std::string::npos ? text.substr(start) : text.substr(start, comma - start);
        if (!entry.empty()) {
            const std::size_t colon = entry.find(':');
            if (colon == std::string::npos) {
                error = tr("页表格式错误");
                return false;
            }
            const std::string page_text = entry.substr(0, colon);
            const std::string scale_text = entry.substr(colon + 1);
            char* end = nullptr;
            const long page = std::strtol(page_text.c_str(), &end, 10);
            if (end == nullptr || *end != '\0' || page < 0) {
                error = tr("页号不是合法数字");
                return false;
            }
            end = nullptr;
            const double scale = std::strtod(scale_text.c_str(), &end);
            if (end == nullptr || *end != '\0' || !(scale > 0.0)) {
                error = tr("页面比例不是合法数字");
                return false;
            }
            RenderItem item;
            item.page = static_cast<int>(page);
            item.scale = scale;
            items.push_back(item);
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return true;
}

}  // namespace

std::string chunk_error_path(const std::string& output) { return output + ".err"; }

bool write_chunk_spec(const std::string& path, const ChunkSpec& spec, std::string& error) {
    if (has_line_break(spec.input) || has_line_break(spec.output)) {
        error = tr("路径里不能有换行");
        return false;
    }
    std::string text;
    text += "input=" + spec.input + "\n";
    text += "output=" + spec.output + "\n";
    text += "quality=" + std::to_string(spec.jpeg_quality) + "\n";
    text += std::string("keep_page_box=") + (spec.keep_page_box ? "1" : "0") + "\n";
    text += "pages=";
    for (std::size_t index = 0; index < spec.items.size(); ++index) {
        if (index != 0) text.push_back(',');
        text += std::to_string(spec.items[index].page);
        text.push_back(':');
        // 六位小数足够表达任何 DPI/72，也让两边（父进程写、子进程读）完全对得上
        text += fmt_fixed(spec.items[index].scale, 6);
    }
    text.push_back('\n');
    if (!write_text_file(path, text)) {
        error = tr("无法写出渲染规格文件：{path}", {{"path", path}});
        return false;
    }
    return true;
}

bool read_chunk_spec(const std::string& path, ChunkSpec& spec, std::string& error) {
    std::string text;
    if (!read_text_file(path, text)) {
        error = tr("无法读取渲染规格文件：{path}", {{"path", path}});
        return false;
    }
    // 顺手吃掉可能的 UTF-8 BOM（被编辑器动过也不会读出一堆乱码键）
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }

    spec = ChunkSpec{};
    bool has_input = false;
    bool has_output = false;
    bool has_pages = false;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        std::string line =
            newline == std::string::npos ? text.substr(start) : text.substr(start, newline - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            const std::size_t equals = line.find('=');
            if (equals == std::string::npos) {
                error = tr("渲染规格文件格式错误：{path}", {{"path", path}});
                return false;
            }
            const std::string key = line.substr(0, equals);
            const std::string value = line.substr(equals + 1);
            if (key == "input") {
                spec.input = value;
                has_input = true;
            } else if (key == "output") {
                spec.output = value;
                has_output = true;
            } else if (key == "quality") {
                spec.jpeg_quality = std::atoi(value.c_str());
            } else if (key == "keep_page_box") {
                spec.keep_page_box = value == "1";
            } else if (key == "pages") {
                if (!parse_pages(value, spec.items, error)) return false;
                has_pages = true;
            }
            // 认不出来的键直接忽略：将来加字段时旧版本还能跑
        }
        if (newline == std::string::npos) break;
        start = newline + 1;
    }

    if (!has_input || !has_output || !has_pages) {
        error = tr("渲染规格文件缺少必需的字段：{path}", {{"path", path}});
        return false;
    }
    if (spec.jpeg_quality < 1 || spec.jpeg_quality > 100) {
        // 规格文件被改坏时用一个安全的默认值，而不是让 libjpeg 去猜
        spec.jpeg_quality = kQualityDefault;
    }
    return true;
}

int run_worker(const std::string& spec_path) {
    ChunkSpec spec;
    std::string error;
    if (!read_chunk_spec(spec_path, spec, error)) {
        // 连规格都读不进来（output 未知），只能把说明写在规格文件旁边，
        // 让父进程至少能给出一点线索。
        const std::string fallback = spec_path + ".err";
        write_file(fallback, reinterpret_cast<const unsigned char*>(error.data()),
                   error.size());
        return kExitFail;
    }

    // 上一轮留下的说明文件必须先删掉：父进程只在退出码非 0 时才读它，
    // 留着会让"读取失败原因"读到过期的内容。
    remove_file(chunk_error_path(spec.output));

    std::string problem;
    if (render_pages(spec.input, spec.items, spec.jpeg_quality, spec.keep_page_box,
                     spec.output, problem, {})) {
        return kExitOk;
    }

    // 失败：把说明写成 <output>.err（UTF-8）。标准输出保持绝对干净，
    // 父进程要用同一个终端报进度。
    if (problem.empty()) problem = tr("渲染失败");
    write_file(chunk_error_path(spec.output),
               reinterpret_cast<const unsigned char*>(problem.data()), problem.size());
    return kExitFail;
}

}  // namespace pdfc
