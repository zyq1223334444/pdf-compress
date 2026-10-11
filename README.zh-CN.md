[🇬🇧 English](README.md) · [⬇ 下载](https://github.com/zyq1223334444/pdf-compress/releases)

# PDF Compressor

**你说要多大的 PDF，它去找能达到那个大小的参数。**

给它一个目标大小（MB），它给你一个差不多那么大的文件。C++20 + CMake 写成，内核是
[MuPDF](https://mupdf.com/)，有三个入口：

| | |
|---|---|
| **命令行** | `pdf_compress 输入.pdf 5` —— 压到 5 MB 左右。Windows、Linux、macOS |
| **图形界面** | 仅 Windows。Win32 + Direct2D 手工绘制，不依赖任何界面框架，不需要装运行时 |
| **资源管理器右键菜单** | Windows 11 一级菜单，`.pdf` 文件直接出现在右键菜单里，不用点"显示更多选项" |

两种压缩方式配合使用：**无损优化**（清理并重新打包，画质不变）和**有损渲染**（按搜索算出的
分辨率把每页重新渲染成 JPEG）。能无损解决就无损解决，必须渲染时才渲染。

## 功能

- **优先无损** —— 丢掉无用对象、重建压缩流，结果在内存里量出来，收益不到 1% 就根本不写文件。
- **必要时有损渲染** —— 每页渲染成 JPEG，文件大小由分辨率（DPI）和 JPEG 质量共同决定。
- **会算参数的搜索** —— 先在 72 DPI 标定一次，再用实测的 `大小 ∝ 缩放^p` 关系直接跳到目标，
  通常 **2~3 次渲染**就命中。
- **目标做不到时直说** —— 会告诉你实际能做到的范围，而不是反复白试。
- **两种策略** —— `closest`（离目标最近，±5% 以内，可以大也可以小）或 `smaller`
  （必须在目标以下，误差 10% 以内）。
- **手动模式** —— 自己一步步调，并给出"要达到目标大概需要多少 DPI"的提示。
- **多进程渲染** —— 默认按**物理核心**分块（这里开超线程反而更慢），`-j` 可改。
- **页面尺寸不变** —— DPI 只改像素数，不改纸张大小。
- **内存保护** —— 单页最多 40 Mpx（约 120 MB/进程），超出的页会自动降分辨率并给出提示。
- **能中断** —— `Ctrl+C` 会保留当前最好的结果、清掉全部临时文件，退出码 130。
- **中英双语** —— 界面、帮助和所有提示都支持中文与 English。
- **免安装** —— 发布的可执行文件是自包含的单文件：MuPDF 和右键菜单组件都编译在里面。

## 下载

到 [Releases](https://github.com/zyq1223334444/pdf-compress/releases) 页取。
二进制由 CI 构建后挂在那里，**不进仓库正文**。

| 平台 | 文件 | 说明 |
|---|---|---|
| Windows | `pdf_compress_<版本>_win64.exe` | 单文件版，双击即用，不用解压 |
| Windows | `pdf-compress-<版本>-win64.zip` | 同一个可执行文件 + 中英文 README + 许可证 |
| Windows | `pdf_compress_setup_<版本>.exe` | 安装包：开始菜单、PATH、卸载程序、右键菜单 |
| Linux | `pdf-compress_<版本>_linux_x86_64.tar.gz` | 命令行版 |
| macOS | `pdf-compress_<版本>_macos_x86_64.tar.gz`、`..._macos_arm64.tar.gz` | Intel 与 Apple 芯片，只有命令行 |

Windows 那两个包里的可执行文件已经把 MuPDF 和右键菜单组件都装进去了，放到哪里都能单独跑。
程序没有代码签名，SmartScreen 会拦一下，见[常见问题](#常见问题)。

## 快速开始

```bat
rem Windows：压到 5 MB 左右
pdf_compress.exe 报告.pdf 5

rem Windows：打开图形界面（不带参数也是这样）
pdf_compress.exe
pdf_compress.exe 报告.pdf 发票.pdf

rem 添加右键菜单（只对 .pdf 生效，第一次会请求管理员权限）
pdf_compress.exe --install-menu
```

```bash
# Linux / macOS：选项完全一样
./pdf_compress 报告.pdf 5
./pdf_compress --help-cn
```

输出默认写在输入文件旁边，名字是 `<原名>_compressed.pdf`，用 `-o` 可以改目录。

## 命令行

```
pdf_compress <输入PDF> <目标大小MB> [选项]
pdf_compress <输入PDF> --dpi 150 [选项]
```

选项的值写成 `--opt value` 或 `--opt=value` 都行。不认识的选项会报错并退出，退出码 2。

| 选项 | 说明 |
|---|---|
| `-o, --output-dir <目录>` | 输出目录（默认与输入文件同目录） |
| `--mode {auto,manual}` | `auto` = 自动搜索（默认），`manual` = 手动逐步调整 |
| `--strategy {closest,smaller}` | `closest`：最接近目标（默认）；`smaller`：必须小于目标 |
| `--max-retries <次数>` | 自动模式最多渲染几次（默认 8，正常 3 次内命中） |
| `--dpi <数值>` | 只按这个分辨率渲染一次，此时目标大小可以省略 |
| `--max-dpi <数值>` | 搜索的分辨率上限（默认 600） |
| `--min-dpi <数值>` | 搜索的分辨率下限（默认 7.2） |
| `-j, --jobs <数值>` | 渲染进程数（默认物理核心数，`0` 表示自动） |
| `--force-render` | 无损结果已经小于目标时，也强制渲染（只会更大，不会更清晰） |
| `--keep-page-box` | 保留原始页面尺寸（`MediaBox`），不用裁切框 |
| `--dry-run` | 只显示计划，不写文件 |
| `--gui` | 强制打开图形界面 |
| `--lang {zh,en}` | 只对本次运行指定语言（不写入设置） |
| `-h, --help` | 显示帮助，语言跟随当前设置 |
| `--help-cn`、`--help-en` | 强制中文帮助（`--help-zh` 同义）或英文帮助 |
| `--install-menu` / `--uninstall-menu` | 添加 / 移除资源管理器右键菜单（仅 Windows，需要管理员权限） |
| `-V, --version` | 显示版本号 |

另有两个内部选项：`--quiet` 关掉 `--install-menu` 的进度输出（安装包调用的就是
`--install-menu --quiet`），`--worker` 用来启动渲染子进程，不要手打。

### 两种策略

| 策略 | 可接受区间 | 行为 |
|---|---|---|
| `closest`（默认） | `0.95 × 目标` ~ `1.05 × 目标` | 可以比目标大也可以小，搜索直接瞄目标 |
| `smaller` | `0.90 × 目标` ~ 目标（上限不含） | 必须严格小于目标，搜索瞄区间中点，免得卡在刚好超一点的地方 |

用 `smaller` 时，如果最好的那次比目标大，它会写出仍然小于目标的最近结果，并提示"另一个更接近的
结果只有 `closest` 会接受"。（手动模式里也有 `larger`/`smaller`，那是"下一次调大/调小"的回答，
不是策略。）

### 例子

```bat
pdf_compress.exe 报告.pdf 5                       rem 压到 5 MB 左右
pdf_compress.exe 报告.pdf 10 --strategy smaller    rem 必须小于 10 MB
pdf_compress.exe 报告.pdf 8 --mode manual -o out   rem 手动一步步调
pdf_compress.exe 报告.pdf --dpi 150                rem 固定分辨率，大小随它去
pdf_compress.exe 报告.pdf 300 --max-dpi 900        rem 想压大一点时抬高上限
pdf_compress.exe 报告.pdf 20 -j 4                  rem 用 4 个渲染进程
pdf_compress.exe 报告.pdf 2 --dry-run              rem 只看计划
```

### 退出码

| 退出码 | 含义 |
|---|---|
| `0` | 成功（`--dry-run`、以及目标达不到但已写出最接近的结果，同样算成功） |
| `1` | 失败（文件不存在、不是 PDF、已加密、写不出去、没有可用结果……） |
| `2` | 参数错误（未知选项、取值非法、既没给目标大小也没给 `--dpi`） |
| `130` | 被 `Ctrl+C` 中断，已保留当前最好的结果 |

### 输出标记

每条状态行都以方括号标记开头，方便过滤日志；图形界面的日志区用的是同一套标记。

| 标记 | 含义 |
|---|---|
| `[任务]` | 本次运行的参数（输入、目标、策略、进程数） |
| `[无损]` | 无损优化阶段 |
| `[渲染]` | 渲染进度，以及每次尝试的参数与结果 |
| `[搜索]` | 下一次尝试的预测参数 |
| `[成功]` / `[结果]` | 结果已写出；最终大小、误差、压缩比、渲染次数 |
| `[警告]` / `[提示]` | 策略未完全满足、参数被收敛、或一句说明 |
| `[中断]` / `[失败]` | 被中断（已保留最好结果）/ 失败 |

## 图形界面（Windows）

```
pdf_compress.exe                 打开窗口
pdf_compress.exe 报告.pdf        打开窗口并载入这个文件
```

- **文件卡片** —— "添加 PDF 文件"用系统自带的对话框（过滤 `*.pdf`、可多选）；"移除选中"和
  "清空"管理队列；双击某一行会在资源管理器里打开它所在的目录。同一个文件不会重复入队。
  文件按顺序压缩，每一行有自己的状态与结果。
- **设置卡片** —— 命令行能调的这里都有：目标大小、固定 DPI、输出目录（带选择按钮）、模式、策略、
  最大尝试次数、进程数、搜索上下限，以及"强制渲染"、"保留页面尺寸"、"只看计划"三个勾选项。
- **进度与日志卡片** —— 进度条、实时日志和一行状态（*就绪*、*正在渲染 3/12 页*、*正在取消…*、
  *完成：2 个成功，1 个失败*）。"取消"会在当前这一块渲染完后停下，并保留最好的结果。
- 命令行会问到的那种问题（比如"无损结果已经小于目标，还要渲染吗？"）在窗口里变成一行带按钮的
  内联提示，而不是弹窗。
- 标题栏有 **中文 / English** 按钮和明暗主题切换，两个选择都会记住。
- 安装/卸载右键菜单故意只放在命令行，窗口里没有。

### 界面语言

优先级从高到低：

1. 命令行的 `--lang`（只对本次运行有效）；
2. 图形界面里选的语言，存在 `settings.ini`；
3. 注册表 —— 便携版是 `HKCU\Software\pdf-compress\Language`，否则是安装程序写下的
   `HKLM\SOFTWARE\pdf-compress\Language`（就是安装向导第一页选的那个）；
4. 系统区域设置（`zh*` 中文，其余英文）。

`-h`/`--help` 跟随当前设置的语言，`--help-cn` 和 `--help-en` 可以只对本次运行强制切换。

## 资源管理器右键菜单（仅 Windows，仅 `.pdf`）

```bat
pdf_compress.exe --install-menu      rem 第一次需要管理员权限
pdf_compress.exe --uninstall-menu
```

安装包里有一项默认勾选的任务。点菜单项会用图形界面打开你右键的那个文件（多选则一起带过去）。

Windows 11 的**一级**菜单只认一种注册方式：实现 `IExplorerCommand` 的原生 COM DLL，装在一个
**MSIX 稀疏包**里，通过 `windows.fileExplorerContextMenus` 声明接管 `.pdf`。这套东西在 `shell/`：

| 文件 | 作用 |
|---|---|
| `shell/ExplorerCommand.cpp` | 实现 `IExplorerCommand` 的 COM 服务器 |
| `shell/AppxManifest.xml` | 稀疏包清单（COM 代理服务器 + `.pdf` 菜单注册） |
| `shell/make_assets.py`、`shell/Resources/` | 包里的磁贴图 |
| `shell/make_payload.py` | 把 `.msix` 与 `.cer` 转成 `src/shell_payload.cpp` |
| `build/windows/build_shell_menu.bat` | 编译 DLL、打包并用 `makeappx`/`signtool` 签名 |

同时还会写一个传统的 `HKCU\Software\Classes\SystemFileAssociations\.pdf\shell` 动词，这样
Windows 10 和"显示更多选项"子菜单也能用。

- **第一次**安装只需要管理员权限来信任证书，程序会自己请求提权；之后都按普通权限运行。用安装包装
  过的机器上证书已经就位，所以 `--install-menu --quiet` 不会再弹 UAC。
- 构建时会生成一张**自签名证书**（`CN=zyq1223334444`，有效期 10 年，从不进仓库）。安装程序只把
  它的**公钥部分**导入 `Cert:\LocalMachine\TrustedPeople`，卸载时再删掉。说清楚一点：这**确实是**
  一次与安全相关的改动（往机器级信任库加了一张证书），也是进一级菜单必须付的代价。
- 这个包还被**嵌在可执行文件里**（`src/shell_payload.cpp`），所以便携版自己就能装菜单，旁边不用
  带任何文件；如果 exe 旁边正好有 `pdf_compress_shell.msix` 和 `.cer`，则优先用旁边那两个。
- Linux/macOS 上 `--install-menu` 会提示只支持 Windows。
- 菜单项没立刻出现就重启资源管理器，或者注销再登录：外壳会缓存动词。

## 安装包（Windows）

下载 `pdf_compress_setup_<版本>.exe` 直接装，或者自己编：`build\windows\build_installer.bat`。

- 装到 `C:\Program Files\pdf-compress`，带开始菜单项（其中一个"命令行帮助"快捷方式执行
  `pdf_compress.exe --help`）和正规的卸载程序，也会出现在"设置 → 应用"里；
- **安装向导第一页选的语言会成为程序默认语言** —— 写进 `HKLM\SOFTWARE\pdf-compress\Language`，
  命令行输出、帮助和图形界面都跟着它走；
- 可以把安装目录加进系统 `PATH`（**默认勾选**），之后在任何终端里都能直接敲 `pdf_compress`；
  卸载时按改动前备份下的值**逐字节还原** `PATH`；
- 右键菜单那项**默认勾选**；安装程序先信任证书，再让程序给当前用户注册这个包；
- 桌面快捷方式那项有，但**默认不勾**；
- 卸载会移除右键菜单（连同证书）、`PATH` 里的那一项、开始菜单项和 `HKLM\SOFTWARE\pdf-compress`。

## 配置

| 位置 | 谁写的 | 内容 |
|---|---|---|
| `%LOCALAPPDATA%\pdf-compress\settings.ini` | 图形界面（也可以手改） | 语言、主题、上次用的表单值 |
| `HKLM\SOFTWARE\pdf-compress` | 安装程序 | `Language`、`InstallDir`、`Version`、`OriginalPath`（`PATH` 备份） |
| `HKCU\Software\pdf-compress` | 便携版执行 `--install-menu` 时 | `InstallDir`、`Language` |

非 Windows 平台的设置文件是 `$XDG_CONFIG_HOME/pdf-compress/settings.ini`，退到
`~/.config/pdf-compress/settings.ini`（连 `HOME` 都没有时退到 `$TMPDIR` 下的固定目录）。Windows
之外没有注册表。

`settings.ini` 是极简的 `key=value`（UTF-8、LF、`#` 开头是注释），写入是原子的：先写临时文件再
改名，所以崩溃也不会留下半个文件。删掉它就回到默认状态；文件坏了按空文件处理 —— 设置读不出来
绝不会变成启动失败。

键：`language`、`theme`、`target_size`、`dpi`、`output_dir`、`mode`、`strategy`、`max_retries`、
`max_dpi`、`min_dpi`、`jobs`，以及 `force_render`、`keep_page_box`、`dry_run`（`1`/`0`）。

命令行选项的优先级永远最高，`settings.ini` 只提供图形界面的初始值。

## 实现说明

1. **无损优化。** 用 MuPDF 的文档写入器做垃圾回收、流压缩和清理，结果**在内存里**量大小；收益
   不到 1% 就干脆不写副本 —— 原文件直接当作无损候选。纯图片型 PDF 通常一点都压不下去，而未压缩
   的文本型 PDF 可能收益很大。

2. **决策。** 如果无损结果**已经小于目标**，程序既不会偷偷放弃，也不会偷偷把文件做大：它会说明
   情况、解释继续渲染只会让文件更大而不会更清晰，然后询问（交互终端里提问，窗口里是一行内联
   提示）。非交互运行默认保留无损结果，除非显式给了 `--force-render`。

3. **渲染搜索。** 否则先在 72 DPI 标定一次，再用实测点在 log-log 空间按 `大小 ∝ 缩放^p`
   插值/外推，直接**跳到目标**。JPEG 质量（50~100，默认 85）是缩放撞到边界后的第二根杠杆。
   `smaller` 瞄的是区间的中点（0.95 × 目标），免得卡在刚好超一点的地方。目标根本达不到时会直接
   报出可做到的范围，而不是一直试。

4. **多进程渲染。** 页面切成连续的分块（每个进程最多一块，不留小尾巴），交给 `--worker`
   子进程并行渲染，最后按顺序合并。页数 ≤4 或者 `-j 1` 时走串行路径。

为什么不会偏得离谱：可接受区间本来就窄，搜索又瞄的是区间中点；`大小 ∝ 缩放^p` 的拟合用的是
**这份文档自己**的实测点，斜率还夹在 1.2~3.0 之间，一次异常测量带不偏下一次猜测；目标不可达时
提前收手；尝试次数用尽时，也会把最接近目标的那次结果写出来。

## 构建

| 组件 | 要求 |
|---|---|
| PDF 内核 | **MuPDF ≥ 1.25** —— 只在编译时需要，发布的二进制里已经静态编进去了 |
| 编译器（Windows） | **MSVC 2022**（Visual Studio 2022 或 Build Tools），装"使用 C++ 的桌面开发" |
| 编译器（Linux/macOS） | 支持 C++20 的 **GCC** 或 **Clang** |
| 构建系统 | **CMake ≥ 3.16** |
| 安装包（可选） | **Inno Setup 6.5+**（实测 6.7.3） |
| 右键菜单（可选，仅 Windows） | Windows 10/11 SDK（`makeappx.exe`、`signtool.exe`）和 Python（给 `shell/*.py` 用） |

### 获取 MuPDF

CMake 按这个顺序找：`-DMUPDF_ROOT=<前缀>` → 环境变量 `MUPDF_ROOT` → 系统路径 / `pkg-config` →
仓库里的 `third_party/mupdf/`。

| 平台 | 命令 |
|---|---|
| Windows | `build\windows\build_mupdf.bat` → `third_party\mupdf`（装了 vcpkg 就用它，否则从 mupdf.com 下载源码包，用 `platform\win32\libmupdf.vcxproj` 编译） |
| Linux / macOS | `bash build/unix/build_mupdf_unix.sh` → 固定版本 1.26.0 编到 `build/out/mupdf-unix-prefix`，静态链接 |
| Debian/Ubuntu | `sudo apt-get install -y build-essential cmake libmupdf-dev` |
| Fedora | `sudo dnf install -y gcc-c++ cmake mupdf-devel` |
| Arch | `sudo pacman -S --needed base-devel cmake mupdf` |
| macOS | `brew install cmake mupdf`（另需 Xcode 命令行工具） |
| 自己编译 | `-DMUPDF_ROOT=<前缀>`（前缀里要有 `include/` 和 `lib/`） |

两个脚本都能带版本号参数，也都能反复运行 —— 已经编好了就什么都不做。

> **需要 MuPDF 1.25 或更新。** 发行版包普遍偏旧：Ubuntu 24.04 带的还是 1.23，它的 JPEG 编码接口
> 只收三个参数，而这里的源码用的是四个。`build/unix/build_mupdf_unix.sh` 就是为这件事存在的：
> 它编译 Windows 构建用的同一个 1.26.0，并且静态链接，所以发布件旁边不用再放 `libmupdf.so`。

### Windows

```bat
build\windows\build_mupdf.bat      rem 每台机器一次：MuPDF -> third_party\mupdf
build\windows\build_exe.bat        rem 先编右键菜单包，再走 CMake -> build\out\pdf_compress.exe

rem 可选
build\windows\check_compile.bat    rem 只做语法检查（只需要头文件）
build\windows\package.bat          rem 三种发布件一起出 -> dist\windows\
build\windows\build_installer.bat  rem 只编安装包
build\windows\build_shell_menu.bat rem 只编右键菜单包（build_exe.bat 会先调它）
```

想手动用 CMake 也一样：

```bat
cmake -S . -B build\out\cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build\out\cmake --config Release
```

### Linux / macOS

```bash
bash build/unix/build_mupdf_unix.sh                                  # 一次：MuPDF -> build/out/mupdf-unix-prefix
MUPDF_ROOT="$PWD/build/out/mupdf-unix-prefix" bash build/unix/build_unix.sh
```

`build_unix.sh` 自己算出仓库根，CMake 中间产物全部留在 `build/out/cmake_unix/`，最后做一次冒烟
测试（`--help` 必须能跑起来），发布文件夹写到 `dist/<平台>/pdf-compress_<版本>_<os>_<arch>/`。
不做压缩包：Linux 和 macOS 上发布件本来就是一个文件夹。系统 MuPDF 版本太旧时它也会先给一句警告。

> **Linux/macOS 没有图形界面** —— 窗口是 Win32 + Direct2D，其他平台上由
> `src/gui/gui_stub.cpp` 顶替，`--gui` 会明确告诉你这一点而不是报错。

### 测试

CMake 会编出两个测试程序 —— `pdfc_search_tests`（搜索数学，完全不需要 MuPDF）和
`pdfc_i18n_tests`（文案表）—— 注册为 `search` 和 `i18n`：

```bat
cmake --build build\out\cmake --config Release
ctest --test-dir build\out\cmake -C Release
```

```bash
cmake --build build/out/cmake_unix
ctest --test-dir build/out/cmake_unix
```

`python tools/check_i18n.py` 会把源码里每一处 `tr("...")` 抽出来和英文表比对，漏翻的文案不会悄悄
烂在那里。

### 打包与发布

```bat
build\windows\package.bat
```

写出 `dist\windows\`：三种 Windows 发布件（单文件、文件夹、文件夹 zip）加安装包。Linux 和 macOS
由 CI 做同样的事 —— 见 `.github/workflows/build.yml`：它先编译固定版本的 MuPDF（每个 runner
缓存），再在真机上跑 `build/unix/build_unix.sh`，打上 `v2.1.0` 这样的 tag 时把产物挂到 Release。
**`dist/` 是 gitignore 的**：发布产物挂在 GitHub Release 上，不进仓库。

## 目录结构

| 路径 | 放什么 |
|---|---|
| `src/` | 全部 C++ 源码，另有 `src/gui/`（Win32 + Direct2D）和 `src/app.rc`（图标与版本资源） |
| `shell/` | Windows 11 右键菜单组件：COM 服务器、MSIX 清单、磁贴图、载荷生成脚本 |
| `tests/` | 两个测试程序 |
| `tools/` | Python 辅助脚本：文案检查、英文表生成、头文件下载 |
| `assets/` | 程序图标和画图标的脚本 |
| `build/windows/` | `build_exe.bat`、`build_mupdf.bat`、`package.bat`、`build_installer.bat`、`check_compile.bat`、`build_shell_menu.bat` |
| `build/unix/` | `build_unix.sh`、`build_mupdf_unix.sh` |
| `build/installer/` | `installer.iss` 和简体中文语言文件 |
| `build/out/` | 全部中间产物（gitignore） |
| `dist/` | 发布文件夹（gitignore） |

## 常见问题

**CMake 说找不到 MuPDF。** 找不到头文件或库时它会停下并给出可读的提示。Windows 上跑
`build\windows\build_mupdf.bat`，Linux/macOS 上跑 `build/unix/build_mupdf_unix.sh` 或用发行版包，
也可以用自己的前缀：`-DMUPDF_ROOT=<前缀>`。如果 `third_party\mupdf` 里只有头文件，那是没编完的
半成品，重跑一次脚本。

**右键菜单没出现。** 它是可选项，要用单独的命令装：`pdf_compress.exe --install-menu`。然后重启
资源管理器或注销再登录（外壳会缓存动词）；确认第一次安装拿到了管理员权限（否则证书进不了信任
库，注册会失败并给出提示）；还要确认这个 exe 里真的带了右键菜单组件 —— 没跑
`build\windows\build_shell_menu.bat` 或那一步失败的构建会明确说"本构建不含右键菜单"。

**杀毒软件报毒。** 这是本机用 MSVC 编出来的、**没有代码签名**的 Win32 程序，启发式扫描器对一个
从没见过的新二进制报警很常见。它做的每一件事都能在 `src/` 里读到 —— 不愿意凭信任接受的话，自己
跑 `build\windows\build_exe.bat` 编一个，或者给安装目录加个排除项。唯一真正与安全相关的动作上面
已经写清楚了：`--install-menu` 会往机器级信任库加一张自签名证书。

**同时给了 `--dpi` 和目标大小会怎样？** 固定分辨率优先：只按那个 DPI 渲染一次，提示一句目标被忽略，
然后写出结果，完全不做搜索。

**输出文件在哪？** 输出目录里的 `<原名>_compressed.pdf`（默认与输入文件同目录）。临时文件放在该
目录下隐藏的 `.pdf_compress_*` 里，正常结束、失败、中断都会清理掉。

**`Ctrl+C` 之后还剩什么？** 当前最好的结果会写到正常输出路径，全部临时文件清理干净，退出码 130。

**能一次压好几个文件吗？** 命令行一次一个输入；窗口里可以排任意多个队列，逐个处理。右键菜单会把
整个多选一起交给窗口。

## 更新记录

- **v2.1 —— C++ 重写（当前版本）。** 一个原生可执行文件取代 Python 运行时，原生
  Win32 + Direct2D 窗口取代 Tkinter，免清单的安装包，参数搜索从原型逐行移植过来。
- **v2.1 —— Python 实现。** 这个仓库的原型，现在已经不在树里；替换它的那次提交
  （*"feat!: replace the Python implementation with the C++ rewrite"*）还把它留在历史里，比如
  `git show <那次提交>^:pdfcomp/core.py`。
- **v2.0** —— 第一个支持"按目标大小搜索"的版本。

## 许可证

[MIT](LICENSE) © 2026 zyq1223334444

PDF 内核 MuPDF 版权归 Artifex Software，采用
[AGPL](https://www.gnu.org/licenses/agpl-3.0.html) 或商业许可双授权。发布的可执行文件是静态链接
它的；如果 AGPL 对你的用途是个问题，请用你自己取得授权的 MuPDF 从源码构建。
