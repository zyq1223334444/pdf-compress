// -*- coding: utf-8 -*-
//
// MuPDF 封装层：打开文档、无损优化、逐页渲染成 JPEG、拼装成新的 PDF。
//
// 这里只做"一页一页的脏活"，不理解目标大小，也不理解重试策略——搜索逻辑在
// search.cpp，调度在 engine.cpp。所有函数失败时都通过 error 出参返回一句
// 已经翻译好的说明，不抛异常（渲染是在子进程里跑的，异常跨进程没意义）。
//
// 关于 MuPDF 的版本差异：图像写出与 JPEG 编码这几个 API 在不同版本签名有过
// 变动，pdf.cpp 里用 FZ_VERSION 做了兼容分支，改 MuPDF 版本时先看那几处。

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pdfc {

/// 一页要按什么比例渲染（scale = DPI / 72）。
struct RenderItem {
    int page = 0;          // 0 基页号
    double scale = 1.0;
};

/// 把某个 scale 应用到整份文档后的结果：每页的计划，以及被像素护栏压制的页。
struct PagePlan {
    std::vector<RenderItem> items;
    std::vector<std::pair<int, double>> clipped;   // (1 基页号, 实际 scale)
};

/// 打开文档只为了拿页数（顺便验证它能不能打开、有没有加密）。
bool probe_document(const std::string& input, int& page_count, std::string& error);

/// 计算每页的实际渲染比例，应用单页像素上限。
///
/// 超大页面（例如整页拼接的工程图）会让一个进程吃掉几 GB 内存，所以超过
/// max_pixels 的页面自动降分辨率——降的是这一页，其它页照旧。
bool plan_pages(const std::string& input, double scale, std::int64_t max_pixels,
                PagePlan& plan, std::string& error);

/// 无损优化：garbage collect + deflate + clean，不改画质。结果放在内存里，
/// 免得为了"看一眼大小"就写一份完整副本。
bool optimize_lossless(const std::string& input, std::vector<unsigned char>& output,
                       std::string& error);

/// 渲染若干页并写成一份临时 PDF（子进程与单进程路径共用）。
///
/// on_page 每完成一页回调一次，用来报进度；可以是空函数。
bool render_pages(const std::string& input, const std::vector<RenderItem>& items,
                  int jpeg_quality, bool keep_page_box, const std::string& output,
                  std::string& error, const std::function<void()>& on_page = {});

/// 把若干份临时 PDF 按顺序拼成一份。
bool merge_pdfs(const std::vector<std::string>& parts, const std::string& output,
                std::string& error);

/// 覆盖写文件；返回是否成功。
bool write_file(const std::string& path, const unsigned char* data, std::size_t size);

/// 复制文件（无损结果就是原文件时用它）。
bool copy_file(const std::string& from, const std::string& to);

/// 删除文件（失败不报错，临时文件清理用）。
void remove_file(const std::string& path);

/// 建一个临时目录，返回路径；失败返回空串。
///
/// error 不为空时会被写成一句能贴进日志的说明（系统错误码 + 试过的路径）。
/// 临时目录建不出来时，"哪个目录建不出来、为什么"是唯一有用的问题，光说
/// "无法创建输出目录"会把排查方向指到完全错误的地方。
std::string make_temp_dir(const std::string& parent, const std::string& prefix,
                          std::string* error = nullptr);

/// 递归删除目录（失败不报错）。
void remove_tree(const std::string& path);

}  // namespace pdfc
