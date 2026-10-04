# PDF Compressor（PDF 压缩工具）

一款基于 PyMuPDF 的高性能 PDF 压缩工具，支持**无损优化**和**有损渲染压缩**，并利用**多核并行处理**加速。

## 功能特点

- ✅ **无损优化** – 清理冗余数据、合并重复对象、压缩流，不损失任何画质。
- ✅ **有损渲染压缩** – 将每页渲染为 JPEG，通过分辨率（DPI）和 JPEG 质量精确控制文件大小。
- ✅ **智能参数搜索** – 先按 72 DPI 标定一次，再用实测数据按 `size ∝ scale^p` 幂律直接跳到目标附近，
  **通常 2~3 次渲染即可命中**（旧版本用固定步长试探，常常要 7 次以上）。
- ✅ **目标不可达时直接说清楚** – 例如"即使最小设置也有 32 MB，无法降到 0.01 MB"，
  而不是无意义地反复尝试。
- ✅ **两种策略** – `closest`（最接近目标，误差 ≤5%，可大于或小于）或 `smaller`（必须小于目标，误差 ≤10%）。
- ✅ **交互式手动模式** – 逐步调整参数，并给出"要命中目标建议多少 DPI"的提示。
- ✅ **高分辨率上限** – 默认最高 **600 DPI**（旧版本硬上限只有约 216 DPI），可用 `--max-dpi` 调整。
- ✅ **多核加速** – 默认按**物理核心数**分块并行（超线程反而更慢），可用 `-j` 指定进程数。
- ✅ **页面几何不变** – DPI 只决定图像清晰度，**不再改变纸张尺寸**（旧版本会把 A4 变成 A2 或半张 A4）。
- ✅ **内存护栏** – 单页渲染像素上限 40 Mpx（约 120 MB/进程），超出自动降分辨率并提示。
- ✅ **中断安全** – 按 `Ctrl+C` 保留已生成的最佳结果，清理全部临时文件，退出码 130。
- ✅ **输出编码安全** – 输出重定向到文件/管道时不会因编码问题崩溃（旧版本会）。

## 安装

```bash
pip install -r requirements.txt
```

或直接安装 PyMuPDF：

```bash
pip install pymupdf
```

## 使用方法

### 作为 Python 脚本运行

```bash
python pdf_compress.py <输入PDF路径> <目标大小(MB)> [选项]
```

### 作为独立可执行文件运行

`pdf_compress.exe` 由 Nuitka（Python → C → 机器码）编译，无需安装 Python 即可运行：

```bash
pdf_compress.exe <输入PDF路径> <目标大小(MB)> [选项]
```

所有选项与脚本版本一致，多进程加速在 exe 中同样可用。

### 按固定分辨率渲染（不必给目标大小）

```bash
python pdf_compress.py scan.pdf --dpi 150 -o ./out
```

### 选项说明

| 选项 | 说明 |
|------|------|
| `-o, --output-dir <目录>` | 输出目录（默认与输入文件同目录） |
| `--mode {auto,manual}` | `auto` 自动搜索（默认），`manual` 手动交互 |
| `--strategy {closest,smaller}` | `closest` 最接近目标（误差≤5%，默认）；`smaller` 必须小于目标（误差≤10%） |
| `--max-retries <次数>` | 自动模式最大渲染次数（默认 8，正常 3 次内命中） |
| `--dpi <数值>` | 直接按指定分辨率渲染一次（此时可省略目标大小） |
| `--max-dpi <数值>` | 自动搜索的分辨率上限（默认 600） |
| `--min-dpi <数值>` | 自动搜索的分辨率下限（默认 7.2） |
| `-j, --jobs <数值>` | 渲染进程数（默认=物理核心数，`0`=自动） |
| `--force-render` | 无损结果已小于目标时，仍强行有损渲染把文件做大 |
| `--keep-page-box` | 保留原始页面尺寸（MediaBox），裁切（CropBox）内容画回原位 |
| `--dry-run` | 只显示计划，不做实际压缩 |
| `--help-zh` | 显示中文帮助 |
| `-h, --help` | 显示英文帮助 |

### 使用示例

```bash
# 压到接近 5 MB（自动模式，最接近策略）
python pdf_compress.py mydoc.pdf 5

# 严格小于 10 MB（误差≤10%）
python pdf_compress.py mydoc.pdf 10 --strategy smaller

# 手动模式 + 指定输出目录
python pdf_compress.py mydoc.pdf 8 --mode manual -o ./output

# 只要 150 DPI，不管大小
python pdf_compress.py mydoc.pdf --dpi 150

# 提高上限到 900 DPI 去寻找更大的目标体积
python pdf_compress.py mydoc.pdf 300 --max-dpi 900

# 四进程渲染
python pdf_compress.py mydoc.pdf 20 -j 4
```

## 工作原理

1. **无损优化** – 用 `save(garbage=4, deflate=True, clean=True)` 清除无用对象、压缩流。
   优化结果先在内存里算大小，**收益不足 1% 就不再写出副本**（旧版本无条件写一份完整拷贝）。
   对纯图片型 PDF 通常收益为 0%，对未压缩的文本 PDF 可达 −94%。

2. **决策** – 如果无损优化后**已经小于目标**，工具不会默默放弃，也不会默默把文件做大，而是：
   - 明确告诉用户这个情况；
   - 说明继续有损渲染**只会让文件变大，不会提高画质或清晰度**（分辨率、细节不会增加）；
   - 交互环境会询问是否继续；非交互环境默认保留无损结果，可用 `--force-render` 明确要求强行逼近目标。

3. **渲染搜索** – 若无损结果大于目标，则进入渲染压缩：
   - 在 72 DPI 标定一次；
   - 用已测点按 `size ∝ scale^p` 在 log-log 空间插值/外推，**直接跳到目标附近**；
   - 若落在上下限边界，才改用 JPEG 质量作为副旋钮；
   - `smaller` 策略会瞄准合格区间的中心（0.95×目标），避免一直卡在"刚好超过目标一点点"的边界上；
   - 目标本身不可达时（例如比最小可做到的文件还小），直接报告可做到的范围并结束。

4. **多进程处理** – 页面均分成连续块（块数 ≤ 进程数，不会出现只有 1 页的碎块），
   每个块由独立进程渲染后按顺序合并。页数 ≤4 或 `-j 1` 时走单进程以避免进程开销。
   实测 8 页文档 4 进程相对单进程约 **1.9×** 加速；8 线程（超线程）反而比 4 线程慢。

## 输出与状态标记

所有状态行都以方括号标记开头，便于查看和脚本过滤：

| 标记 | 含义 |
|------|------|
| `[任务]` | 任务参数（输入、目标、策略、进程数） |
| `[无损]` | 无损优化阶段 |
| `[渲染]` | 渲染进度与每次尝试的参数、结果 |
| `[搜索]` | 下一次尝试的预计参数 |
| `[成功]` | 已按要求输出 |
| `[结果]` | 最终大小、误差、压缩率、渲染次数 |
| `[警告]` | 未完全满足策略，或参数被边界限制 |
| `[提示]` | 说明性信息（例如建议改用其他策略） |
| `[中断]` | 用户中断，已保留最佳结果 |
| `[失败]` | 出错，退出码 1 |

## 退出码

| 退出码 | 含义 |
|--------|------|
| `0` | 成功（含"未完全命中但已输出最接近结果"的情况） |
| `1` | 失败（文件不存在、不是 PDF、已加密、参数非法等） |
| `130` | 用户按 Ctrl+C 中断（最佳结果已保留） |

## 重要注意事项

- **有损渲染会永久丢弃原 PDF 中的文本和矢量信息** – 输出变成图片集合，最适合扫描件或图片型 PDF。
- `--strategy smaller` 的含义是"**必须**小于目标"：如果搜索只能得到 8.01 MB 而目标是 8 MB，
  它会选择更小、更保守的结果，并在 `[提示]` 中告知另一个更接近目标的结果（可用 `closest` 获得）。
- 分辨率上限默认 600 DPI。想追求更大体积/更高清晰度可用 `--max-dpi` 提高，
  但单页像素超过 40 Mpx 后会被自动压制（保护内存）。
- 默认输出页面以**裁切框（CropBox）**为准（不带扫描边距）。若要逐页保持原始纸张尺寸，
  加 `--keep-page-box`。
- 输出文件默认命名为 `<原文件名>_compressed.pdf`；临时文件集中在一个隐藏的临时目录里，
  正常结束/中断都会自动清理。

## 从源码构建 exe（Nuitka）

项目里带了 `build_exe.bat`，直接运行即可（自动编译并覆盖旧 exe，约 3 分钟）：

```bat
build_exe.bat
```

需要 C 编译器：**MSVC 14.3+**（Visual Studio 2022 Build Tools 或更新版本）。
注意 Nuitka 在 Python 3.13 及以上**不能用 MinGW**，只能用 MSVC。

它做的关键事情（脚本注释里有完整说明，都是实测踩出来的）：

```bat
set VSLANG=1033                                  :: 见下面第 1 条
python -m nuitka --standalone --onefile --msvc=latest --low-memory --lto=no ^
    --nofollow-import-to=pymupdf ^
    --no-deployment-flag=excluded-module-usage ^
    --include-data-files="<site-packages>\pymupdf\*.py=pymupdf/" ^
    --include-data-files="<site-packages>\pymupdf\*.pyd=pymupdf/" ^
    --include-data-files="<site-packages>\pymupdf\*.dll=pymupdf/" ^
    --include-data-files="%SystemRoot%\System32\msvcp140.dll=msvcp140.dll" ^
    --include-data-files="<python 安装目录>\python3.dll=python3.dll" ^
    --windows-console-mode=force --output-filename=pdf_compress.exe pdf_compress.py
```

**为什么不让 Nuitka 直接编译 PyMuPDF**（这些东西不说清楚，下次构建一定会再踩一遍）：

1. **中文版 Visual Studio 会让构建直接失败**：Nuitka 的 Scons 后端用 `mbcs` 代码页解码 `cl.exe` 的输出，
   遇到中文版 MSVC 会抛 `UnicodeDecodeError: 'mbcs' codec can't decode bytes`。`VSLANG=1033` 强制英文消息即可。
2. **PyMuPDF 太大**：`pymupdf/mupdf.py`（5065 个函数 + 547 个类）会被展开成
   **122 MB / 235 万行**的单个 C 文件。开链接期优化时 MSVC 报
   `fatal error C1002: 编译器的堆空间不足` 并导致 `LNK1257`。
3. **关掉 LTO 也不够**：Nuitka 优化器会在该模块上来回震荡约 20 分钟，之后 MSVC 仍要吞下这个巨型翻译单元。
4. 所以这里改成：**只把本项目的代码编译成 C，PyMuPDF 原样随包分发**
   （`--nofollow-import-to` + 显式把 `.py/.pyd/.dll` 作为数据文件打包），
   构建时间从约 50 分钟降到 **约 3 分钟**，功能完全一致。
5. PyMuPDF 的 `_mupdf.pyd` / `_extra.pyd` / `mupdfcpp64.dll` 依赖 **MSVCP140.dll**（MSVC C++ 运行时）
   和 **python3.dll**（CPython 稳定 ABI 转发层）；Nuitka 只自带 vcruntime140，必须显式打包这两个，
   否则 exe 会报 `ImportError: DLL load failed while importing _extra`。

**exe 已验证**（21 项检查全过）：8 页文档多进程压缩正常（旧 PyInstaller 版 exe 在这里会
`BrokenProcessPool` 崩溃）、4 页走单进程、中文帮助、输出重定向、`--force-render`、中断都正常；
并且把系统里已安装的 `pymupdf` 改名移走后，exe 仍能独立完成压缩（确认真正自包含）。

## 更新记录（v2.0）

- 修复：输出重定向（批处理/计划任务/CI）时 emoji 触发 `UnicodeEncodeError`，
  旧版本会把"已经成功"的压缩报成失败，某些路径下还会删光所有临时文件。
- 修复：参数搜索只会在"无损结果小于目标"时直接放弃并谎称"无法达到"。
- 修复：手动模式输入无效字符会**整份文档重新渲染**，且尝试编号错乱。
- 修复：`scale` 会同时改变页面物理尺寸（A4 → A2 或半张 A4），现在只影响 DPI。
- 修复：多进程按逻辑核数启动，超线程反而更慢；现在按物理核心数。
- 修复：中断退出码为 0（现在 130）；无损阶段无条件写整份副本（现在收益<1% 就不写）。
- 新增：解析上限 216 → 600 DPI（`--max-dpi`）、`--dpi` 固定分辨率渲染、`-j/--jobs`、
  `--force-render`、`--keep-page-box`、单页像素护栏、渲染进度输出、友好错误提示。
- 变更：exe 改用 Nuitka 编译（Python → C → 机器码），修复了旧 exe 处理 >4 页 PDF 时
  多进程崩溃（`BrokenProcessPool`）的问题。

## 许可证

MIT
