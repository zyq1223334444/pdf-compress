// -*- coding: utf-8 -*-
//
// 渲染子进程。
//
// 父进程把一块页面（页号 + 各自的比例）写成规格文件，再以
//      pdf_compress --worker <规格文件>
// 启动自己；子进程渲染完就把结果写成一份临时 PDF 后退出。
//
// 子进程不打印任何东西到标准输出（父进程要读自己的进度），出错靠退出码和
// 一个伴生的 .err 文件传递说明。

#pragma once

#include <string>
#include <vector>

#include "pdf.hpp"

namespace pdfc {

/// 一块页面的规格文件内容。
struct ChunkSpec {
    std::string input;
    std::string output;
    int jpeg_quality = 85;
    bool keep_page_box = false;
    std::vector<RenderItem> items;
};

/// 规格文件里出错说明的伴生文件：<output>.err
std::string chunk_error_path(const std::string& output);

bool write_chunk_spec(const std::string& path, const ChunkSpec& spec, std::string& error);
bool read_chunk_spec(const std::string& path, ChunkSpec& spec, std::string& error);

/// 子进程入口。返回进程退出码（0 成功，1 失败）。
int run_worker(const std::string& spec_path);

}  // namespace pdfc
