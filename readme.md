> [🇨🇳 中文](readme.zh.md)

# PDF Compressor

A high-performance PDF compression tool built on PyMuPDF. It supports **lossless optimisation**
and **lossy rendering compression**, and it accelerates both with multi-core parallelism.

The point of the tool is that you say *how big* you want the file to be, not which dial to turn:
you give it a target size in MB and it searches for the parameters that land there.

## Features

- ✅ **Lossless optimisation** – drops redundant data, merges duplicate objects and compresses streams, with no loss of quality.
- ✅ **Lossy rendering compression** – renders every page as JPEG and controls the file size through resolution (DPI) and JPEG quality.
- ✅ **Smart parameter search** – calibrates once at 72 DPI and then jumps straight at the target using the measured
  `size ∝ scale^p` power law, so it **usually lands in 2–3 renders** (the old fixed-step search often needed 7 or more).
- ✅ **Says so when the target is unreachable** – e.g. *"even at the smallest setting the file is 32 MB, it cannot go down to 0.01 MB"*,
  instead of pointlessly retrying.
- ✅ **Two strategies** – `closest` (within ±5% of the target, above or below) or `smaller` (must stay below the target, within 10%).
- ✅ **Interactive manual mode** – step through the parameters, with a hint about the DPI that would hit the target.
- ✅ **Higher resolution ceiling** – up to **600 DPI** by default (the old build was hard-capped at ~216 DPI); change it with `--max-dpi`.
- ✅ **Multi-core acceleration** – chunks across the **physical cores** by default (hyper-threading is measurably slower here); tune it with `-j`.
- ✅ **Page geometry is preserved** – DPI only changes image sharpness; it **no longer changes the paper size**
  (the old build turned A4 into A2 or into half an A4).
- ✅ **Memory guard** – at most 40 Mpx per rendered page (≈120 MB per worker); larger pages are reduced automatically, with a warning.
- ✅ **Interrupt safe** – `Ctrl+C` keeps the best result produced so far, removes every temporary file and exits with code 130.
- ✅ **Encoding safe** – redirecting the output to a file or a pipe no longer crashes the run (the old build did).

## Installation

```bash
pip install -r requirements.txt
```

Or install PyMuPDF directly:

```bash
pip install pymupdf
```

## Usage

### As a Python script

```bash
python pdf_compress.py <INPUT_PDF> <TARGET_SIZE_MB> [OPTIONS]
```

### Option 1: installer (Windows, recommended)

Download **`pdf_compress_setup_2.0.0.exe`** from
[Releases](https://github.com/zyq1223334444/pdf-compress/releases) and run it:

- installs to `C:\Program Files\pdf-compress` with Start Menu shortcuts and a **proper uninstaller**
  (it also appears under Settings -> Apps)
- optionally adds the install folder to the system `PATH` (checked by default), so `pdf_compress` works
  from any terminal; the uninstaller **restores `PATH` byte-for-byte** — the original value is backed up
  in the registry before the edit

### Option 2: no installation (Windows single file / zip)

| Download | Shape | Startup (measured) | Exit code on Ctrl+C |
|---|---|---|---|
| `pdf_compress.exe` | single file (~26 MB, PyMuPDF inside) | ≈2.5 s (unpacks on every run) | `0xC000013A` (see below) |
| `pdf_compress_standalone_win64.zip` | unzip into a folder | ≈0.4 s | **130**, same as the script |

```bash
pdf_compress.exe <INPUT_PDF> <TARGET_SIZE_MB> [OPTIONS]
```

> **Known difference in the single-file build**: `--onefile` wraps the real program in a self-extracting
> bootstrap process — and it has to unpack PyMuPDF, which is why startup is so much slower. A console
> Ctrl+C reaches both processes, the bootstrap exits first, so the shell sees `0xC000013A`
> (`-1073741510`) instead of 130. The program itself still shuts down cleanly: it keeps the best result it
> had, cleans its temp folder and leaves no stray process. Only the exit code differs.

### Option 3: Linux / macOS

| Platform | Single file | Folder build (keeps the exec bit, recommended) |
|---|---|---|
| Linux x86_64 | `pdf_compress_linux_x86_64` | `pdf_compress_linux_x86_64.tar.gz` |
| macOS Intel | `pdf_compress_macos_x86_64` | `pdf_compress_macos_x86_64.tar.gz` |
| macOS Apple Silicon | `pdf_compress_macos_arm64` | `pdf_compress_macos_arm64.tar.gz` |

```bash
tar -xzf pdf_compress_linux_x86_64.tar.gz
./pdf_compress_standalone/pdf_compress doc.pdf 5
```

> The macOS binaries are **unsigned**, so Gatekeeper may block the first run:
> `xattr -d com.apple.quarantine <file>`, or right-click -> Open.
> The Linux and macOS binaries are built on **real machines** by GitHub Actions
> (see `.github/workflows/build.yml`), because Nuitka compiles Python to C and then calls the target
> platform's own linker — there is no cross-compiling. To build them yourself: `bash build/build_unix.sh`
> (needs gcc / patchelf on Linux).

All three shapes are compiled with Nuitka (Python -> C -> machine code) and need no Python installation.
Every option matches the script version, and multi-process acceleration works in the binaries too
(on Linux/macOS the worker count is derived from the physical core count).

### Render at a fixed resolution (no target needed)

```bash
python pdf_compress.py scan.pdf --dpi 150 -o ./out
```

### Options

| Option | Description |
|--------|-------------|
| `-o, --output-dir DIR` | Output directory (default: same as the input file) |
| `--mode {auto,manual}` | `auto` = automatic search (default), `manual` = interactive |
| `--strategy {closest,smaller}` | `closest` = within ±5% of the target (default); `smaller` = strictly below the target, within 10% |
| `--max-retries N` | Maximum number of render attempts (default 8; normally 3 are enough) |
| `--dpi N` | Render once at this resolution (the target size becomes optional) |
| `--max-dpi N` | Upper resolution bound for the search (default 600) |
| `--min-dpi N` | Lower resolution bound for the search (default 7.2) |
| `-j, --jobs N` | Worker processes (default: physical core count; `0` = auto) |
| `--force-render` | Render anyway when the lossless result is already smaller than the target |
| `--keep-page-box` | Keep the original page size (MediaBox) and draw the cropped content back in place |
| `--dry-run` | Show the plan only, do not compress |
| `--help-zh` | Show help in Chinese |
| `-h, --help` | Show this English help |

### Examples

```bash
# Compress to roughly 5 MB (auto mode, closest strategy)
python pdf_compress.py mydoc.pdf 5

# Strictly below 10 MB (within 10%)
python pdf_compress.py mydoc.pdf 10 --strategy smaller

# Manual mode with a custom output directory
python pdf_compress.py mydoc.pdf 8 --mode manual -o ./output

# Just 150 DPI, whatever the size
python pdf_compress.py mydoc.pdf --dpi 150

# Raise the ceiling to 900 DPI when aiming for a larger file
python pdf_compress.py mydoc.pdf 300 --max-dpi 900

# Render with four processes
python pdf_compress.py mydoc.pdf 20 -j 4
```

## How it works

1. **Lossless optimisation** – `save(garbage=4, deflate=True, clean=True)` removes unused objects and compresses
   streams. The result is measured **in memory**, and if the gain is below 1% no copy is written at all
   (the old build always wrote a full duplicate). Image-only PDFs typically gain 0%; uncompressed text PDFs can gain 94%.

2. **Decision** – if the lossless result is **already smaller than the target**, the tool neither silently gives up nor
   silently inflates the file:
   - it states the situation clearly;
   - it explains that rendering further **only makes the file bigger and does not improve quality or sharpness**
     (resolution and detail are not gained);
   - in an interactive terminal it asks whether to continue; in a non-interactive run it keeps the lossless result
     unless `--force-render` is given.

3. **Render search** – if the lossless result is larger than the target:
   - one calibration render at 72 DPI;
   - the measured points are interpolated/extrapolated in log-log space along `size ∝ scale^p` to **jump to the target**;
   - JPEG quality is only used as a secondary knob once the scale hits a bound;
   - the `smaller` strategy aims at the centre of the acceptance band (0.95 × target) so it does not get stuck
     just barely above the target;
   - if the target cannot be reached at all, the achievable range is reported and the search stops early.

4. **Multi-processing** – pages are split into contiguous chunks (at most one chunk per worker, no tiny tail chunks),
   rendered in parallel and merged in order. Documents with ≤4 pages (or `-j 1`) use the sequential path.
   Measured on an 8-page document: about **1.9×** faster with 4 processes; 8 hyper-threads were slower than 4.

## Output and status markers

Every status line starts with a bracketed marker, which makes logs easy to read and filter:

| Marker | Meaning |
|--------|---------|
| `[任务]` | Task parameters (input, target, strategy, workers) |
| `[无损]` | Lossless optimisation phase |
| `[渲染]` | Render progress, and the parameters/result of each attempt |
| `[搜索]` | The predicted parameters for the next attempt |
| `[成功]` | Output produced as requested |
| `[结果]` | Final size, error, compression ratio, number of renders |
| `[警告]` | Strategy not fully met, or a parameter was clamped |
| `[提示]` | Informational hint (e.g. suggesting another strategy) |
| `[中断]` | Interrupted; the best result was kept |
| `[失败]` | Failure, exit code 1 |

## Exit codes

| Code | Meaning |
|------|---------|
| `0` | Success (including "closest result output" when the target was not fully reachable) |
| `1` | Failure (missing file, not a PDF, encrypted, invalid arguments, ...) |
| `130` | Interrupted with Ctrl+C (the best result was kept) |

## Important notes

- **Lossy rendering permanently discards text and vector information** – the output becomes a set of images.
  This tool is best for scanned documents or image-based PDFs.
- `--strategy smaller` means **strictly** smaller: if the search can only produce 8.01 MB for an 8 MB target, it will
  output a smaller, more conservative result and print a `[提示]` about the closer alternative (available with `closest`).
- The resolution ceiling is 600 DPI by default. Raise it with `--max-dpi` for larger/higher-quality output, but a page
  exceeding 40 Mpx is automatically clamped to protect memory.
- Output pages are based on the **crop box** by default (no scanner margins). Add `--keep-page-box` to preserve the
  original paper size page by page.
- The output is named `<name>_compressed.pdf`; temporary files live in a hidden per-run directory and are removed
  on both normal completion and interruption.

## Repository layout

The repository holds the program and the means to build it. Everything a build *produces* lives in one
ignored folder, so a checkout stays clean:

```
pdf_compress.py             the whole program (single file)
requirements.txt            runtime dependency (pymupdf)
readme.md / readme.zh.md    these documents
LICENSE
.github/workflows/build.yml CI: Linux + both macOS architectures
build/                      build tooling - part of the repository
├── build_exe.bat             Windows single-file exe
├── build_exe_standalone.bat  Windows folder build + zip
├── build_installer.bat       Windows installer (calls installer.iss)
├── build_unix.sh             Linux / macOS binaries
├── installer.iss             Inno Setup script
├── ChineseSimplified.isl     Simplified Chinese texts for the installer
└── out/                      ignored: every artefact and Nuitka intermediate
```

`build/out/` is the only thing `.gitignore` excludes here. The scripts themselves are tracked, so anyone
can rebuild the binaries from a checkout.

## Building from source (Nuitka)

| What you want | Where to build it | Command |
|---|---|---|
| Windows single-file exe | Windows | `build\build_exe.bat` → `build\out\pdf_compress.exe` (~3 min) |
| Windows folder build | Windows | `build\build_exe_standalone.bat` → folder + zip in `build\out\` |
| Windows installer | Windows | `build\build_installer.bat` (needs Inno Setup 6.5+, see below) |
| Linux / macOS binaries | **the target platform** (WSL / Mac / CI) | `bash build/build_unix.sh` → `build/out/dist_unix/` |

The scripts resolve the repository root themselves, so they can be started from any working directory.
They only ever write inside `build/out/`.

> **Why Linux/macOS cannot be built on Windows**: Nuitka compiles Python to C and then calls the
> **target platform's own** compiler and linker — there is no cross-compiling. This repo uses GitHub
> Actions to build Linux and both macOS architectures on real machines; see
> `.github/workflows/build.yml` (run it by hand, or push a tag and the binaries are attached to the
> release automatically).

On Windows a C compiler is required: **MSVC 14.3+** (Visual Studio 2022 Build Tools or newer).
Note that Nuitka **cannot use MinGW with Python 3.13+**, MSVC only.

The installer is compiled with **Inno Setup 6.5+**: `winget install --id JRSoftware.InnoSetup -e`.
Its `[Code]` section does two things that were measured, not guessed: before adding the install folder
to the system `PATH` it **backs the original value up in the registry** and restores it byte-for-byte on
uninstall (a user's `PATH` may end in `;` or contain empty entries, which pure string surgery cannot undo
cleanly), and it removes the Start Menu entries, the uninstall registry entry and the install folder.

What `build_exe.bat` does, and why (the full reasoning is in the script comments):

```bat
set VSLANG=1033                                  :: see (1) below
python -m nuitka --standalone --onefile --msvc=latest --low-memory --lto=no ^
    --nofollow-import-to=pymupdf ^
    --no-deployment-flag=excluded-module-usage ^
    --include-data-files="<site-packages>\pymupdf\*.py=pymupdf/" ^
    --include-data-files="<site-packages>\pymupdf\*.pyd=pymupdf/" ^
    --include-data-files="<site-packages>\pymupdf\*.dll=pymupdf/" ^
    --include-data-files="%SystemRoot%\System32\msvcp140.dll=msvcp140.dll" ^
    --include-data-files="<python>\python3.dll=python3.dll" ^
    --windows-console-mode=force --output-filename=pdf_compress.exe pdf_compress.py
```

**Why PyMuPDF is *not* compiled by Nuitka:**

1. **A localized Visual Studio breaks the build**: Nuitka's Scons backend decodes `cl.exe`
   output with the `mbcs` code page and dies with
   `UnicodeDecodeError: 'mbcs' codec can't decode bytes`. `VSLANG=1033` forces English messages.
2. **PyMuPDF is enormous**: `pymupdf/mupdf.py` (5065 functions + 547 classes) expands into a single
   **122 MB / 2.35 million line** C file. With link-time optimisation MSVC fails on it with
   `fatal error C1002: out of heap space` and `LNK1257`.
3. **Disabling LTO is not enough either**: Nuitka's optimiser oscillates over that module for
   ~20 minutes and MSVC still has to swallow the giant translation unit.
4. So only this project's own code is compiled to C, and PyMuPDF is shipped verbatim
   (`--nofollow-import-to` plus explicit data-file packaging of its `.py`/`.pyd`/`.dll`).
   That cuts the build from ~50 minutes to **~3 minutes** with identical behaviour.
5. PyMuPDF's `_mupdf.pyd` / `_extra.pyd` / `mupdfcpp64.dll` depend on **MSVCP140.dll** (the MSVC C++
   runtime) and **python3.dll** (the CPython stable-ABI forwarder). Nuitka only ships vcruntime140,
   so both must be packaged explicitly, otherwise the exe fails with
   `ImportError: DLL load failed while importing _extra`.

**The exe has been verified** (21 checks passing): multi-process compression of an 8-page document
(where the old PyInstaller exe crashed with `BrokenProcessPool`), the sequential path for 4-page
files, Chinese help, redirected output, `--force-render`, and interruption. It also still works
after renaming the installed `pymupdf` away, confirming it is genuinely self-contained.

## Changelog (v2.0)

- Fixed: with redirected output (batch files, scheduled tasks, CI) the emoji markers raised `UnicodeEncodeError`;
  the old build reported a **successful** compression as a failure and could delete every temporary file.
- Fixed: the search gave up whenever the lossless result was already smaller than the target, and wrongly claimed
  the target was unreachable.
- Fixed: typing an invalid character in manual mode **re-rendered the whole document**, with broken attempt numbering.
- Fixed: `scale` also changed the physical page size (A4 -> A2 or half an A4); it now only affects DPI.
- Fixed: workers were spawned per logical core, making hyper-threading slower; now physical cores are used.
- Fixed: interrupt exited with code 0 (now 130); the lossless phase always wrote a full duplicate (now skipped below 1% gain).
- Added: 216 -> 600 DPI ceiling (`--max-dpi`), fixed-resolution rendering (`--dpi`), `-j/--jobs`,
  `--force-render`, `--keep-page-box`, a per-page pixel guard, render progress output, friendlier errors.
- Changed: the exe is now built with Nuitka (Python -> C -> machine code), which fixes the old exe crashing on
  multi-process jobs for PDFs with more than 4 pages (`BrokenProcessPool`).

## License

MIT
