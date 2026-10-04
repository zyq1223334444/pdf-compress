#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
PDF Compressor – 高性能 PDF 压缩工具

基于 PyMuPDF，支持无损优化和有损渲染压缩，利用多进程加速。

用法（脚本）:        python pdf_compress.py <输入PDF> <目标大小(MB)> [选项]
用法（可执行文件）:  pdf_compress.exe <输入PDF> <目标大小(MB)> [选项]
按指定 DPI 直接渲染: python pdf_compress.py <输入PDF> --dpi 150 [选项]
"""

import argparse
import math
import multiprocessing as mp
import os
import shutil
import signal
import sys
import tempfile
import time
from concurrent.futures import ProcessPoolExecutor, wait as futures_wait
from dataclasses import dataclass

import pymupdf  # PyMuPDF

__version__ = "2.0"

# ========================== 配置常量 ==========================
DPI_BASE = 72.0                 # PDF 用户空间单位：72 pt = 1 英寸

DEFAULT_QUALITY = 85            # 默认 JPEG 质量
MIN_QUALITY = 50                # 质量下限（JPEG 质量低于此值画质劣化明显）
MAX_QUALITY = 100               # 质量上限

# 分辨率上下限：上限由原来的 216 DPI 提高到 600 DPI，以覆盖更多目标大小
DEFAULT_MAX_DPI = 600.0
DEFAULT_MIN_DPI = 7.2           # 等价于原来的 scale 0.1
MAX_SCALE = DEFAULT_MAX_DPI / DPI_BASE
MIN_SCALE = DEFAULT_MIN_DPI / DPI_BASE

# 单页渲染像素上限（内存护栏）：40 Mpx ≈ 120 MB RGB 缓冲，8 进程约 1 GB
MAX_PIXELS_PER_PAGE = 40_000_000

LOSSLESS_MIN_GAIN = 0.01        # 无损优化收益低于 1% 时不写出临时副本
ACCEPT_CLOSEST = 0.05           # closest 策略：误差 ±5%
ACCEPT_SMALLER = 0.10           # smaller 策略：低于目标且误差 ≤10%
DEFAULT_RETRIES = 8             # 自动模式最大渲染次数（实测通常 3 次内命中）
SEQUENTIAL_PAGE_LIMIT = 4       # 页数不超过此值时不分块（避免进程开销）
TEMP_PREFIX = ".pdf_compress_"

EXIT_OK = 0
EXIT_FAIL = 1
EXIT_INTERRUPT = 130


# ========================== 输出辅助 ==========================

def setup_console() -> None:
    """把标准流切成 UTF-8 且永不因编码失败而崩溃。

    Windows 下输出被重定向（批处理、计划任务、CI）时，Python 默认使用
    本地代码页（简体中文为 gbk），此时打印非 GBK 字符会抛 UnicodeEncodeError，
    在旧版本里足以让整轮压缩白跑。这里统一兜住。
    """
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


def emit(message: str = "") -> None:
    """带刷新的打印（子进程/重定向时也能实时看到）。"""
    try:
        print(message, flush=True)
    except Exception:
        pass


def human_size(num_bytes: float) -> str:
    """把字节数格式化成便于阅读的字符串。"""
    num = float(num_bytes)
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if abs(num) < 1024.0 or unit == "TB":
            if unit == "B":
                return f"{num:.0f} {unit}"
            return f"{num:.2f} {unit}"
        num /= 1024.0
    return f"{num:.2f} TB"


def to_mb(num_bytes: float) -> float:
    return num_bytes / (1024.0 * 1024.0)


def size_change(size: int, original: int) -> str:
    """描述相对原文件的变化（变大也是合理结果，不要写成"负的变小"）。"""
    if original <= 0:
        return "大小未知"
    ratio = size / original
    if ratio < 1:
        return f"比原文件小 {(1 - ratio) * 100:.1f}%"
    if ratio > 1:
        return f"比原文件大 {(ratio - 1) * 100:.1f}%"
    return "与原文件相同"


def dpi_to_scale(dpi: float) -> float:
    return dpi / DPI_BASE


def scale_to_dpi(scale: float) -> float:
    return scale * DPI_BASE


# ========================== 异常 ==========================

class PdfError(Exception):
    """可预期的错误（会以友好提示结束，而不是堆栈）。"""


# ========================== 系统信息 ==========================

def logical_cpu_count() -> int:
    return max(1, os.cpu_count() or 1)


def physical_cpu_count() -> int:
    """物理核心数（超线程不重复计数）。

    渲染是纯 CPU 密集型任务，用逻辑核数（含超线程）反而更慢：
    本项目实测 4 物理核的机器上 8 进程比 4 进程慢约 7%。
    """
    try:
        import ctypes
        from ctypes import wintypes

        fn = ctypes.windll.kernel32.GetLogicalProcessorInformationEx
        fn.argtypes = [wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD)]
        fn.restype = wintypes.BOOL

        length = wintypes.DWORD(0)
        fn(0, None, ctypes.byref(length))          # 第一次调用只为取长度
        if length.value == 0:
            raise OSError("no data")
        buf = ctypes.create_string_buffer(length.value)
        if not fn(0, buf, ctypes.byref(length)):
            raise OSError("GetLogicalProcessorInformationEx failed")

        cores = 0
        offset = 0
        while offset + 8 <= length.value:
            relationship = wintypes.DWORD.from_buffer(buf, offset).value
            size = wintypes.DWORD.from_buffer(buf, offset + 4).value
            if size < 8:
                break
            if relationship == 0:                  # RelationProcessorCore
                cores += 1
            offset += size
        if cores:
            return cores
    except Exception:
        pass
    return max(1, logical_cpu_count() // 2)


def resolve_workers(jobs: int) -> int:
    """把 --jobs 解析成实际进程数（0 = 自动）。"""
    if jobs and jobs > 0:
        return max(1, min(jobs, 64))
    return max(1, physical_cpu_count())


# ========================== PDF 基础操作 ==========================

def open_pdf(path: str):
    """打开 PDF，失败时抛 PdfError（带中文说明）。"""
    try:
        doc = pymupdf.open(path)
    except Exception as exc:
        raise PdfError(f"无法打开文件（不是有效的 PDF，或文件已损坏）：{exc}") from exc
    if doc.needs_pass:
        doc.close()
        raise PdfError("该 PDF 已加密，需要密码。请先用其他工具解除密码保护后再压缩。")
    if len(doc) == 0:
        doc.close()
        raise PdfError("该 PDF 没有任何页面。")
    return doc


def optimize_pdf(input_path: str, output_path: str) -> int:
    """无损优化 PDF 并写入文件，返回输出大小（字节）。"""
    doc = pymupdf.open(input_path)
    try:
        doc.save(output_path, garbage=4, deflate=True, clean=True)
    finally:
        doc.close()
    return os.path.getsize(output_path)


def optimize_bytes(input_path: str) -> bytes:
    """无损优化并直接在内存里取结果，避免为了"看一眼大小"而写一份完整副本。"""
    doc = pymupdf.open(input_path)
    try:
        return doc.tobytes(garbage=4, deflate=True, clean=True)
    except AttributeError:      # 老版本 PyMuPDF 没有 tobytes
        tmp = None
        try:
            fd, tmp = tempfile.mkstemp(suffix=".pdf")
            os.close(fd)
            doc.save(tmp, garbage=4, deflate=True, clean=True)
            with open(tmp, "rb") as handle:
                return handle.read()
        finally:
            if tmp and os.path.exists(tmp):
                os.remove(tmp)
    finally:
        doc.close()


# ========================== 渲染压缩 ==========================

def _page_plan(doc, scale: float, max_pixels: int):
    """计算每页的实际渲染 scale（应用像素护栏），返回 (计划, 被压制的页)。"""
    plan = []
    clipped = []
    for index, page in enumerate(doc):
        effective = scale
        rect = page.rect
        pixels = (rect.width * scale) * (rect.height * scale)
        if max_pixels and pixels > max_pixels:
            effective = scale * math.sqrt(max_pixels / pixels)
            clipped.append((index + 1, effective))
        plan.append((index, effective))
    return plan, clipped


def _render_into(new_doc, page, scale: float, jpeg_quality: int,
                 keep_page_box: bool) -> None:
    """把一页渲染成 JPEG 并作为一页放进 new_doc。

    关键点：scale 只决定图像像素数（DPI = 72 × scale），
    输出页面的物理尺寸保持原样，不会因为缩放而变成 A2/半张 A4。
    """
    rect = page.rect
    matrix = pymupdf.Matrix(scale, scale)
    pix = page.get_pixmap(matrix=matrix, colorspace=pymupdf.csRGB)
    data = pix.tobytes("jpeg", jpeg_quality)

    if keep_page_box and page.rotation == 0:
        # 保留 MediaBox，把裁切后的内容画回原来的位置
        media = page.mediabox
        crop = page.cropbox
        x0 = crop.x0 - media.x0
        y0 = crop.y0 - media.y0
        width, height = media.width, media.height
        target = pymupdf.Rect(x0, y0, x0 + rect.width, y0 + rect.height)
    else:
        width, height = rect.width, rect.height
        target = pymupdf.Rect(0, 0, width, height)
    new_page = new_doc.new_page(width=width, height=height)
    new_page.insert_image(target, stream=data)


def process_chunk(input_path: str, items, jpeg_quality: int, temp_dir: str,
                  chunk_id: int, keep_page_box: bool) -> str:
    """多进程子任务：渲染一个页面块并保存为临时 PDF。

    items: [(页索引, 实际 scale), ...]
    """
    temp_path = os.path.join(temp_dir, f"chunk_{chunk_id:04d}.pdf")
    doc = pymupdf.open(input_path)
    new_doc = pymupdf.open()
    try:
        for index, scale in items:
            _render_into(new_doc, doc[index], scale, jpeg_quality, keep_page_box)
        new_doc.save(temp_path, garbage=4, deflate=True, clean=True)
    finally:
        new_doc.close()
        doc.close()
    return temp_path


def render_compress_pdf(input_path: str, scale: float, output_path: str,
                        jpeg_quality: int = DEFAULT_QUALITY, *,
                        workers: int | None = None, progress=None,
                        keep_page_box: bool = False,
                        max_pixels: int = MAX_PIXELS_PER_PAGE,
                        warn=None) -> int:
    """有损渲染压缩：每页渲染为 JPEG 后拼成新 PDF，返回输出大小。

    scale 只决定图像像素数（DPI = 72 × scale），页面物理尺寸保持不变。
    """
    doc = pymupdf.open(input_path)
    try:
        total_pages = len(doc)
        plan, clipped = _page_plan(doc, scale, max_pixels)
    finally:
        doc.close()

    if clipped and warn:
        first = clipped[0]
        warn(f"[警告] 第 {first[0]} 页尺寸过大，已自动把分辨率从 {scale_to_dpi(scale):.0f} DPI "
             f"降到 {scale_to_dpi(first[1]):.0f} DPI（单页像素上限 "
             f"{max_pixels / 1e6:.0f} Mpx，避免内存溢出）")

    workers = resolve_workers(workers or 0)
    if total_pages <= SEQUENTIAL_PAGE_LIMIT or workers <= 1:
        doc = pymupdf.open(input_path)
        new_doc = pymupdf.open()
        try:
            for done, (index, page_scale) in enumerate(plan, start=1):
                _render_into(new_doc, doc[index], page_scale, jpeg_quality, keep_page_box)
                if progress:
                    progress(done, total_pages)
            new_doc.save(output_path, garbage=4, deflate=True, clean=True)
        finally:
            new_doc.close()
            doc.close()
        return os.path.getsize(output_path)

    # 分块：块数不超过进程数，且尽量均分（尾部不再出现只有 1 页的小块）
    chunks = _split_chunks([item for item in plan], workers)
    with tempfile.TemporaryDirectory(prefix="pdfchunks_") as temp_dir:
        chunk_files = [None] * len(chunks)
        with ProcessPoolExecutor(max_workers=min(workers, len(chunks))) as executor:
            futures = {}
            for chunk_id, items in enumerate(chunks):
                futures[executor.submit(process_chunk, input_path, items,
                                        jpeg_quality, temp_dir, chunk_id,
                                        keep_page_box)] = chunk_id
            pending = set(futures)
            done_pages = 0
            started = time.monotonic()
            while pending:
                finished, pending = futures_wait(pending, timeout=0.3)
                for future in finished:
                    chunk_id = futures[future]
                    chunk_files[chunk_id] = future.result()
                    done_pages += len(chunks[chunk_id])
                    if progress:
                        progress(done_pages, total_pages, time.monotonic() - started)

        merged = pymupdf.open()
        try:
            for chunk_file in chunk_files:
                if not chunk_file:
                    continue
                part = pymupdf.open(chunk_file)
                try:
                    merged.insert_pdf(part)
                finally:
                    part.close()
            merged.save(output_path, garbage=4, deflate=True, clean=True)
        finally:
            merged.close()
    return os.path.getsize(output_path)


def _split_chunks(items, workers: int):
    """把页面均分成连续块，块数 ≤ workers。"""
    workers = max(1, min(workers, len(items)))
    size = math.ceil(len(items) / workers)
    return [items[i:i + size] for i in range(0, len(items), size)]


# ========================== 参数搜索 ==========================

@dataclass
class Trial:
    """一次尝试的记录。scale=0 表示无损优化结果。"""
    scale: float
    quality: int
    size: int
    path: str
    is_input: bool = False

    @property
    def dpi(self) -> float:
        return scale_to_dpi(self.scale)

    @property
    def rendered(self) -> bool:
        return self.scale > 0


@dataclass
class Decision:
    """搜索的下一步：要么给出参数，要么说明无法达到。"""
    scale: float = 0.0
    quality: int = DEFAULT_QUALITY
    stop: bool = False
    reason: str = ""


def accept_range(target: int, strategy: str):
    """返回当前策略的合格区间 (下界, 上界)。"""
    if strategy == "smaller":
        return target * (1.0 - ACCEPT_SMALLER), float(target)
    return target * (1.0 - ACCEPT_CLOSEST), target * (1.0 + ACCEPT_CLOSEST)


def is_accepted(size: int, target: int, strategy: str) -> bool:
    low, high = accept_range(target, strategy)
    if strategy == "smaller":
        return low <= size < target
    return low <= size <= high


def pick_best(trials, target: int, strategy: str) -> Trial | None:
    """按策略挑选最佳结果。"""
    if not trials:
        return None
    if strategy == "smaller":
        under = [t for t in trials if t.size < target]
        if under:
            # 优先最接近目标，其次分辨率高、质量高
            return min(under, key=lambda t: (abs(target - t.size), -t.scale, -t.quality))
        return min(trials, key=lambda t: t.size)
    return min(trials, key=lambda t: (abs(t.size - target), -t.scale))


def _log(value: float) -> float:
    return math.log(max(value, 1e-9))


def predict_scale(trials, target: int, smin: float, smax: float) -> float | None:
    """用已测得的数据点推算能达到目标的 scale。

    渲染后文件大小近似满足 size ∝ scale^p（p 通常在 1.8~2.2）。
    这里在 log-log 空间做插值/外推，斜率取自实测两点，并限制在合理范围内，
    因此通常 2~3 次尝试即可命中目标（旧版固定步长需要 7 次以上）。
    """
    points = sorted({(t.scale, t.size) for t in trials if t.rendered and t.size > 0})
    if not points:
        return None

    slope = 2.0
    base_scale, base_size = points[-1]
    if len(points) >= 2:
        bracket = None
        for left, right in zip(points, points[1:]):
            if left[1] <= target <= right[1]:
                bracket = (left, right)
                break
        if bracket is None:
            bracket = (points[0], points[1]) if target < points[0][1] else (points[-2], points[-1])
        (s1, z1), (s2, z2) = bracket
        if abs(_log(s2) - _log(s1)) > 1e-6 and abs(_log(z2) - _log(z1)) > 1e-6:
            slope = (_log(z2) - _log(z1)) / (_log(s2) - _log(s1))
        slope = min(max(slope, 1.2), 3.0)
        base_scale, base_size = bracket[1] if target >= bracket[1][1] else bracket[0]

    scale = base_scale * (target / base_size) ** (1.0 / slope)
    return max(smin, min(smax, scale))


def aim_size(target: int, strategy: str) -> int:
    """搜索时瞄准的大小 = 合格区间的中心。

    closest 的中心就是 target；但 smaller 的区间是 [0.9t, t)，
    如果瞄准 t 就会一直贴在区间边界上（实测会卡在 8.01MB 与 8.00MB 之间来回），
    瞄准区间中心（0.95t）才能一次落进区间。
    """
    low, high = accept_range(target, strategy)
    return int((low + high) / 2.0)


def _same_scale(a: float, b: float, rel: float = 0.01) -> bool:
    """两个 scale 是否算"同一个"（相对误差 1% 以内）。"""
    return abs(a - b) <= rel * max(abs(a), abs(b), 1e-9)


def predict_quality(points, target: int, qmin: int, qmax: int) -> int | None:
    """在固定 scale 下，用已测的 (quality, size) 点推算能命中目标的质量。"""
    pts = sorted(set(points))
    if len(pts) < 2:
        return None
    bracket = None
    for left, right in zip(pts, pts[1:]):
        if left[1] <= target <= right[1] or right[1] <= target <= left[1]:
            bracket = (left, right)
            break
    if bracket is None:
        bracket = (pts[0], pts[1]) if target < pts[0][1] else (pts[-2], pts[-1])
    (q1, z1), (q2, z2) = bracket
    if abs(_log(q2) - _log(q1)) < 1e-6 or abs(_log(z2) - _log(z1)) < 1e-6:
        return None
    slope = (_log(z2) - _log(z1)) / (_log(q2) - _log(q1))
    slope = min(max(slope, 0.2), 4.0)
    base_q, base_z = bracket[1] if target >= bracket[1][1] else bracket[0]
    quality = base_q * (target / base_z) ** (1.0 / slope)
    return int(round(max(qmin, min(qmax, quality))))


def _bound_plan(bound: float, trials, target: int, strategy: str, *,
                qmin: int, qmax: int, base_quality: int, grow: bool) -> Decision:
    """scale 已顶到边界时改用 JPEG 质量作为副旋钮。

    第一次探测边界时直接用最极端的一档（要变小就用最低质量，要变大就用最高质量），
    这样"目标不可达"只需 2 次渲染就能确定，而不是逐档试完整个质量阶梯。
    """
    low, high = accept_range(target, strategy)
    aim = aim_size(target, strategy)
    points = sorted({(t.quality, t.size) for t in trials
                     if abs(t.scale - bound) <= 1e-6 + bound * 1e-6})
    tried = {q for q, _ in points}
    extreme = qmax if grow else qmin

    if not points:
        return Decision(scale=bound, quality=extreme)

    extreme_size = next((z for q, z in points if q == extreme), None)
    if extreme_size is not None:
        if grow and extreme_size < low:
            return Decision(stop=True, reason=(
                f"已达最大设置（{scale_to_dpi(bound):.0f} DPI / JPEG {qmax}），文件只有 "
                f"{human_size(extreme_size)}，无法升到目标 {to_mb(target):.2f} MB 附近"))
        if not grow and extreme_size > high:
            return Decision(stop=True, reason=(
                f"已达最小设置（{scale_to_dpi(bound):.0f} DPI / JPEG {qmin}），文件仍有 "
                f"{human_size(extreme_size)}，无法降到目标 {to_mb(target):.2f} MB 附近"))

    predicted = predict_quality(points, aim, qmin, qmax)
    if predicted is not None and predicted not in tried:
        return Decision(scale=bound, quality=predicted)

    # 还差一个基准点才能插值
    if base_quality not in tried:
        return Decision(scale=bound, quality=base_quality)

    for quality in sorted(range(qmin, qmax + 1),
                          key=lambda q: abs(q - (predicted or base_quality))):
        if quality not in tried:
            return Decision(scale=bound, quality=quality)

    sizes = [z for _, z in points]
    return Decision(stop=True, reason=(
        f"{scale_to_dpi(bound):.0f} DPI 下所有 JPEG 质量都已试过，文件大小只能落在 "
        f"{human_size(min(sizes))} ~ {human_size(max(sizes))} 之间，无法逼近目标 "
        f"{to_mb(target):.2f} MB"))


def plan_next(trials, target: int, strategy: str, *, smin: float, smax: float,
              qmin: int = MIN_QUALITY, qmax: int = MAX_QUALITY,
              base_quality: int = DEFAULT_QUALITY) -> Decision:
    """给出下一次尝试的参数，或判断目标不可达。

    第一步在 72 DPI 标定一次，之后用 log-log 幂律插值直接跳到目标附近，
    因此通常 2~3 次渲染即可命中；只有 scale 顶到上下限时才动用 JPEG 质量。
    """
    rendered = [t for t in trials if t.rendered]
    if not rendered:
        return Decision(scale=min(max(1.0, smin), smax), quality=base_quality)

    tried = {(round(t.scale, 4), t.quality) for t in rendered}
    aim = aim_size(target, strategy)

    scale = predict_scale(rendered, aim, smin, smax)
    if scale is None:
        return Decision(stop=True, reason="没有可用的测算数据")

    if scale >= smax:
        return _bound_plan(smax, rendered, target, strategy, qmin=qmin, qmax=qmax,
                           base_quality=base_quality, grow=True)
    if scale <= smin:
        return _bound_plan(smin, rendered, target, strategy, qmin=qmin, qmax=qmax,
                           base_quality=base_quality, grow=False)

    # 与已试过的 scale 太接近就不再浪费一次渲染：改用夹逼
    if any(q == base_quality and _same_scale(scale, s) for s, q in tried):
        under = [t for t in rendered if t.size < aim]
        over = [t for t in rendered if t.size >= aim]
        if under and over:
            lo = max(under, key=lambda t: t.scale)
            hi = min(over, key=lambda t: t.scale)
            if hi.scale / lo.scale > 1.005:
                return Decision(scale=math.sqrt(lo.scale * hi.scale), quality=base_quality)
            return Decision(stop=True, reason=(
                f"已把参数收缩到 {scale_to_dpi(lo.scale):.0f}~{scale_to_dpi(hi.scale):.0f} DPI，"
                f"文件大小在 {human_size(lo.size)} ~ {human_size(hi.size)} 之间，"
                f"该策略要求的误差范围已无法再逼近"))
        last = rendered[-1]
        scale = min(smax, scale * 1.08) if last.size < aim else max(smin, scale * 0.92)
        if any(q == base_quality and _same_scale(scale, s) for s, q in tried):
            return Decision(stop=True, reason="参数已无可调整空间")
    return Decision(scale=scale, quality=base_quality)


# ========================== 主流程 ==========================

def _ask(question: str, default: bool = False) -> bool:
    try:
        answer = input(question).strip().lower()
    except (EOFError, KeyboardInterrupt):
        return default
    return answer in ("y", "yes", "是", "1")


def _interactive() -> bool:
    try:
        return bool(sys.stdin) and sys.stdin.isatty() and bool(sys.stdout) and sys.stdout.isatty()
    except Exception:
        return False


def _finalize(best: Trial, final_path: str) -> None:
    """把最佳结果放到最终路径（无损结果可能就是原文件，此时复制而不是移动）。"""
    if os.path.abspath(best.path) == os.path.abspath(final_path):
        return
    if best.is_input:
        shutil.copy2(best.path, final_path)
    else:
        shutil.move(best.path, final_path)


def _report_final(best: Trial, target: int, strategy: str, original: int,
                  attempts: int, trials=None) -> None:
    deviation = (best.size - target) / target
    emit(f"[结果] 最终大小 {human_size(best.size)}（目标 {to_mb(target):.2f} MB，"
         f"误差 {deviation:+.1%}），{size_change(best.size, original)}，"
         f"共渲染 {attempts} 次")
    if is_accepted(best.size, target, strategy):
        return
    emit(f"[警告] 未落在 {strategy} 策略要求的区间内，已输出最接近目标的结果")
    # smaller 策略下"勉强超过目标一点点"的结果会被放弃，转而输出小得多的文件；
    # 这里明确指出另一种选择，避免用户以为工具做不到。
    if trials:
        closest = min(trials, key=lambda t: abs(t.size - target))
        if closest is not best and abs(closest.size - target) < abs(best.size - target) / 2:
            emit(f"[提示] 另有更接近目标的结果 {human_size(closest.size)}"
                 f"（误差 {(closest.size - target) / target:+.1%}），但不满足 smaller 的"
                 f"“必须小于目标”要求；如需它可以改用 --strategy closest")


def ask_force_grow(args, size: int, target: int) -> bool:
    """无损结果已小于目标时，决定是否仍要用有损渲染把文件做大。

    返回 True 表示继续渲染。交互环境会询问，非交互环境默认不渲染
    （除非显式给出 --force-render）。
    """
    emit(f"[提示] 无损优化后为 {human_size(size)}，已经小于目标 {to_mb(target):.2f} MB。")
    emit("[提示] 继续做有损渲染只会让文件变大，不会提高画质或清晰度"
         "（分辨率、细节不会增加），仅仅是文件体积变大。")
    if args.force_render:
        emit("[提示] 已指定 --force-render，继续有损渲染以逼近目标大小")
        return True
    if _interactive():
        return _ask(f"是否使用有损渲染强行把文件做大到 {to_mb(target):.2f} MB 附近？"
                    f"（不会提高画质，只会变大）[y/N]: ", default=False)
    emit("[提示] 非交互环境，默认保留无损结果（如需强行逼近目标，请加 --force-render）")
    return False


def run(args) -> int:
    """执行一次压缩，返回退出码。"""
    input_path = os.path.abspath(args.input)
    if not os.path.isfile(input_path):
        emit(f"[失败] 文件不存在：{input_path}")
        return EXIT_FAIL

    doc = open_pdf(input_path)
    try:
        total_pages = len(doc)
    finally:
        doc.close()

    fixed_scale = None
    target = 0
    if args.dpi is not None:
        if args.dpi <= 0:
            emit("[失败] --dpi 必须大于 0")
            return EXIT_FAIL
        fixed_scale = dpi_to_scale(args.dpi)
        if args.target_size:
            emit(f"[提示] 已指定 --dpi {args.dpi:g}，忽略目标大小 {args.target_size:g} MB")
        target = int(round(args.target_size * 1024 * 1024)) if args.target_size else 0
    else:
        if args.target_size is None:
            emit("[失败] 需要给出目标大小(MB)，或使用 --dpi 指定固定分辨率")
            return EXIT_FAIL
        if args.target_size <= 0:
            emit("[失败] 目标大小必须大于 0")
            return EXIT_FAIL
        target = int(round(args.target_size * 1024 * 1024))

    dpi_max = args.max_dpi
    dpi_min = args.min_dpi
    if dpi_min >= dpi_max:
        emit(f"[失败] --min-dpi({dpi_min:g}) 必须小于 --max-dpi({dpi_max:g})")
        return EXIT_FAIL
    smin, smax = dpi_to_scale(dpi_min), dpi_to_scale(dpi_max)

    output_dir = args.output_dir or os.path.dirname(input_path) or "."
    try:
        os.makedirs(output_dir, exist_ok=True)
    except OSError as exc:
        emit(f"[失败] 无法创建输出目录：{exc}")
        return EXIT_FAIL

    base = os.path.splitext(os.path.basename(input_path))[0]
    final_path = os.path.join(output_dir, base + "_compressed.pdf")
    original_size = os.path.getsize(input_path)
    workers = resolve_workers(args.jobs)

    emit(f"[任务] 输入 {os.path.basename(input_path)}（{human_size(original_size)}，{total_pages} 页）")
    if fixed_scale is not None:
        emit(f"[任务] 固定分辨率 {args.dpi:g} DPI（scale={fixed_scale:.3f}），进程数 {workers}")
    else:
        emit(f"[任务] 目标 {to_mb(target):.2f} MB，策略 {args.strategy}，"
             f"分辨率范围 {dpi_min:g}~{dpi_max:g} DPI，进程数 {workers}")

    if args.dry_run:
        emit("[提示] --dry-run：仅显示计划，不做实际压缩")
        return EXIT_OK

    run_dir = tempfile.mkdtemp(prefix=TEMP_PREFIX, dir=output_dir)
    trials: list[Trial] = []
    attempts = 0
    started = time.monotonic()
    try:
        # ---------- 固定 DPI：一次渲染结束 ----------
        if fixed_scale is not None:
            path = os.path.join(run_dir, "fixed.pdf")
            emit(f"[渲染] 第 1/1 次：{args.dpi:g} DPI（scale={fixed_scale:.3f}），"
                 f"JPEG {DEFAULT_QUALITY}（--dpi 固定分辨率，不做搜索）")
            size = render_compress_pdf(
                input_path, fixed_scale, path, DEFAULT_QUALITY, workers=workers,
                keep_page_box=args.keep_page_box, progress=_progress_printer(), warn=emit)
            trials.append(Trial(fixed_scale, DEFAULT_QUALITY, size, path))
            attempts = 1
            _finalize(trials[0], final_path)
            emit(f"[成功] 已按 {args.dpi:g} DPI 输出：{final_path}")
            emit(f"[结果] 文件大小 {human_size(size)}（{size_change(size, original_size)}），"
                 f"页数 {total_pages}，用时 {time.monotonic() - started:.1f}s")
            if target:
                _report_final(trials[0], target, args.strategy, original_size, attempts, trials)
            return EXIT_OK

        # ---------- 第一阶段：无损优化 ----------
        emit("[无损] 正在做无损优化（不改变画质）...")
        try:
            optimized = optimize_bytes(input_path)
        except Exception as exc:
            raise PdfError(f"无损优化失败：{exc}") from exc
        optimized_size = len(optimized)
        gain = 1.0 - optimized_size / original_size
        emit(f"[无损] {human_size(original_size)} → {human_size(optimized_size)}（收益 {gain:+.1%}）")

        if is_accepted(optimized_size, target, args.strategy):
            path = os.path.join(run_dir, "lossless.pdf")
            with open(path, "wb") as handle:
                handle.write(optimized)
            _finalize(Trial(0.0, 0, optimized_size, path), final_path)
            emit(f"[成功] 无损优化后即符合策略，已保存：{final_path}")
            emit(f"[结果] 文件大小 {human_size(optimized_size)}"
                 f"（目标 {to_mb(target):.2f} MB，误差 {(optimized_size - target) / target:+.1%}）")
            return EXIT_OK

        if gain >= LOSSLESS_MIN_GAIN:
            path = os.path.join(run_dir, "lossless.pdf")
            with open(path, "wb") as handle:
                handle.write(optimized)
            trials.append(Trial(0.0, 0, optimized_size, path))
        else:
            emit("[无损] 收益可忽略（<1%），跳过写出临时副本")
            trials.append(Trial(0.0, 0, original_size, input_path, is_input=True))
        del optimized

        # ---------- 无损结果已小于目标：需要用户决定是否强行做大 ----------
        # 注意用严格小于：若恰好等于目标，smaller 策略仍需要更小，应继续走渲染搜索
        if optimized_size < target:
            if not ask_force_grow(args, optimized_size, target):
                best = trials[0]
                _finalize(best, final_path)
                emit(f"[成功] 已保留无损优化结果：{final_path}")
                emit(f"[结果] 文件大小 {human_size(best.size)}"
                     f"（目标 {to_mb(target):.2f} MB，误差 {(best.size - target) / target:+.1%}）")
                return EXIT_OK

        # ---------- 第二阶段：渲染压缩 ----------
        low, high = accept_range(target, args.strategy)
        emit(f"[渲染] 需要命中的区间：{to_mb(low):.2f} ~ {to_mb(high):.2f} MB"
             f"（策略 {args.strategy}）")

        if args.mode == "manual":
            return _manual_flow(args, input_path, trials, run_dir, final_path,
                                target, original_size, workers, smin, smax)

        max_attempts = max(1, args.max_retries)
        decision = plan_next(trials, target, args.strategy, smin=smin, smax=smax)
        while attempts < max_attempts and not decision.stop:
            attempts += 1
            scale, quality = decision.scale, decision.quality
            path = os.path.join(run_dir, f"try{attempts:02d}.pdf")
            emit(f"[渲染] 第 {attempts}/{max_attempts} 次：{scale_to_dpi(scale):.0f} DPI"
                 f"（scale={scale:.3f}），JPEG {quality}")
            size = render_compress_pdf(
                input_path, scale, path, quality, workers=workers,
                keep_page_box=args.keep_page_box, progress=_progress_printer(), warn=emit)
            trial = Trial(scale, quality, size, path)
            trials.append(trial)
            emit(f"[渲染] 得到 {human_size(size)}（{size / target:.3f} 倍目标，"
                 f"误差 {(size - target) / target:+.1%}）")

            if is_accepted(size, target, args.strategy):
                _finalize(trial, final_path)
                emit(f"[成功] 已命中目标：{final_path}")
                _report_final(trial, target, args.strategy, original_size, attempts, trials)
                return EXIT_OK
            decision = plan_next(trials, target, args.strategy, smin=smin, smax=smax)
            if not decision.stop:
                emit(f"[搜索] 下一次预计：{scale_to_dpi(decision.scale):.0f} DPI，"
                     f"JPEG {decision.quality}")

        if decision.stop:
            emit(f"[警告] {decision.reason}")
        else:
            emit(f"[警告] 已达到最大尝试次数（{max_attempts}），未能落入目标区间")

        best = pick_best(trials, target, args.strategy)
        if best is None:
            emit("[失败] 没有生成任何可用结果")
            return EXIT_FAIL
        _finalize(best, final_path)
        emit(f"[成功] 已输出最接近目标的结果：{final_path}")
        _report_final(best, target, args.strategy, original_size, attempts, trials)
        if best.rendered:
            emit(f"[提示] 该结果相当于 {best.dpi:.0f} DPI / JPEG {best.quality}；"
                 f"如可接受其他大小，可据此调整目标值")
        return EXIT_OK

    except PdfError as exc:
        emit(f"[失败] {exc}")
        return EXIT_FAIL
    except KeyboardInterrupt:
        emit("")
        emit("[中断] 用户中断，正在保存已生成的最佳结果...")
        best = pick_best(trials, target or original_size, args.strategy)
        if best is not None:
            try:
                _finalize(best, final_path)
                emit(f"[中断] 已保留最佳结果：{final_path}（{human_size(best.size)}）")
            except Exception as exc:
                emit(f"[中断] 保存最佳结果失败：{exc}")
        else:
            emit("[中断] 尚未生成任何结果，未输出文件")
        emit(f"[中断] 用时 {time.monotonic() - started:.1f}s，退出码 {EXIT_INTERRUPT}")
        return EXIT_INTERRUPT
    except Exception as exc:
        emit(f"[失败] 发生错误：{exc}")
        return EXIT_FAIL
    finally:
        shutil.rmtree(run_dir, ignore_errors=True)


def _progress_printer():
    """渲染进度回调（只在完成页数变化时打印）。"""
    state = {"done": -1}

    def show(done: int, total: int, elapsed: float = 0.0) -> None:
        if done == state["done"]:
            return
        state["done"] = done
        percent = done / total * 100 if total else 100.0
        emit(f"[渲染] 进度 {done}/{total} 页（{percent:.0f}%），已用 {elapsed:.1f}s")

    return show


def _manual_flow(args, input_path, trials, run_dir, final_path, target,
                 original_size, workers, smin, smax) -> int:
    """手动模式：参数由用户逐步调整（无效输入不会触发重新渲染）。"""
    lossless = trials[0]
    emit(f"[手动] 无损优化结果：{human_size(lossless.size)}"
         f"（目标 {to_mb(target):.2f} MB）")
    if _ask("是否直接接受无损优化结果？(y/n): ", default=True):
        _finalize(lossless, final_path)
        emit(f"[成功] 已保存无损优化结果：{final_path}")
        return EXIT_OK

    scale = min(max(1.0, smin), smax)
    quality = DEFAULT_QUALITY
    attempt = 0
    while True:
        attempt += 1
        path = os.path.join(run_dir, f"manual{attempt:02d}.pdf")
        emit(f"[渲染] 第 {attempt} 次：{scale_to_dpi(scale):.0f} DPI（scale={scale:.3f}），"
             f"JPEG {quality}")
        size = render_compress_pdf(
            input_path, scale, path, quality, workers=workers,
            keep_page_box=args.keep_page_box, progress=_progress_printer(), warn=emit)
        trial = Trial(scale, quality, size, path)
        trials.append(trial)
        deviation = (size - target) / target
        emit(f"[渲染] 当前大小 {human_size(size)}（目标 {to_mb(target):.2f} MB，"
             f"误差 {deviation:+.1%}）")

        # 先给建议，再读输入：无效输入只会重新提问，不会重新渲染
        hint = predict_scale(trials, aim_size(target, args.strategy), smin, smax)
        if hint is not None:
            emit(f"[提示] 若要命中目标，建议分辨率约 {scale_to_dpi(hint):.0f} DPI"
                 f"（当前 {scale_to_dpi(scale):.0f} DPI）")
        emit("[提示] 输入 y=接受当前结果，larger=提高分辨率/质量，"
             "smaller=降低分辨率/质量，q=退出并保留最佳结果")
        answer = ""
        while True:
            try:
                answer = input("请选择 (y/larger/smaller/q): ").strip().lower()
            except EOFError:
                answer = "y"
            if answer in ("y", "yes", "是", "larger", "smaller", "q", "quit", "exit"):
                break
            emit("[提示] 无效输入，请输入 y / larger / smaller / q")

        if answer in ("y", "yes", "是"):
            _finalize(trial, final_path)
            emit(f"[成功] 已保存：{final_path}")
            _report_final(trial, target, args.strategy, original_size, attempt, trials)
            return EXIT_OK
        if answer in ("q", "quit", "exit"):
            best = pick_best(trials, target, args.strategy)
            if best is not None:
                _finalize(best, final_path)
                emit(f"[成功] 已保存最佳结果：{final_path}（{human_size(best.size)}）")
            return EXIT_OK

        if scale >= smax or (scale > smax * 0.95 and answer == "larger"):
            scale = smax
            quality = max(MIN_QUALITY, min(MAX_QUALITY, quality + (5 if answer == "larger" else -5)))
            emit(f"[提示] 分辨率已达上限 {scale_to_dpi(smax):.0f} DPI，改为调整 JPEG 质量")
        elif scale <= smin or (scale < smin * 1.05 and answer == "smaller"):
            scale = smin
            quality = max(MIN_QUALITY, min(MAX_QUALITY, quality + (5 if answer == "larger" else -5)))
            emit(f"[提示] 分辨率已达下限 {scale_to_dpi(smin):.0f} DPI，改为调整 JPEG 质量")
        else:
            step = 0.15 if answer == "larger" else -0.15
            scale = max(smin, min(smax, scale + step))


# ========================== 命令行 ==========================

HELP_ZH = """
用法（脚本）:     python pdf_compress.py <输入PDF> <目标大小(MB)> [选项]
用法（可执行）:   pdf_compress.exe <输入PDF> <目标大小(MB)> [选项]
固定 DPI 渲染:    python pdf_compress.py <输入PDF> --dpi 150 [选项]

选项:
  -o, --output-dir <目录>   输出目录（默认与输入文件同目录）
  --mode {auto,manual}      auto=自动搜索（默认），manual=手动逐步调整
  --strategy {closest,smaller}
                            closest=最接近目标（误差≤5%，可大于或小于，默认）
                            smaller=必须小于目标（误差≤10%）
  --max-retries <次数>      自动模式最大渲染次数（默认 8，正常 3 次内命中）
  --dpi <数值>              直接按指定分辨率渲染一次（可省略目标大小）
  --max-dpi <数值>          自动搜索的分辨率上限（默认 600）
  --min-dpi <数值>          自动搜索的分辨率下限（默认 7.2）
  -j, --jobs <数值>         渲染进程数（默认=物理核心数，0 表示自动）
  --force-render            无损结果已小于目标时，也强制有损渲染把文件做大
  --keep-page-box           保留原始页面尺寸（MediaBox），不因缩放改变纸张大小
  --dry-run                 只显示计划，不做实际压缩
  --help-zh                 显示本帮助
  -h, --help                显示英文帮助

说明:
  - 有损渲染会丢弃文本与矢量信息，输出是图片集合，最适合扫描件/图片型 PDF。
  - scale=分辨率/72。渲染只改变图像像素数（DPI），不再改变页面物理尺寸。
  - 分辨率越高、JPEG 质量越高，文件越大。搜索会用实测数据推算所需参数，
    通常 2~3 次即可命中目标；若目标本身不可达会直接告知可做到的范围。
  - 单页渲染像素上限 40 Mpx（约 120 MB 内存/进程），超出会自动降低该页分辨率。
  - 按 Ctrl+C 可中断：会保留已生成的最佳结果，并在结束时清理全部临时文件。
"""


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="pdf_compress",
        description="Compress a PDF towards a target size (lossless optimisation + "
                    "lossy JPEG rendering, multi-process accelerated). "
                    "Use --help-zh for Chinese help.")
    parser.add_argument("input", help="Input PDF file path")
    parser.add_argument("target_size", nargs="?", type=float,
                        help="Target size in MB (optional when --dpi is given)")
    parser.add_argument("-o", "--output-dir",
                        help="Output directory (default: same as input file)")
    parser.add_argument("--mode", choices=["auto", "manual"], default="auto",
                        help="auto = automatic search (default), manual = step-by-step")
    parser.add_argument("--strategy", choices=["closest", "smaller"], default="closest",
                        help="closest: within 5%% of target (default); "
                             "smaller: below target, within 10%%")
    parser.add_argument("--max-retries", type=int, default=DEFAULT_RETRIES,
                        help=f"Maximum number of render attempts (default: {DEFAULT_RETRIES})")
    parser.add_argument("--dpi", type=float, default=None,
                        help="Render once at this fixed resolution (DPI)")
    parser.add_argument("--max-dpi", type=float, default=DEFAULT_MAX_DPI,
                        help=f"Upper resolution bound for the search (default: {DEFAULT_MAX_DPI:g})")
    parser.add_argument("--min-dpi", type=float, default=DEFAULT_MIN_DPI,
                        help=f"Lower resolution bound for the search (default: {DEFAULT_MIN_DPI:g})")
    parser.add_argument("-j", "--jobs", type=int, default=0,
                        help="Worker processes (default: physical core count; 0 = auto)")
    parser.add_argument("--force-render", action="store_true",
                        help="Render even when the lossless result is already smaller than "
                             "the target (makes the file bigger, NOT better)")
    parser.add_argument("--keep-page-box", action="store_true",
                        help="Keep the original page size (MediaBox) instead of the crop box")
    parser.add_argument("--dry-run", action="store_true", help="Show the plan only")
    parser.add_argument("--help-zh", action="store_true", help="Show Chinese help")
    return parser


def _sigint_handler(sig, frame):
    """把 Ctrl+C 变成 KeyboardInterrupt，由主流程统一收尾（保留最佳结果）。"""
    raise KeyboardInterrupt


def main(argv=None) -> int:
    setup_console()
    try:
        signal.signal(signal.SIGINT, _sigint_handler)
    except Exception:
        pass

    if argv is None:
        argv = sys.argv[1:]
    if "--help-zh" in argv:
        emit(HELP_ZH)
        return EXIT_OK

    parser = build_parser()
    try:
        args = parser.parse_args(argv)
    except SystemExit as exc:
        return int(exc.code or 0)
    return run(args)


if __name__ == "__main__":
    # 多进程需要在主模块中启动（Windows 下必需）
    sys.exit(main())
