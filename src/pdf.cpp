// -*- coding: utf-8 -*-
//
// MuPDF 封装层的实现。
//
// 三条纪律：
//   1. 每个函数自己开一份 fz_context 并在返回前 drop 掉——MuPDF 的上下文
//      不是线程安全的，而渲染本来就跑在独立子进程里，一个进程一份最省心；
//   2. 所有可能抛异常的 MuPDF 调用都包在严格成对的 fz_try / fz_always /
//      fz_catch 里。fz_always 负责释放，fz_catch 只负责把说明抄进缓冲区，
//      因为 fz_caught_message 只在 catch 块内有效；
//   3. 记进 fz_try 的指针都先声明、初始化为空，并用 fz_var 声明，避免
//      setjmp/longjmp 之后局部变量的值失效（MuPDF 官方推荐的写法）。

#include "pdf.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

// MuPDF 自己的头文件在 /W4 下会吐几个警告（inline 函数里未引用的 ctx 参数、
// 用 setjmp 做异常的 stream.h），这些不是本项目的代码，压掉以免把真正的警告
// 淹掉。pragma 只包住 include，出了这个块 /W4 依旧生效。
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4100)   // 'ctx': 未引用的形参（MuPDF 的 inline 函数）
#  pragma warning(disable : 4611)   // setjmp 与 C++ 对象析构的交互（MuPDF 的异常机制）
#endif
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#if defined(_MSC_VER)
// C4611（"setjmp 与 C++ 对象析构的交互不可移植"）在任何调用 MuPDF 的 C++ 文件里
// 都无法避免：fz_try 就是 setjmp/longjmp。本文件遵守一条约定——fz_try 块里只放
// 裸指针和 POD，所有带析构函数的对象（std::string / std::function）都在 fz_try
// 之前声明，或者只作为引用参数传进来，由 fz_always / fz_catch 之后的正常控制流
// 负责销毁。所以这条警告在这里是误报；改这个文件时请继续保持这个约定。
#  pragma warning(disable : 4611)
#endif

#include "common.hpp"
#include "i18n.hpp"

// ── MuPDF 版本差异 ──────────────────────────────────────────────
// FZ_VERSION 是字符串（"1.26.0"），不能参与 #if；version.h 里另有数值宏
// FZ_VERSION_MAJOR / FZ_VERSION_MINOR，本文件只认它们。老版本没有这两个宏，
// 兜底成 0，于是所有版本判断都会退到"老 API"那一支。
// 改 MuPDF 版本时只需要看这一处和下面每个 #if 旁边的注释。
#ifndef FZ_VERSION_MAJOR
#  define FZ_VERSION_MAJOR 0
#endif
#ifndef FZ_VERSION_MINOR
#  define FZ_VERSION_MINOR 0
#endif
#define PDFC_MUPDF_AT_LEAST(major, minor)                     \
    ((FZ_VERSION_MAJOR > (major)) ||                          \
     (FZ_VERSION_MAJOR == (major) && FZ_VERSION_MINOR >= (minor)))

namespace pdfc {
namespace {

/// MuPDF 默认把警告和错误打到 stderr。子进程绝对不能写标准输出/错误（父进程
/// 要在同一个终端上自己报进度），父进程也不希望 PDF 内部的修补噪音混进来，
/// 所以两条通道都接到空回调上。
void swallow_message(void* user, const char* message) {
    (void)user;
    (void)message;
}

/// 建一份"安静"的上下文；失败返回 nullptr。
fz_context* new_context() {
    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (ctx == nullptr) return nullptr;
#if PDFC_MUPDF_AT_LEAST(1, 16)
    // 错误/警告回调是 1.16 加的（"User callbacks for warning and error messages"）
    fz_set_error_callback(ctx, swallow_message, nullptr);
    fz_set_warning_callback(ctx, swallow_message, nullptr);
#endif
    fz_register_document_handlers(ctx);
    return ctx;
}

/// pdf_default_write_options 的取法在版本之间变过：
///   1.24 及以前：void pdf_default_write_options(fz_context*, pdf_write_options*)
///   1.25 起：    FZ_DATA extern const pdf_write_options pdf_default_write_options;
/// 两种形态语义相同（改版时把函数体原样搬成了常量），这里收敛成一个函数。
///
/// 由于 PyMuPDF 的 doc.save() 也是"先取库的默认值再覆盖 garbage/deflate/clean"，
/// 只有拿到库的默认值，两版写出来的文件才会一样大。
pdf_write_options default_write_options(fz_context* ctx) {
#if PDFC_MUPDF_AT_LEAST(1, 24)
    (void)ctx;
    return pdf_default_write_options;
#else
    pdf_write_options opts;
    std::memset(&opts, 0, sizeof(opts));
    pdf_default_write_options(ctx, &opts);
    return opts;
#endif
}

/// 等价于 PyMuPDF 的 doc.save(..., garbage=4, deflate=True, clean=True)。
///
/// PyMuPDF 的 save() 内部就是这么映射的：
///     deflate=True  -> do_compress = do_compress_images = do_compress_fonts = 1
///     garbage=4     -> do_garbage = 4
///     clean=True    -> do_clean = do_sanitize = 1
/// 分块临时文件、合并结果、无损优化三处都用同一组参数，是为了和 Python 版
/// 的 process_chunk()/merge/optimize_bytes() 写出的字节数尽量对齐。
pdf_write_options tidy_write_options(fz_context* ctx) {
    pdf_write_options opts = default_write_options(ctx);
    opts.do_compress = 1;
    opts.do_compress_images = 1;
    opts.do_compress_fonts = 1;
    opts.do_garbage = 4;
    opts.do_clean = 1;
    opts.do_sanitize = 1;
    return opts;
}

/// fz_make_rect 是较新版本才提供的 inline 辅助函数；直接填结构体成员在所有
/// 版本上都成立，也省掉一处版本判断。fz_rect 从 1.0 起就是这四个 float。
fz_rect make_rect(float x0, float y0, float x1, float y1) {
    fz_rect rect;
    rect.x0 = x0;
    rect.y0 = y0;
    rect.x1 = x1;
    rect.y1 = y1;
    return rect;
}

/// Windows 上 std::filesystem::path 的窄字符构造是按"本地代码页"解释的，
/// 中文路径会直接错掉；所以统一先把 UTF-8 转成宽字符。POSIX 上路径本来就是
/// 字节串，原样使用。
std::filesystem::path to_path(const std::string& text) {
#if defined(_WIN32)
    return std::filesystem::path(utf8_to_wide(text));
#else
    return std::filesystem::path(text);
#endif
}

/// 打开失败的统一说明。error 为空时给一句最朴素的原因，保证文案表里只有
/// 这一条"打不开"的键。
std::string open_error(const char* detail) {
    return tr("无法打开文件（不是有效的 PDF，或文件已损坏）：{error}",
              {{"error", detail != nullptr && detail[0] != '\0' ? detail : "unknown"}});
}

/// 把当前异常的消息抄进缓冲区：fz_caught_message 只在 fz_catch 块内有效。
void capture_message(fz_context* ctx, char* buffer, std::size_t size) {
    std::snprintf(buffer, size, "%s", fz_caught_message(ctx));
}

/// 一页在输出 PDF 里的落位：页面 MediaBox 与图像要铺满的矩形（同一坐标系）。
struct PageGeometry {
    fz_rect mediabox{0, 0, 0, 0};
    fz_rect image_rect{0, 0, 0, 0};
};

/// 算出把这一页画进新 PDF 时的页面尺寸与图像位置。
///
/// 对应 Python 的 _render_into()：scale 只决定图像的像素数（DPI = 72 × scale），
/// 页面的物理尺寸保持原样；keep_page_box 且页面没有旋转时，用原始 MediaBox
/// 建页，再把裁切后的内容按原来的偏移画回去（否则裁切信息就丢了）。
bool page_geometry(fz_context* ctx, fz_page* page, pdf_document* pdf, int page_number,
                   bool keep_page_box, PageGeometry& geometry) {
    const fz_rect bounds = fz_bound_page(ctx, page);
    const float width = bounds.x1 - bounds.x0;
    const float height = bounds.y1 - bounds.y0;

    bool use_media_box = false;
    int rotate = 0;
    fz_rect media{0, 0, 0, 0};
    fz_rect crop{0, 0, 0, 0};
    if (keep_page_box) {
        pdf_obj* page_obj = pdf_lookup_page_obj(ctx, pdf, page_number);
        if (page_obj != nullptr) {
            media = pdf_to_rect(ctx, pdf_dict_get_inheritable(ctx, page_obj, PDF_NAME(MediaBox)));
            crop = media;
            pdf_obj* crop_obj = pdf_dict_get_inheritable(ctx, page_obj, PDF_NAME(CropBox));
            if (pdf_is_array(ctx, crop_obj)) crop = pdf_to_rect(ctx, crop_obj);
            rotate = pdf_dict_get_inheritable_int(ctx, page_obj, PDF_NAME(Rotate)) % 360;
            if (rotate < 0) rotate += 360;
            // 旋转过的页面在 fitz 里是"转过来"的坐标，MediaBox 的偏移没有意义，
            // 这种情况和 Python 一样退回 page.rect 那条分支。
            use_media_box = rotate == 0;
        }
    }

    if (use_media_box) {
        const float x0 = crop.x0 - media.x0;
        const float y0 = crop.y0 - media.y0;
        geometry.mediabox = make_rect(0, 0, media.x1 - media.x0, media.y1 - media.y0);
        geometry.image_rect = make_rect(x0, y0, x0 + (crop.x1 - crop.x0),
                                        y0 + (crop.y1 - crop.y0));
    } else {
        geometry.mediabox = make_rect(0, 0, width, height);
        geometry.image_rect = make_rect(0, 0, width, height);
    }
    return geometry.mediabox.x1 > 0.0f && geometry.mediabox.y1 > 0.0f;
}

/// 用文档写出器把若干页渲染进内存里的 PDF 缓冲区，再用 pdf_save_document
/// 按 tidy 选项写到目标路径。
///
/// 为什么不直接让写出器写文件：写出器的选项串在不同版本里解释方式不一样
/// （有的按 mutool clean 的单字母，有的按 key=value），而我们要的是与
/// PyMuPDF 完全相同的 garbage/deflate/clean 组合。先落到内存、再自己用
/// pdf_save_document 写盘，就完全不依赖选项串的解析行为了，代价只是一次
/// 内存里的解析。
bool write_rendered_pages(const std::string& input, const std::vector<RenderItem>& items,
                          int jpeg_quality, bool keep_page_box,
                          const std::string& output, std::string& error,
                          const std::function<void()>& on_page) {
    fz_context* ctx = new_context();
    if (ctx == nullptr) {
        error = open_error("out of memory");
        return false;
    }

    fz_document* doc = nullptr;
    fz_buffer* buffer = nullptr;
    fz_document_writer* writer = nullptr;
    fz_document* reopened = nullptr;
    fz_page* page = nullptr;
    fz_pixmap* pixmap = nullptr;
    fz_buffer* jpeg = nullptr;
    fz_image* image = nullptr;
    std::string reason;
    char caught[512] = {0};
    bool failed = false;

    fz_var(doc);
    fz_var(buffer);
    fz_var(writer);
    fz_var(reopened);
    fz_var(page);
    fz_var(pixmap);
    fz_var(jpeg);
    fz_var(image);
    fz_var(reason);
    fz_var(caught);

    fz_try(ctx) {
        doc = fz_open_document(ctx, input.c_str());
        pdf_document* pdf = pdf_specifics(ctx, doc);
        if (pdf == nullptr) {
            reason = tr("无法打开文件（不是有效的 PDF，或文件已损坏）：{error}",
                        {{"error", "not a PDF document"}});
            fz_throw(ctx, FZ_ERROR_GENERIC, "not a PDF");
        }

        buffer = fz_new_buffer(ctx, 4096);
        // 写出器接管 fz_output 的所有权（连创建失败时也由它释放），
        // 所以这里不能自己留着 out 再 drop 一次。
        writer = fz_new_pdf_writer_with_output(
            ctx, fz_new_output_with_buffer(ctx, buffer), nullptr);

        for (const RenderItem& item : items) {
            page = fz_load_page(ctx, doc, item.page);
            const fz_matrix ctm =
                fz_scale(static_cast<float>(item.scale), static_cast<float>(item.scale));
            pixmap = fz_new_pixmap_from_page(ctx, page, ctm, fz_device_rgb(ctx), 0);
            // fz_default_color_params + invert_cmyk=0 与 PyMuPDF 的
            // pix.tobytes("jpeg", quality) 走的是同一个编码入口，字节一致。
            jpeg = fz_new_buffer_from_pixmap_as_jpeg(ctx, pixmap, fz_default_color_params,
                                                     jpeg_quality, 0);
            // JPEG 会以 DCTDecode 原样嵌进 PDF（不重新编码），这正是
            // PyMuPDF 的 insert_image(stream=...) 的行为。
            image = fz_new_image_from_buffer(ctx, jpeg);

            PageGeometry geometry;
            if (!page_geometry(ctx, page, pdf, item.page, keep_page_box, geometry)) {
                reason = tr("无法打开文件（不是有效的 PDF，或文件已损坏）：{error}",
                            {{"error", "page has an empty box"}});
                fz_throw(ctx, FZ_ERROR_GENERIC, "empty page box");
            }

            // 单位图像 [0,1]² 先按目标宽高缩放，再平移到目标位置。
            // MuPDF 是行向量约定（p' = p·M），fz_concat(A, B) 表示"先 A 后 B"。
            const fz_matrix placement = fz_concat(
                fz_scale(geometry.image_rect.x1 - geometry.image_rect.x0,
                         geometry.image_rect.y1 - geometry.image_rect.y0),
                fz_translate(geometry.image_rect.x0, geometry.image_rect.y0));

            fz_device* device = fz_begin_page(ctx, writer, geometry.mediabox);
            fz_fill_image(ctx, device, image, placement, 1.0f, fz_default_color_params);
            fz_end_page(ctx, writer);

            fz_drop_image(ctx, image);
            image = nullptr;
            fz_drop_buffer(ctx, jpeg);
            jpeg = nullptr;
            fz_drop_pixmap(ctx, pixmap);
            pixmap = nullptr;
            fz_drop_page(ctx, page);
            page = nullptr;

            // 子进程不报单页进度，但单进程路径要靠它报进度，所以每页都回调
            if (on_page) on_page();
        }

        // 必须 close，否则文件不完整；之后写出器连同它持有的 fz_output 一起 drop
        fz_close_document_writer(ctx, writer);
        fz_drop_document_writer(ctx, writer);
        writer = nullptr;

        reopened = fz_open_document_with_buffer(ctx, "application/pdf", buffer);
        pdf_document* saved = pdf_specifics(ctx, reopened);
        if (saved == nullptr) {
            reason = tr("无法打开文件（不是有效的 PDF，或文件已损坏）：{error}",
                        {{"error", "cannot reparse the written PDF"}});
            fz_throw(ctx, FZ_ERROR_GENERIC, "cannot reparse");
        }
        pdf_write_options options = tidy_write_options(ctx);
        pdf_save_document(ctx, saved, output.c_str(), &options);
    }
    fz_always(ctx) {
        fz_drop_image(ctx, image);
        fz_drop_buffer(ctx, jpeg);
        fz_drop_pixmap(ctx, pixmap);
        fz_drop_page(ctx, page);
        fz_drop_document_writer(ctx, writer);
        fz_drop_document(ctx, reopened);
        fz_drop_buffer(ctx, buffer);
        fz_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        if (reason.empty()) {
            capture_message(ctx, caught, sizeof(caught));
            reason = caught;
        }
        failed = true;
    }
    fz_drop_context(ctx);

    if (failed) {
        error = reason.empty() ? open_error(nullptr) : reason;
        // 写出过程留下的半成品不能留着，否则下一轮会读到坏文件
        remove_file(output);
        return false;
    }
    if (file_size(output) <= 0) {
        error = open_error("empty output");
        return false;
    }
    return true;
}

}  // namespace

bool probe_document(const std::string& input, int& page_count, std::string& error) {
    page_count = 0;
    fz_context* ctx = new_context();
    if (ctx == nullptr) {
        error = open_error("out of memory");
        return false;
    }

    fz_document* doc = nullptr;
    int pages = 0;
    int needs_password = 0;
    int opened = 0;
    char caught[512] = {0};
    bool failed = false;

    fz_var(doc);
    fz_var(pages);
    fz_var(needs_password);
    fz_var(opened);
    fz_var(caught);

    fz_try(ctx) {
        doc = fz_open_document(ctx, input.c_str());
        opened = 1;
        if (fz_needs_password(ctx, doc)) {
            needs_password = 1;
        } else {
            pages = fz_count_pages(ctx, doc);
        }
    }
    fz_always(ctx) {
        fz_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        capture_message(ctx, caught, sizeof(caught));
        failed = true;
    }
    fz_drop_context(ctx);

    if (failed || !opened) {
        error = open_error(caught);
        return false;
    }
    if (needs_password) {
        error = tr("该 PDF 已加密，需要密码。请先用其他工具解除密码保护后再压缩。");
        return false;
    }
    if (pages <= 0) {
        error = tr("该 PDF 没有任何页面。");
        return false;
    }
    page_count = pages;
    return true;
}

bool plan_pages(const std::string& input, double scale, std::int64_t max_pixels,
                PagePlan& plan, std::string& error) {
    plan.items.clear();
    plan.clipped.clear();

    fz_context* ctx = new_context();
    if (ctx == nullptr) {
        error = open_error("out of memory");
        return false;
    }

    fz_document* doc = nullptr;
    fz_page* page = nullptr;
    char caught[512] = {0};
    bool failed = false;

    fz_var(doc);
    fz_var(page);
    fz_var(caught);

    fz_try(ctx) {
        doc = fz_open_document(ctx, input.c_str());
        const int count = fz_count_pages(ctx, doc);
        for (int index = 0; index < count; ++index) {
            page = fz_load_page(ctx, doc, index);
            const fz_rect rect = fz_bound_page(ctx, page);
            fz_drop_page(ctx, page);
            page = nullptr;

            const double width = static_cast<double>(rect.x1 - rect.x0);
            const double height = static_cast<double>(rect.y1 - rect.y0);
            double effective = scale;
            // 与 Python 的 _page_plan 一致：相乘顺序也一样，免得浮点结果差一位
            const double pixels = (width * scale) * (height * scale);
            if (max_pixels > 0 && pixels > static_cast<double>(max_pixels)) {
                // 超大页面会把一个进程的内存吃光，按像素上限把这一页的分辨率压下来
                effective = scale * std::sqrt(static_cast<double>(max_pixels) / pixels);
                plan.clipped.emplace_back(index + 1, effective);
            }
            plan.items.push_back(RenderItem{index, effective});
        }
    }
    fz_always(ctx) {
        fz_drop_page(ctx, page);
        fz_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        capture_message(ctx, caught, sizeof(caught));
        failed = true;
    }
    fz_drop_context(ctx);

    if (failed) {
        plan.items.clear();
        plan.clipped.clear();
        error = open_error(caught);
        return false;
    }
    return true;
}

bool optimize_lossless(const std::string& input, std::vector<unsigned char>& output,
                       std::string& error) {
    output.clear();

    fz_context* ctx = new_context();
    if (ctx == nullptr) {
        error = tr("无损优化失败：{error}", {{"error", "out of memory"}});
        return false;
    }

    fz_document* doc = nullptr;
    fz_buffer* buffer = nullptr;
    fz_output* out = nullptr;
    char caught[512] = {0};
    bool failed = false;

    fz_var(doc);
    fz_var(buffer);
    fz_var(out);
    fz_var(caught);

    fz_try(ctx) {
        doc = fz_open_document(ctx, input.c_str());
        pdf_document* pdf = pdf_specifics(ctx, doc);
        if (pdf == nullptr) fz_throw(ctx, FZ_ERROR_GENERIC, "not a PDF");
        buffer = fz_new_buffer(ctx, 4096);
        out = fz_new_output_with_buffer(ctx, buffer);
        pdf_write_options options = tidy_write_options(ctx);
        // 结果留在内存里：大多数时候只是"看一眼大小"，不值得写一份完整副本
        pdf_write_document(ctx, pdf, out, &options);
        fz_close_output(ctx, out);
    }
    fz_always(ctx) {
        fz_drop_output(ctx, out);
        fz_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        capture_message(ctx, caught, sizeof(caught));
        failed = true;
    }
    // 先把数据抄出来再 drop 缓冲区：顺序反了就读到已释放的内存
    if (!failed && buffer != nullptr) {
        unsigned char* data = nullptr;
        const std::size_t size = fz_buffer_storage(ctx, buffer, &data);
        if (data != nullptr && size > 0) output.assign(data, data + size);
    }
    fz_drop_buffer(ctx, buffer);
    fz_drop_context(ctx);

    if (failed || output.empty()) {
        output.clear();
        error = tr("无损优化失败：{error}",
                   {{"error", caught[0] != '\0' ? caught : "unknown"}});
        return false;
    }
    return true;
}

bool render_pages(const std::string& input, const std::vector<RenderItem>& items,
                  int jpeg_quality, bool keep_page_box, const std::string& output,
                  std::string& error, const std::function<void()>& on_page) {
    return write_rendered_pages(input, items, jpeg_quality, keep_page_box, output, error,
                                on_page);
}

bool merge_pdfs(const std::vector<std::string>& parts, const std::string& output,
                std::string& error) {
    if (parts.empty()) {
        error = tr("无法打开文件（不是有效的 PDF，或文件已损坏）：{error}",
                   {{"error", "nothing to merge"}});
        return false;
    }

    fz_context* ctx = new_context();
    if (ctx == nullptr) {
        error = open_error("out of memory");
        return false;
    }

    fz_buffer* buffer = nullptr;
    fz_document_writer* writer = nullptr;
    fz_document* reopened = nullptr;
    fz_document* part = nullptr;
    fz_page* page = nullptr;
    std::string reason;
    char caught[512] = {0};
    bool failed = false;

    fz_var(buffer);
    fz_var(writer);
    fz_var(reopened);
    fz_var(part);
    fz_var(page);
    fz_var(reason);
    fz_var(caught);

    fz_try(ctx) {
        buffer = fz_new_buffer(ctx, 4096);
        writer = fz_new_pdf_writer_with_output(
            ctx, fz_new_output_with_buffer(ctx, buffer), nullptr);

        // 逐份打开临时 PDF，把每一页重放进新文档。
        // 这里没有用 pdf_graft_*：1.24 起那套 API 已经从公开头文件里消失了
        // （include/mupdf/pdf 下没有 graft.h），而重放页面只依赖
        // load/bound/run 这几个十几年来没动过的函数。
        for (const std::string& part_path : parts) {
            part = fz_open_document(ctx, part_path.c_str());
            const int count = fz_count_pages(ctx, part);
            for (int index = 0; index < count; ++index) {
                page = fz_load_page(ctx, part, index);
                const fz_rect bounds = fz_bound_page(ctx, page);
                fz_device* device = fz_begin_page(ctx, writer, bounds);
                // 分块里的图像本来就是 DCTDecode，PDF 设备会把它原样带过去；
                // 如果哪天发现体积明显变大，就是这里被重新编码了。
                fz_run_page(ctx, page, device, fz_identity, nullptr);
                fz_end_page(ctx, writer);
                fz_drop_page(ctx, page);
                page = nullptr;
            }
            fz_drop_document(ctx, part);
            part = nullptr;
        }

        fz_close_document_writer(ctx, writer);
        fz_drop_document_writer(ctx, writer);
        writer = nullptr;

        reopened = fz_open_document_with_buffer(ctx, "application/pdf", buffer);
        pdf_document* saved = pdf_specifics(ctx, reopened);
        if (saved == nullptr) {
            reason = tr("无法打开文件（不是有效的 PDF，或文件已损坏）：{error}",
                        {{"error", "cannot reparse the merged PDF"}});
            fz_throw(ctx, FZ_ERROR_GENERIC, "cannot reparse");
        }
        pdf_write_options options = tidy_write_options(ctx);
        pdf_save_document(ctx, saved, output.c_str(), &options);
    }
    fz_always(ctx) {
        fz_drop_page(ctx, page);
        fz_drop_document(ctx, part);
        fz_drop_document_writer(ctx, writer);
        fz_drop_document(ctx, reopened);
        fz_drop_buffer(ctx, buffer);
    }
    fz_catch(ctx) {
        if (reason.empty()) {
            capture_message(ctx, caught, sizeof(caught));
            reason = caught;
        }
        failed = true;
    }
    fz_drop_context(ctx);

    if (failed) {
        error = reason.empty() ? open_error(nullptr) : reason;
        remove_file(output);
        return false;
    }
    if (file_size(output) <= 0) {
        error = open_error("empty output");
        return false;
    }
    return true;
}

bool write_file(const std::string& path, const unsigned char* data, std::size_t size) {
    // 二进制 + 截断：同一个路径可能留着上一轮的旧内容
    std::ofstream stream(to_path(path), std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    if (size > 0 && data != nullptr) {
        stream.write(reinterpret_cast<const char*>(data),
                     static_cast<std::streamsize>(size));
    }
    stream.close();
    // close() 在磁盘写满之类的情况下会置 failbit，这里必须看它的脸色
    return !stream.fail();
}

bool copy_file(const std::string& from, const std::string& to) {
    std::error_code code;
    // 目标通常已经存在（同一份输入压第二次），必须允许覆盖
    std::filesystem::copy_file(to_path(from), to_path(to),
                              std::filesystem::copy_options::overwrite_existing, code);
    return !code;
}

void remove_file(const std::string& path) {
    std::error_code code;
    std::filesystem::remove(to_path(path), code);
}

std::string make_temp_dir(const std::string& parent, const std::string& prefix,
                          std::string* error) {
    if (error != nullptr) error->clear();
    const std::filesystem::path base =
        parent.empty() ? std::filesystem::temp_directory_path() : to_path(parent);
    // 名字里带进程号与单调计数：同一台机器上并行跑两份也不会撞车
    static unsigned counter = 0;
    for (int attempt = 0; attempt < 64; ++attempt) {
        char suffix[64];
        std::snprintf(suffix, sizeof(suffix), "%u_%u",
                      static_cast<unsigned>(monotonic_seconds() * 1000.0) & 0xFFFFFFu,
                      ++counter);
        const std::filesystem::path candidate = base / (prefix + suffix);
        std::error_code code;
        if (std::filesystem::create_directory(candidate, code) && !code) {
#if defined(_WIN32)
            return wide_to_utf8(candidate.wstring());
#else
            return candidate.string();
#endif
        }
        if (error != nullptr && error->empty()) {
            // 只在第一次失败时记原因：后面 63 次是同样的错，重复没有信息量
            *error = "create_directory failed for " +
#if defined(_WIN32)
                     wide_to_utf8(candidate.wstring()) +
#else
                     candidate.string() +
#endif
                     " (error " + std::to_string(code.value()) + ": " + code.message() + ")";
        }
    }
    return {};
}

void remove_tree(const std::string& path) {
    if (path.empty()) return;
    std::error_code code;
    std::filesystem::remove_all(to_path(path), code);
}

}  // namespace pdfc
