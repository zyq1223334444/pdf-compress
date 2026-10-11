> [🇬🇧 English](readme.md)

# PDF Compressor（PDF 压缩工具）

一款用 **C++20** 编写、用 **CMake** 构建、界面由 **Win32 + Direct2D** 手绘的 PDF 压缩工具。
支持**无损优化**与**有损渲染压缩**，并用**多进程并行**加速。

它的重点是：你只需要说"想要多大"，而不是自己去拧参数——给出目标体积（MB），它来搜索能落在那个大小的参数。

不带参数运行就是**图形界面**；在命令行上直接给 PDF 路径也会打开界面并把这些文件放进列表。
命令行的用法保持不变，两种方式背后是同一个压缩引擎。

## 与 Python 版的关系

本仓库是 **C++ 重写版**，C++ 构建才是**全功能版本**。原来的 Python 实现已经不在这个树里了：
它是个原型，把它换掉的那个提交（"feat!: replace the Python implementation with the C++ rewrite"）
的上一个版本里还完整留着它，想翻的话：

    git show <那个提交>^:pdfcomp/core.py

沿用下来的东西：

- 一个原生可执行文件（`pdf_compress.exe`）——不需要 Python 运行时、不需要安装解释器、
  每次启动也不需要自解压（MuPDF 本身是 DLL 构建时，它的 DLL 放在 exe 旁边，而不是塞进 exe 里）；
- 同一条压缩流程，按 `pdfcomp/core.py` 逐段移植（`src/search.cpp` 里的搜索算法是逐行翻译，
  连 Python 的"四舍六入五成双"取整语义都照抄）；
- 图形界面改为原生 Win32 + Direct2D，不再依赖 Tkinter，也就完全不需要任何界面库；
- 右键菜单组件被嵌进可执行文件里，因此便携版自己就能安装右键菜单。

## 功能特点

- ✅ **无损优化** – 清理无用对象、净化并压缩流，不损失任何画质。优化结果先在内存里量体积，
  收益不足 1% 就完全不写出副本。
- ✅ **有损渲染压缩** – 把每页渲染成 JPEG，通过分辨率（DPI）与 JPEG 质量精确控制文件大小。
- ✅ **智能参数搜索** – 先按 72 DPI 标定一次，再用实测数据按 `size ∝ scale^p` 幂律直接跳到目标附近，
  **通常 2~3 次渲染即可命中**。
- ✅ **目标不可达时直接说清楚** – 直接报告实际能做到的范围，而不是无意义地反复尝试。
- ✅ **两种策略** – `closest`（最接近目标，误差 ≤5%，可大于或小于）或 `smaller`（必须小于目标，误差 ≤10%）。
- ✅ **交互式手动模式** – 逐步调整参数，并给出"要命中目标建议多少 DPI"的提示。
- ✅ **高分辨率上限** – 默认最高 **600 DPI**，可用 `--max-dpi` 调整。
- ✅ **多进程加速** – 默认按**物理核心数**分块并行（实测超线程反而更慢），可用 `-j` 指定进程数。
- ✅ **页面几何不变** – DPI 只改变图像像素数，不改变页面物理尺寸。
- ✅ **内存护栏** – 单页渲染像素上限 40 Mpx（约 120 MB/进程），超出会自动降低该页分辨率并提示。
- ✅ **中断安全** – 按 `Ctrl+C` 会保留已生成的最佳结果、清理全部临时文件，退出码 130。
- ✅ **原生图形界面** – 不带参数运行即可：Windows 11 风格、亮/暗主题、系统原生文件对话框、
  可排队压缩多个 PDF，所有命令行选项在界面上都有。
- ✅ **Windows 11 一级右键菜单** – `.pdf` 的右键菜单里直接多出"用 PDF Compressor 压缩"，
  不用点"显示更多选项"。
- ✅ **语言跟随安装时的选择** – 安装向导里选的语言就是命令行输出、帮助、图形界面和开始菜单快捷方式的默认语言。

## 依赖要求

| 项目 | 要求 |
|---|---|
| PDF 引擎 | **MuPDF** —— 必需，不随仓库分发，也没有可以内置它的包 |
| 编译器（Windows） | **MSVC 2022**（Visual Studio 2022 或 Build Tools），需要"使用 C++ 的桌面开发"工作负载 |
| 编译器（Linux/macOS） | 支持 C++20 的 **GCC** 或 **Clang** |
| 构建系统 | **CMake ≥ 3.16** |
| 语言标准 | **C++20**（由 `CMakeLists.txt` 设定） |
| 安装包（可选） | **Inno Setup 6.5+**（实测 6.7.3） |
| 右键菜单（可选，仅 Windows） | Windows 10/11 SDK（`makeappx.exe`、`signtool.exe`）+ Python（只给 `shell/` 里生成载荷和磁贴图的辅助脚本用） |

### 获取 MuPDF

CMake 按下面的顺序查找 MuPDF：`-DMUPDF_ROOT=<前缀>` → 环境变量 `MUPDF_ROOT` → 系统路径 /
`pkg-config` → 仓库内的 `third_party/mupdf/`。

| 平台 | 命令 |
|---|---|
| Windows | `build\build_mupdf.bat` → 编译到 `third_party\mupdf`（装了 vcpkg 就用 vcpkg，否则从 mupdf.com 下载源码包，用 MSBuild 编译 `platform\win32\libmupdf.vcxproj`） |
| Debian/Ubuntu | `sudo apt-get install -y build-essential cmake libmupdf-dev` |
| Fedora | `sudo dnf install -y gcc-c++ cmake mupdf-devel` |
| Arch | `sudo pacman -S --needed base-devel cmake mupdf` |
| macOS | `brew install cmake mupdf`（另需 Xcode 命令行工具） |
| 自己编译 | 配置时传 `-DMUPDF_ROOT=<前缀>`（前缀里要有 `include/` 与 `lib/`） |

`build\build_mupdf.bat` 默认编译 MuPDF 1.26.0，也可以带一个版本号参数
（`build\build_mupdf.bat 1.24.9`），并且可以反复运行：`third_party\mupdf` 里已经有可用的
构建结果时它什么都不做。

## 构建

### Windows：一条命令

```bat
build\build_exe.bat
```

产出：

| 产物 | 说明 |
|---|---|
| `build\out\pdf_compress.exe` | 程序本体 |
| `build\out\pdf-compress.exe` | 同一个文件，用 CMake 目标名再放一份 |
| `build\out\shell\*.msix`、`build\out\shell\*.cer` | 右键菜单包（如果那一步跑过） |
| `build\out\cmake\` | CMake 目录树（中间产物） |

顺序是有讲究的：右键菜单包**先**编译，因为它会生成 `src\shell_payload.cpp`，而可执行文件要把这个文件
嵌进去。这一步失败只是警告——你得到一个 `--install-menu` 会说"这份构建没有带右键菜单"的可执行文件。
脚本写出的东西全部落在 `build\out\` 里，该目录不进仓库。

### Windows：手动 CMake

```bat
rem 1. 先准备 MuPDF（每台机器一次；用 vcpkg 或系统前缀的话可以跳过）
build\build_mupdf.bat

rem 2. 配置
cmake -S . -B build\out\cmake -G "Visual Studio 17 2022" -A x64 -DMUPDF_ROOT=%CD%\third_party\mupdf

rem 3. 编译
cmake --build build\out\cmake --config Release --parallel
```

结果是 `build\out\cmake\Release\pdf_compress.exe`（CMake 目标叫 `pdf-compress`，靠 `OUTPUT_NAME`
把二进制重命名为 `pdf_compress.exe`——MSIX 清单、开始菜单快捷方式和文档都按这个名字找它）。
在 Windows 上可执行文件按 GUI 子系统链接：双击不会多弹一个控制台黑窗，从终端启动时它会附加到父控制台。

### Windows：打三个发布版

```bat
build\package.bat
```

它把 `build_exe.bat` 的产物打包装好（先跑那一个；安装包由 `build_installer.bat` 出，
`package.bat` 在发现它缺失时会替你跑）。产物全部落在 `dist\windows\`：

| 产物 | 说明 |
|---|---|
| `pdf_compress_2.1.0_win64.exe` | 单文件版——整个程序就是一个可执行文件 |
| `pdf-compress-2.1.0-win64\` | 文件夹版：同一个可执行文件，加上中英文说明、许可证和右键菜单包 |
| `pdf-compress-2.1.0-win64.zip` | 文件夹版，已打包 |
| `pdf_compress_setup_2.1.0.exe` | 安装包 |

单文件版是真的单文件：MuPDF 静态链接，右键菜单包嵌在 exe 里，拷到哪都能直接跑。`dist\`
不进仓库——发布产物不该混进源码。

### Linux / macOS

```bash
bash build/build_unix.sh
```

脚本会自己算出仓库根目录，因此在任何工作目录下都能直接运行。CMake 的中间产物全部留在
`build/out/cmake_unix/`，发布文件夹写到 `dist/<平台>/pdf-compress_2.1.0_<os>_<arch>/`：

| 产物 | 说明 |
|---|---|
| `pdf_compress` | 可执行文件 |
| `readme.md`、`readme.zh.md`、`LICENSE` | 和 Windows 文件夹版同样带的文档 |
| `libmupdf.so*` | 只在 MuPDF 是共享库时才出现；`/lib`、`/usr/lib` 里的系统库不会被打进去 |

不做压缩包：Linux 和 macOS 上发布件本来就是一个文件夹。

MuPDF 不在系统默认路径时，用环境变量 `MUPDF_ROOT` 指过去。脚本最后会做一次冒烟测试
（`--help` 必须能跑起来并打印内容）。CI 在真机上构建这些二进制——见 `.github/workflows/build.yml`，
它就是在 Ubuntu 和两个 macOS 架构上跑这个脚本，并在打 tag 时把结果挂到 Release 上。

> **Linux/macOS 没有图形界面**：那些构建只有命令行工具。窗口是 Win32 + Direct2D，
> 其他平台上由 `src/gui/gui_stub.cpp` 顶替。

### 只做语法检查

```bat
build\check_compile.bat
build\check_compile.bat src\engine.cpp src\search.cpp
```

链接真正的程序需要 MuPDF 库，但检查 API 用法只需要头文件（`tools\fetch_mupdf.py` 会把它们下载到
`third_party\mupdf\include`）。这个脚本用 `cl /c /std:c++20 /W4 /permissive-` 检查全部源文件
（不给参数时）或你指定的文件，从而在依赖栈齐备之前就能暴露语法、类型和 API 签名错误。

### 单元测试

`CMakeLists.txt` 会编译两个测试可执行文件——`pdfc_search_tests`（搜索算法，完全不需要 MuPDF）和
`pdfc_i18n_tests`（文案表）——并用 `add_test` 注册成 `search` 与 `i18n`：

```bat
cmake --build build\out\cmake --config Release
ctest --test-dir build\out\cmake -C Release
```

```bash
cmake --build build/out/cmake_unix --config Release
ctest --test-dir build/out/cmake_unix
```

`tools\check_i18n.py` 是漏翻检查：它把源码里所有 `tr("...")` 字面量抠出来和英文表逐条比对，
所以翻译不会悄悄烂掉。

### 安装包

```bat
build\build_exe.bat
build\build_installer.bat
```

`build_installer.bat` 先把载荷整理到 `build\out\pdf_compress_win64`（exe、MuPDF 的 DLL、右键菜单包
及其证书），再编译 `build\installer.iss`，产出
`build\out\dist_installer\pdf_compress_setup_2.1.0.exe`。需要 Inno Setup 6.5+，
可用 `winget install --id JRSoftware.InnoSetup -e` 安装。`build\package.bat` 会替你跑它，
并把结果复制进 `dist\windows\`。

### 单独构建右键菜单包

```bat
shell\build_shell_menu.bat
```

正常情况不需要手动跑它：`build_exe.bat` 会先调用它。它编译 `ExplorerCommand.dll`
（实现 `IExplorerCommand` 的进程内 COM 服务器）、在构建时现生成一张自签名证书、打包并签名
`pdf_compress_shell.msix`，最后把这两个二进制以十六进制字符串写进 `src\shell_payload.cpp`，
以便嵌进可执行文件。

## 使用方法

### 图形界面（Windows）

```bat
pdf_compress.exe
pdf_compress.exe report.pdf
pdf_compress.exe report.pdf invoice.pdf
pdf_compress.exe --gui
```

- **文件卡片**："添加 PDF 文件"打开操作系统自己的文件对话框，过滤条件是 `*.pdf`，可一次选多个；
  "移除所选"和"清空"管理列表；双击列表项会用资源管理器打开它所在的文件夹。同一份文件不会重复入列。
- 文件会一个接一个压缩，列表里每一行显示自己的状态与结果（等待 → 压缩中 → 完成并显示大小；失败；已取消）。
- **压缩设置卡片**——命令行有的选项这里都有：目标大小（MB）、固定 DPI（可留空）、输出目录
  （带目录选择按钮）、模式（自动搜索 / 手动调整）、策略（最接近目标 / 必须小于目标）、最大渲染次数、
  渲染进程数、搜索上限与下限（DPI），以及"强制有损渲染（即使文件会变大）""保留原始页面尺寸"
  "只预览计划，不实际压缩"三个勾选框。
- **进度与日志卡片**：进度条、实时日志和一行状态（*就绪*、*正在渲染 3/12 页*、*正在取消…*、
  *完成：成功 2 个，失败 1 个*）。**取消**按钮会在当前这一块渲染结束后停下，并保留已经得到的最佳结果。
- 命令行本来要在 stdin 上问的问题（例如"无损结果已经比目标小了，还要强行渲染吗？"，或手动模式的
  "下一步怎么做？"）在图形界面里变成界面上的一条询问行加按钮，不弹模态对话框。
- 标题栏有**中文 / English** 按钮和亮/暗主题开关，两个选择都会被记住。
- 右键菜单的安装/卸载**故意不放进界面**，只能在命令行做。

### 界面语言

语言按下面的顺序决定，前面的优先：

1. 命令行的 `--lang`（只影响本次运行，不写入设置）；
2. 在图形界面里选过的语言，存在 `%LOCALAPPDATA%\pdf-compress\settings.ini`；
3. 注册表里记录的语言——便携版执行 `--install-menu` 时写的是
   `HKCU\Software\pdf-compress\Language`，否则是安装包写进
   `HKLM\SOFTWARE\pdf-compress\Language` 的那个（就是安装向导里选的）；
4. 系统区域设置（`zh*` 用中文，其余用英文）。

- `-h` / `--help` 跟随当前设置的语言；`--help-cn` 强制中文（`--help-zh` 同义）、`--help-en` 强制英文。
- 图形界面里随时可以切换语言；选择会写进 `settings.ini`，优先级高于注册表里的值。

### 命令行

```bat
pdf_compress.exe <输入PDF路径> <目标大小(MB)> [选项]
pdf_compress.exe <输入PDF路径> --dpi 150 [选项]
```

```bash
./pdf_compress <输入PDF路径> <目标大小(MB)> [选项]
./pdf_compress <输入PDF路径> --dpi 150 [选项]
```

每个选项的值既可以写成 `--opt value`，也可以写成 `--opt=value`。无法识别的参数会被当作错误处理，
程序提示你用 `--help` 后以退出码 2 结束。

#### 选项说明

| 选项 | 说明 |
|------|------|
| `-o, --output-dir <目录>` | 输出目录（默认与输入文件同目录） |
| `--mode {auto,manual}` | `auto`=自动搜索（默认），`manual`=手动逐步调整 |
| `--strategy {closest,smaller}` | `closest` 最接近目标（误差≤5%，默认）；`smaller` 必须小于目标（误差≤10%） |
| `--max-retries <次数>` | 自动模式最大渲染次数（默认 8，正常 3 次内命中） |
| `--dpi <数值>` | 直接按指定分辨率渲染一次（此时可省略目标大小） |
| `--max-dpi <数值>` | 自动搜索的分辨率上限（默认 600） |
| `--min-dpi <数值>` | 自动搜索的分辨率下限（默认 7.2） |
| `-j, --jobs <数值>` | 渲染进程数（默认=物理核心数，`0` 表示自动） |
| `--force-render` | 无损结果已小于目标时，也强制有损渲染把文件做大（只是变大，不会更好） |
| `--keep-page-box` | 保留原始页面尺寸（MediaBox），不按裁切框输出 |
| `--dry-run` | 只显示计划，不做实际压缩 |
| `--gui` | 强制打开图形界面 |
| `--lang {zh,en}` | 只对本次运行指定语言（不写入设置） |
| `-h, --help` | 显示帮助，语言跟随当前设置 |
| `--help-cn` | 显示中文帮助（`--help-zh` 同义） |
| `--help-en` | 显示英文帮助 |
| `--install-menu` | 添加资源管理器右键菜单（仅 Windows，需要管理员权限，只对 `.pdf` 生效） |
| `--uninstall-menu` | 移除资源管理器右键菜单（仅 Windows） |
| `-V, --version` | 显示版本号 |

解析器里还有两个不属于公开接口的参数：`--quiet` 会压掉 `--install-menu` / `--uninstall-menu` 的进度输出
（安装包调用的就是 `--install-menu --quiet`），`--worker` 用于启动内部渲染子进程，不需要也不应该手动敲。

### 策略

| 策略 | 合格区间 | 行为 |
|---|---|---|
| `closest`（默认） | `[0.95 × 目标, 1.05 × 目标]` | 结果可以大于也可以小于目标；搜索直接瞄准目标 |
| `smaller` | `[0.90 × 目标, 目标)` —— 上界是开区间 | 结果必须严格小于目标。搜索瞄准区间中心（0.95 × 目标），避免一直卡在"刚好超过目标一点点"的地方 |

**没有第三种策略**：`--strategy` 只接受 `closest` 和 `smaller`。`larger` 和 `smaller` 这两个词也出现在
*手动模式*里，但那里它们是"提高分辨率/质量"和"降低分辨率/质量"这两个下一步指令，不是策略。

用 `smaller` 时，如果最佳结果仍大于目标，程序会输出仍小于目标且最接近它的那个结果，
并提示另一个更接近目标、但只有 `closest` 才会接受的结果。

### 使用示例

```bat
rem 压到接近 5 MB（自动模式，最接近策略）
pdf_compress.exe mydoc.pdf 5

rem 严格小于 10 MB（误差≤10%）
pdf_compress.exe mydoc.pdf 10 --strategy smaller

rem 手动模式 + 指定输出目录
pdf_compress.exe mydoc.pdf 8 --mode manual -o .\output

rem 只要 150 DPI，不管大小
pdf_compress.exe mydoc.pdf --dpi 150

rem 提高上限到 900 DPI 去寻找更大的目标体积
pdf_compress.exe mydoc.pdf 300 --max-dpi 900

rem 四进程渲染
pdf_compress.exe mydoc.pdf 20 -j 4

rem 只显示计划，不写出任何文件
pdf_compress.exe mydoc.pdf 2 --dry-run

rem 打开图形界面（等同于不带参数运行）
pdf_compress.exe

rem 强制用英文帮助
pdf_compress.exe --help-en

rem 安装 / 卸载 Windows 11 右键菜单
pdf_compress.exe --install-menu
pdf_compress.exe --uninstall-menu
```

Linux/macOS 的等价命令（选项相同，把 `pdf_compress.exe` 换成 `./pdf_compress`）：

```bash
./pdf_compress mydoc.pdf 5
./pdf_compress mydoc.pdf 10 --strategy smaller
./pdf_compress mydoc.pdf 8 --mode manual -o ./output
./pdf_compress mydoc.pdf --dpi 150
./pdf_compress mydoc.pdf 300 --max-dpi 900
./pdf_compress mydoc.pdf 20 -j 4
./pdf_compress mydoc.pdf 2 --dry-run
./pdf_compress --help-en
```

### 退出码

| 退出码 | 含义 |
|--------|------|
| `0` | 成功（含"未完全命中但已输出最接近结果"的情况，以及 `--dry-run`） |
| `1` | 失败（文件不存在、不是 PDF、已加密、写不出输出、没有可用结果等） |
| `2` | 参数错误——无法识别的选项、选项缺少取值或取值非法，以及既没给目标大小也没给 `--dpi` |
| `130` | 用户按 Ctrl+C 中断（已保留当时的最佳结果） |

### 输出与状态标记

所有状态行都以方括号标记开头，便于查看和脚本过滤；图形界面里的日志窗显示的是同一套标记。

| 标记 | 含义 |
|------|------|
| `[任务]` | 任务参数（输入、目标、策略、进程数） |
| `[无损]` | 无损优化阶段 |
| `[渲染]` | 渲染进度与每次尝试的参数、结果 |
| `[搜索]` | 下一次尝试的预计参数 |
| `[成功]` | 已按要求输出 |
| `[结果]` | 最终大小、误差、压缩率、渲染次数 |
| `[警告]` | 未完全满足策略，或参数被边界限制 |
| `[提示]` | 说明性信息 |
| `[中断]` | 用户中断，已保留最佳结果 |
| `[失败]` | 出错，退出码 1 |

输出文件名为 `<原文件名>_compressed.pdf`，写在输出目录里（默认与输入文件同目录）。

## Windows 11 资源管理器右键菜单（仅 Windows，仅 `.pdf`）

这是一个**可选**功能，而且由它自己的命令驱动——它故意不出现在图形界面里：

```bat
pdf_compress.exe --install-menu
pdf_compress.exe --uninstall-menu
```

安装包里有一个**默认勾选**的对应任务。

Windows 11 的**一级**右键菜单（不用点"显示更多选项"的那个）只认一种注册方式：把一个实现了
`IExplorerCommand` 的原生 COM DLL 放进 **MSIX 稀疏包（sparse package）**，再用
`windows.fileExplorerContextMenus` 声明它对 `.pdf` 生效。源码在 `shell/`：

| 文件 | 作用 |
|---|---|
| `shell/ExplorerCommand.cpp` | COM 服务器，实现 `IExplorerCommand` |
| `shell/AppxManifest.xml` | 稀疏包清单，注册 COM 代理服务器与 `.pdf` 右键菜单 |
| `shell/build_shell_menu.bat` | 用 MSVC 编 DLL，用 `makeappx` / `signtool` 打包并签名 `.msix` |
| `shell/make_assets.py`、`shell/Resources/` | 包的磁贴图片 |
| `shell/make_payload.py` | 把 `.msix` 与 `.cer` 转成 `src/shell_payload.cpp` |

点这一项的效果是：打开图形界面，并载入你右键点中的那个文件；一次选中多个文件会全部带进去。
另外还注册了传统的 `HKCU\Software\Classes\SystemFileAssociations\.pdf\shell` 项，
让 Windows 10 和"显示更多选项"子菜单照旧可用。

- 第一次安装需要管理员权限，**只是为了把证书装进受信任存储**，程序会自己请求提权；装过一次之后再执行
  就不需要了。安装包装好的机器上证书已经就位，所以它调用 `--install-menu --quiet` 时不会再弹 UAC。
- 构建时会在本地**现生成一张自签名证书**（`CN=zyq1223334444`，有效期 10 年，不提交进仓库）；
  安装时只把它的**公钥**导入 `Cert:\LocalMachine\TrustedPeople`，卸载时再删掉。这里要说清楚：
  **这是一处与安全相关的改动**（往机器级信任库里加了一张证书），这是出现在一级菜单里的必要代价。
- 包还会被**嵌进可执行文件里**，载体是构建时生成的 `src\shell_payload.cpp`，所以便携版自己就能装菜单，
  不需要在 exe 旁边放任何额外文件；如果 `pdf_compress_shell.msix` 和 `pdf_compress_shell.cer`
  恰好就在 exe 旁边，则优先用它们。
- 在 Linux/macOS 上整个功能不可用：`--install-menu` 会报告右键菜单只有 Windows 支持。
- 如果菜单没有立刻出现，重启资源管理器或注销再登录一次——shell 会缓存动词。

## 安装包

```bat
build\build_installer.bat
```

下载并双击 `pdf_compress_setup_2.1.0.exe`，或者用上面的命令自己编译：

- 装到 `C:\Program Files\pdf-compress`，带开始菜单快捷方式（其中一项是"命令行帮助"，执行
  `pdf_compress.exe --help`）和**卸载程序**（"设置 → 应用"里也能卸载）；
- 安装向导第一页选的**语言（中文 / English）同时决定程序默认语言**：它被写进
  `HKLM\SOFTWARE\pdf-compress\Language`，命令行输出、`-h`/`--help`、`--help-cn`/`--help-en`
  和图形界面都用它；
- 可选把安装目录加入系统 `PATH`（该任务**默认勾选**），之后任意终端直接敲 `pdf_compress`；
  卸载时会把 `PATH` **逐字节还原**——安装前先把原值备份进注册表；
- "用 PDF Compressor 压缩"右键菜单任务**默认勾选**；安装程序先把证书导入受信任人证书库，
  再让程序按当前用户注册稀疏包；
- 桌面快捷方式任务存在，但**默认不勾选**；
- 卸载时会一并移除右键菜单（含那张证书）、安装目录在 `PATH` 里的条目、开始菜单项，
  并删除 `HKLM\SOFTWARE\pdf-compress`。

## 配置

三份数据，来源和可写性都不同：

| 位置 | 谁写 | 存了什么 |
|---|---|---|
| `%LOCALAPPDATA%\pdf-compress\settings.ini` | 图形界面（也可以自己手改） | 语言、主题和上次填的表单值 |
| `HKLM\SOFTWARE\pdf-compress` | 安装包；谁都能读，只有提权才能写 | `Language`、`InstallDir`、`Version`，以及 `OriginalPath`（`PATH` 的备份） |
| `HKCU\Software\pdf-compress` | 便携版执行 `--install-menu` 时写 | `InstallDir`、`Language` |

Linux/macOS 上的设置文件是 `$XDG_CONFIG_HOME/pdf-compress/settings.ini`，没有该变量时用
`~/.config/pdf-compress/settings.ini`（连 `HOME` 都没有时退到 `$TMPDIR`/`/tmp` 下的固定目录）。
非 Windows 平台没有注册表。

`settings.ini` 是极简的 `key=value` 文件（UTF-8、LF、`#` 开头是注释），写入是原子的——先写临时文件
再改名，所以断电或崩溃不会留下半个设置文件。删掉它就会回到出厂状态（语言重新按系统区域猜）；
文件坏了当空的处理：设置读不出来绝不会变成启动失败。

| 键 | 含义 |
|---|---|
| `language` | `zh` 或 `en` |
| `theme` | `light` 或 `dark` |
| `target_size` | 界面上最后使用的目标大小（MB） |
| `dpi` | 最后使用的固定 DPI |
| `output_dir` | 最后的输出目录 |
| `mode` | `auto` 或 `manual` |
| `strategy` | `closest` 或 `smaller` |
| `max_retries` | 最大渲染次数 |
| `max_dpi`、`min_dpi` | 搜索上限与下限 |
| `jobs` | 渲染进程数（`0` 表示自动） |
| `force_render`、`keep_page_box`、`dry_run` | 三个勾选框的 `1` / `0` |

优先级：

- **语言**：`--lang` → `settings.ini` → 注册表（先 `HKCU`，再 `HKLM`）→ 系统区域设置。
- **其他设置**：命令行选项永远优先。`settings.ini` 只提供图形界面的初始值，
  不会改变命令行的默认值。

## 工作原理

1. **无损优化** – 用 MuPDF 的文档写出器做垃圾回收、流压缩、净化和清理，等价于 PyMuPDF 的
   `save(garbage=4, deflate=True, clean=True)`。结果先在内存里量体积，**收益不足 1% 就不写出副本**
   （原文件直接作为无损候选）。对纯图片型 PDF 通常收益为 0%，对未压缩的文本 PDF 可以缩小很多。

2. **决策** – 如果无损优化后**已经小于目标**，工具不会默默放弃，也不会默默把文件做大，而是：
   - 明确告诉用户这个情况；
   - 说明继续有损渲染**只会让文件变大，不会提高画质或清晰度**（分辨率、细节不会增加）；
   - 交互环境会询问是否继续；非交互环境默认保留无损结果，可用 `--force-render` 明确要求强行逼近目标。
     （图形界面里同一个问题显示为界面上的一条询问行，不是弹窗。）

3. **渲染搜索** – 若无损结果大于目标，则进入渲染压缩：
   - 在 72 DPI 标定一次；
   - 用已测点按 `size ∝ scale^p` 在 log-log 空间插值/外推，**直接跳到目标附近**；
   - 只有当 scale 顶到上下限时，才改用 JPEG 质量（50~100，默认 85）作为副旋钮；
   - `smaller` 策略瞄准合格区间的中心（0.95×目标），避免一直卡在"刚好超过目标一点点"的边界上；
   - 目标本身不可达时，直接报告可做到的范围并提前结束。

4. **多进程渲染** – 页面均分成连续块（块数不超过进程数，不会出现只有 1 页的碎块），
   由 `--worker` 子进程并行渲染后按顺序合并。页数 ≤4 或 `-j 1` 时走单进程路径。

为什么输出不会比目标大太多：

- `closest` 只接受 ±5%，`smaller` 更是绝不超过目标，合格区间本身就很窄；
- 搜索瞄准区间的**中心**而不是边界；
- `size ∝ scale^p` 的插值用的是**这份文档自己**的实测点，且实测斜率被夹在 1.2~3.0，
  一次离谱的测量不会把下一次猜测带偏；
- 目标确实不可达时会立刻停下并报告可做到的范围，而不是继续浪费尝试次数；
- 尝试次数用尽时，仍然会输出与目标最接近的那一次结果。

## 常见问题

**CMake 说找不到 MuPDF。**
找不到头文件或库时 CMake 会停下并给出可读的提示。解决办法任选其一：Windows 上跑
`build\build_mupdf.bat`；Linux 上装发行版包（`sudo apt-get install -y libmupdf-dev`），
macOS 上用 Homebrew（`brew install mupdf`）；或者用 `-DMUPDF_ROOT=<前缀>` 指向自己编译的 MuPDF
（前缀里必须有 `include/mupdf/fitz.h` 和库文件）。`build_exe.bat` 的错误分支里也会提示同一条命令。
注意：`third_party\mupdf` 里只有头文件说明那是一份不完整的拷贝——重跑那个脚本。

**右键菜单不出现。**
它是可选功能，而且由单独的命令安装：先执行 `pdf_compress.exe --install-menu`。然后：

- 重启资源管理器，或者注销再登录一次——shell 会缓存动词；
- 首次安装需要管理员权限把自签名证书放进"受信任人"证书库；没有权限时注册会失败并明确说明。
  用安装包安装则不会弹 UAC，因为安装程序已经先把证书信任好了；
- 确认你运行的这个 exe 真的带了右键菜单组件。没跑过 `shell\build_shell_menu.bat`
  （或那一步失败）的构建会直接说"这份构建没有带右键菜单"；
- Windows 10（1809 之前）不支持稀疏包；随它一起注册的传统 `HKCU` 项仍然可用，
  在"显示更多选项"子菜单里能找到。

**杀毒软件误报。**
可执行文件是本机用 MSVC 编译出来的，**没有代码签名**（只有 MSIX 右键菜单包做了签名，而且用的是自签名证书），
所以启发式扫描器可能把一个刚编译出来、它从没见过的二进制报出来。从扫描器的角度看这是误报：
程序就是一个普通的 Win32 应用，渲染 PDF 页面然后写出一个新文件，它做的每件事在 `src/` 里都看得到。
如果你不想凭信任接受它，就用这份源码自己编译一份（`build\build_exe.bat`）再对比，或者给安装目录加白名单。
另外要清楚它唯一一处与安全相关的动作：`--install-menu` 会往机器级"受信任人"证书库加一张自签名证书，
这正是安全软件会特别关注的那类改动。

**同时给出 `--dpi` 和目标大小会怎样？**
固定分辨率优先：工具会按那个 DPI **渲染一次**，提示一句"已忽略目标大小"，然后直接输出，
完全不会为目标大小做搜索。

**输出文件在哪？**
在输出目录里（`-o` / `--output-dir`，或图形界面里的输出目录），名字是 `<原文件名>_compressed.pdf`；
没给输出目录时就落在输入文件旁边。临时文件放在输出目录下按次创建的隐藏目录（`.pdf_compress_*`）里，
正常结束、失败和中断都会清理掉。

**按 `Ctrl+C` 会保留什么？**
会把当时的最佳结果写到正常的输出路径，清理全部临时文件，退出码为 130。

**我的系统上没有图形界面。**
窗口是 Win32 + Direct2D，所以只有 Windows 上有。Linux/macOS 的构建就是命令行工具；
`--gui` 会说明这个构建没有图形界面并请你改用命令行。

## 项目结构

构建**产出**的东西全部集中在一个被忽略的目录里，所以 clone 下来永远是干净的：

```
CMakeLists.txt            CMake 3.16+、C++20、MuPDF 查找、目标与测试
readme.md / readme.zh.md  本文档
LICENSE                   MIT

src/                      程序本体
├── main.cpp              入口（Windows 是 wWinMain，其他地方是 main）
├── cli.cpp / cli.hpp     参数解析、帮助文本、入口分发
├── options.cpp / .hpp    JobOptions、校验、进程数
├── engine.cpp / .hpp     压缩流程编排（完全不认识界面）
├── search.cpp / .hpp     目标大小搜索（纯数学，有单元测试）
├── pdf.cpp / .hpp        MuPDF 封装：探测、无损写出、渲染、合并
├── worker.cpp / .hpp     --worker 渲染子进程
├── process.cpp / .hpp    子进程管理
├── config.cpp / .hpp     settings.ini、注册表、语言选择
├── i18n.cpp / .hpp       语言切换；中文是源语言
├── strings_en.cpp        英文翻译表
├── strings_en_extra.cpp  生成器之外的英文条目
├── filedialog.cpp / .hpp 各平台的原生文件/目录对话框
├── shellmenu.cpp / .hpp  资源管理器右键菜单的安装与卸载
├── shell_payload.hpp / shell_payload_default.cpp   内嵌的 MSIX 载荷
├── platform_win.cpp / platform_posix.cpp           平台相关小工具
├── resource.h
└── gui/                  Windows 11 风格界面
    ├── window.cpp / .hpp 窗口、布局与交互
    ├── widgets.cpp / .hpp  按钮、勾选框、下拉框、文件列表
    ├── theme.cpp / .hpp    亮/暗配色
    ├── gui.cpp / .hpp      图形界面入口
    └── gui_stub.cpp        非 Windows 的替身（其他平台上没有图形界面）

assets/                   应用图标（以及画图标的脚本）
shell/                    COM DLL 与 MSIX 稀疏包（仅 Windows）
build/                    构建脚本；产物全部在 build/out/
├── package.bat           Windows 三个发布版 → dist\windows\
├── build_exe.bat         Windows 可执行文件（会先调 shell\build_shell_menu.bat）
├── build_mupdf.bat       下载/编译 MuPDF 到 third_party\mupdf
├── build_installer.bat   整理载荷并编译 installer.iss
├── build_unix.sh         Linux/macOS 构建 → dist\<平台>\
├── check_compile.bat     只做语法检查
├── installer.iss         Inno Setup 脚本
├── ChineseSimplified.isl 安装包简体中文文案
├── app.rc                图标与版本资源
└── out/                  所有构建产物（不进仓库）
dist/                     发布包（不进仓库；package.bat 与 build_unix.sh 写这里）
tests/                    test_search.cpp、test_i18n.cpp
tools/                    fetch_mupdf.py、check_i18n.py、make_strings_en.py
third_party/mupdf/        MuPDF 构建产物（不进仓库；由 build\build_mupdf.bat 生成）
.github/workflows/build.yml  CI：Linux + 两个 macOS 架构
```

`.gitignore` 只排除构建产物与运行时临时文件：`build/out/`、`third_party/`、构建时生成的
`src/shell_payload.cpp`、CMake 中间文件、目标文件与库文件、`__pycache__/`（给 Python 辅助脚本用）、
临时目录 `.pdf_compress_*/` 以及默认输出 `*_compressed.pdf`。脚本本身是提交进仓库的，
所以任何人 clone 之后都能自己重新编译出这些二进制。

## 更新记录

### v2.1（C++20 重写）

- 改用 C++20 + CMake 重写；MuPDF 通过它的 C API 直接使用，压缩流程保持 Python 版的步骤顺序与搜索算法。
- 图形界面改为原生 Win32 + Direct2D，不再是额外依赖；Linux/macOS 不构建界面（那些构建只有命令行）。
- 只有一个自包含的可执行文件：MuPDF 是 DLL 构建时把 DLL 放在它旁边，没有 Python 运行时，
  也没有每次启动的解包过程。
- Windows 11 一级右键菜单组件被嵌进可执行文件（构建时生成的 `src/shell_payload.cpp`），
  所以便携版自己就能装菜单；exe 旁边的 MSIX 包与证书优先于内嵌副本。
- 两个单元测试可执行文件（`pdfc_search_tests`、`pdfc_i18n_tests`），加上 `tools/check_i18n.py`
  的漏翻检查。
- 安装向导的语言页仍然决定程序默认语言（写进 `HKLM\SOFTWARE\pdf-compress\Language`），
  `--lang`、`--help-cn`（`--help-zh`）与 `--help-en` 也都保留。

### v2.1（Python 实现）

- 新增图形界面（不带参数即打开）、Windows 11 一级右键菜单、由安装向导语言页决定程序语言，
  以及 `--lang` / `--help-cn` / `--help-en`。

### v2.0

- 修复：输出重定向时触发 Unicode 编码错误，把已经成功的压缩报成失败。
- 修复：无损结果已经小于目标时，参数搜索会直接放弃。
- 修复：手动模式输入无效字符会整份文档重新渲染。
- 修复：`scale` 会同时改变页面物理尺寸，现在只影响 DPI。
- 修复：多进程按逻辑核数启动，现在按物理核心数。
- 修复：中断退出码为 0（现在 130）。
- 新增：600 DPI 上限（`--max-dpi`）、`--dpi` 固定分辨率渲染、`-j/--jobs`、`--force-render`、
  `--keep-page-box`、单页像素护栏与渲染进度输出。

## 许可证

MIT —— 见 [LICENSE](LICENSE)。Copyright (c) 2026 zyq1223334444。
