> [🇨🇳 中文](readme.zh.md)

# PDF Compressor

A PDF compression tool written in **C++20**, built with **CMake**, with a hand-drawn
**Win32 + Direct2D** graphical interface. It does **lossless optimisation** and **lossy rendering
compression**, and it speeds both up with multi-process parallelism.

The point of the tool is that you say *how big* you want the file to be, not which dial to turn: you
give it a target size in MB and it searches for the parameters that land there.

Run it with no arguments and you get the graphical interface; passing PDF paths also opens it with
those files queued. The command line works as before, and both front ends drive the same engine.

## Relationship to the Python version

This repository is the **C++ rewrite**, and the C++ build is the **full-featured version**. The earlier
Python implementation is no longer part of the tree: it was a prototype, and the commit that replaced it
("feat!: replace the Python implementation with the C++ rewrite") still has it in the history if you ever
want to look (`git show <that commit>^:pdfcomp/core.py`).

What carried over:

- one native executable (`pdf_compress.exe`) — no Python runtime, no interpreter to install, no
  self-extracting bootstrap on every launch; when MuPDF itself is built as a DLL, those DLLs are staged
  next to the exe instead of being packed inside it;
- the same compression pipeline, ported step by step from `pdfcomp/core.py` (the search maths in
  `src/search.cpp` is a line-by-line translation, including Python's round-half-even semantics);
- the graphical interface is native Win32 + Direct2D instead of Tkinter, so it needs no GUI toolkit at
  all;
- the Explorer context menu component is embedded in the executable, so the portable build can install
  it on its own.

## Features

- ✅ **Lossless optimisation** – drops unused objects, sanitises and compresses streams, with no loss of
  quality. The result is measured in memory, and a gain below 1% is not written out at all.
- ✅ **Lossy rendering compression** – renders every page as JPEG and controls the file size through
  resolution (DPI) and JPEG quality.
- ✅ **Smart parameter search** – calibrates once at 72 DPI and then jumps straight at the target using
  the measured `size ∝ scale^p` power law, so it **usually lands in 2–3 renders**.
- ✅ **Says so when the target is unreachable** – it reports the range it can actually produce instead of
  pointlessly retrying.
- ✅ **Two strategies** – `closest` (within ±5% of the target, above or below) or `smaller` (strictly
  below the target, within 10%).
- ✅ **Interactive manual mode** – step through the parameters, with a hint about the DPI that would hit
  the target.
- ✅ **Higher resolution ceiling** – up to **600 DPI** by default; change it with `--max-dpi`.
- ✅ **Multi-process acceleration** – chunks across the **physical cores** by default (hyper-threading is
  measurably slower here); tune it with `-j`.
- ✅ **Page geometry is preserved** – DPI only changes the pixel count; the physical page size is left
  alone.
- ✅ **Memory guard** – at most 40 Mpx per rendered page (≈120 MB per worker); larger pages are reduced
  automatically, with a warning.
- ✅ **Interrupt safe** – `Ctrl+C` keeps the best result produced so far, removes every temporary file and
  exits with code 130.
- ✅ **A native graphical interface** – just run it with no arguments: Windows 11 styling, light/dark
  theme, the operating system's own file dialogs, several PDFs queued, and every command line option in
  the window.
- ✅ **Windows 11 top-level context menu** – `.pdf` files get a "Compress with PDF Compressor" entry
  directly in the right-click menu, not hidden under "Show more options".
- ✅ **The language follows the install choice** – the language picked in the setup wizard becomes the
  default for the command line output, the help, the graphical interface and the Start Menu shortcut.

## Requirements

| Component | Requirement |
|---|---|
| PDF engine | **MuPDF** — required; it is not bundled and there is no wheel that ships it |
| Compiler (Windows) | **MSVC 2022** (Visual Studio 2022 or the Build Tools) with the "Desktop development with C++" workload |
| Compiler (Linux/macOS) | **GCC** or **Clang** with C++20 support |
| Build system | **CMake ≥ 3.16** |
| Language standard | **C++20** (set by `CMakeLists.txt`) |
| Installer (optional) | **Inno Setup 6.5+** (tested with 6.7.3) |
| Context menu (optional, Windows) | Windows 10/11 SDK (`makeappx.exe`, `signtool.exe`) + Python (only for the payload/asset helper scripts in `shell/`) |

### Getting MuPDF

CMake looks for MuPDF in this order: `-DMUPDF_ROOT=<prefix>` → the `MUPDF_ROOT` environment variable →
the system paths / `pkg-config` → `third_party/mupdf/` inside the repository.

| Platform | Command |
|---|---|
| Windows | `build\build_mupdf.bat` → builds into `third_party\mupdf` (uses vcpkg if it is installed, otherwise downloads the source tarball from mupdf.com and builds `platform\win32\libmupdf.vcxproj` with MSBuild) |
| Debian/Ubuntu | `sudo apt-get install -y build-essential cmake libmupdf-dev` |
| Fedora | `sudo dnf install -y gcc-c++ cmake mupdf-devel` |
| Arch | `sudo pacman -S --needed base-devel cmake mupdf` |
| macOS | `brew install cmake mupdf` (plus the Xcode command line tools) |
| Your own build | configure with `-DMUPDF_ROOT=<prefix>` (a prefix containing `include/` and `lib/`) |

`build\build_mupdf.bat` defaults to MuPDF 1.26.0, accepts a version argument
(`build\build_mupdf.bat 1.24.9`) and is safe to re-run: if `third_party\mupdf` already holds a
usable build it does nothing.

## Building

### Windows: one command

```bat
build\build_exe.bat
```

It produces:

| Artefact | What it is |
|---|---|
| `build\out\pdf_compress.exe` | the program |
| `build\out\pdf-compress.exe` | the same file under its CMake target name |
| `build\out\shell\*.msix`, `build\out\shell\*.cer` | the context menu package, if that step ran |
| `build\out\cmake\` | the CMake tree (intermediates) |

The order matters: the context menu package is built **first**, because it generates
`src\shell_payload.cpp`, which the executable embeds. If that step fails it is only a warning — you get
an executable whose `--install-menu` says this build has no context menu component. Everything the
script writes lives under `build\out\`, which is gitignored.

### Windows: manual CMake

```bat
rem 1. MuPDF first (once per machine; skip this if you use vcpkg or a system prefix)
build\build_mupdf.bat

rem 2. Configure
cmake -S . -B build\out\cmake -G "Visual Studio 17 2022" -A x64 -DMUPDF_ROOT=%CD%\third_party\mupdf

rem 3. Build
cmake --build build\out\cmake --config Release --parallel
```

The result is `build\out\cmake\Release\pdf_compress.exe` (the CMake target is called `pdf-compress`,
but `OUTPUT_NAME` renames the binary to `pdf_compress.exe`, the name the MSIX manifest, the Start Menu
shortcut and the documentation all expect). On Windows the executable is linked as a GUI subsystem
application: double-clicking it does not pop up an extra console window, and when it is started from a
terminal it attaches to the parent console instead.

### Windows: the three release builds

```bat
build\package.bat
```

Packages what `build_exe.bat` produced (run it first, and `build_installer.bat` for the installer —
`package.bat` runs that one for you when its output is missing). Everything lands in `dist\windows\`:

| Artefact | What it is |
|---|---|
| `pdf_compress_2.1.0_win64.exe` | the single file build — the whole program in one executable |
| `pdf-compress-2.1.0-win64\` | the folder build: that executable plus the readmes, the licence and the context menu package |
| `pdf-compress-2.1.0-win64.zip` | the folder build, packed |
| `pdf_compress_setup_2.1.0.exe` | the installer |

The single file build really is single: MuPDF is linked statically and the context menu package is
embedded, so the executable can be copied anywhere on its own. `dist\` is gitignored — release
artefacts do not belong in the repository.

### Linux / macOS

```bash
bash build/build_unix.sh
```

The script resolves the repository root itself, so it can be started from any working directory. Every
CMake intermediate stays under `build/out/cmake_unix/`, and the release folder is written to
`dist/<platform>/pdf-compress_2.1.0_<os>_<arch>/`:

| Artefact | What it is |
|---|---|
| `pdf_compress` | the executable |
| `readme.md`, `readme.zh.md`, `LICENSE` | the same documents the Windows folder build carries |
| `libmupdf.so*` | only when MuPDF is a shared library — libraries from `/lib` and `/usr/lib` are skipped |

There is no archive: on Linux and macOS the folder itself is the normal shape of a release.

Point it at a non-system MuPDF with the `MUPDF_ROOT` environment variable. The script finishes with a
smoke test (`--help` must run and print something). CI builds these on real machines — see
`.github/workflows/build.yml`, which runs this very script on Ubuntu and on both macOS architectures and
attaches the results to a release when a tag is pushed.

> **There is no graphical interface on Linux/macOS**: those builds are the command line tool only. The
> window is Win32 + Direct2D, and `src/gui/gui_stub.cpp` takes its place everywhere else.

### Compile-only syntax check

```bat
build\check_compile.bat
build\check_compile.bat src\engine.cpp src\search.cpp
```

Linking the real program needs the MuPDF library, but checking API usage only needs the headers
(`tools\fetch_mupdf.py` downloads them into `third_party\mupdf\include`). This script runs
`cl /c /std:c++20 /W4 /permissive-` over every source file when given no arguments, or over the files
you name, so syntax, type and API-signature mistakes surface before the dependency stack is in place.

### Tests

`CMakeLists.txt` builds two test executables — `pdfc_search_tests` (the search maths, which needs no
MuPDF at all) and `pdfc_i18n_tests` (the string tables) — and registers them with `add_test` under the
names `search` and `i18n`:

```bat
cmake --build build\out\cmake --config Release
ctest --test-dir build\out\cmake -C Release
```

```bash
cmake --build build/out/cmake_unix --config Release
ctest --test-dir build/out/cmake_unix
```

`tools\check_i18n.py` is the missing-translation check: it extracts every `tr("...")` literal from the
sources and compares it against the English table, so a translation cannot quietly rot.

### Installer

```bat
build\build_exe.bat
build\build_installer.bat
```

`build_installer.bat` stages a payload (`build\out\pdf_compress_win64`: the exe, the MuPDF DLLs, the
context menu package and its certificate) and compiles `build\installer.iss`, producing
`build\out\dist_installer\pdf_compress_setup_2.1.0.exe`. It needs Inno Setup 6.5+, which you can install
with `winget install --id JRSoftware.InnoSetup -e`. `build\package.bat` runs it for you and copies the
result into `dist\windows\`.

### Context menu package on its own

```bat
shell\build_shell_menu.bat
```

Normally you never call this by hand: `build_exe.bat` calls it first. It compiles `ExplorerCommand.dll`
(an in-proc COM server implementing `IExplorerCommand`), creates a build-time self-signed certificate,
packs and signs `pdf_compress_shell.msix`, and writes the two binaries into `src\shell_payload.cpp` as
hex strings so they can be embedded into the executable.

## Usage

### Graphical interface (Windows)

```bat
pdf_compress.exe
pdf_compress.exe report.pdf
pdf_compress.exe report.pdf invoice.pdf
pdf_compress.exe --gui
```

- **Files card**: *Add PDF files* opens the operating system's own file dialog with a `*.pdf` filter and
  multiple selection; *Remove selected* and *Clear* manage the queue; double-clicking a row opens its
  folder in Explorer. The same file cannot be queued twice.
- Files are compressed one after another, and every row carries its own status and result (waiting →
  compressing → done, with the size; failed; cancelled).
- **Settings card** — every command line option is here: target size (MB), fixed DPI (may be left
  empty), output folder (with a folder-picker button), mode (search automatically / adjust manually),
  strategy (closest to the target / must stay below the target), maximum render attempts, worker
  processes, search ceiling and floor in DPI, plus the *force lossy rendering (even if the file gets
  bigger)*, *keep the original page size* and *show the plan only* check boxes.
- **Progress and log card**: a progress bar, a live log and a status line (*ready*, *rendering 3/12
  pages*, *cancelling…*, *done: 2 succeeded, 1 failed*). **Cancel** stops after the chunk that is
  currently rendering and keeps the best result produced so far.
- Questions the command line would ask on stdin (for example *"the lossless result is already smaller
  than the target, render anyway?"* or the manual-mode *"what next?"*) become an inline prompt row with
  buttons in the window instead of a modal dialog.
- The header has **中文 / English** buttons and a light/dark theme toggle; both are remembered.
- Installing/removing the context menu is deliberately **not** in the window — command line only.

### Interface language

The language is decided in this order, highest priority first:

1. `--lang` on the command line (this run only, not saved to the settings);
2. the language picked in the graphical interface, stored in `%LOCALAPPDATA%\pdf-compress\settings.ini`;
3. the language recorded in the registry — `HKCU\Software\pdf-compress\Language`, written by
   `--install-menu` for a portable install, otherwise `HKLM\SOFTWARE\pdf-compress\Language`, written by
   the installer (the one chosen in the wizard);
4. the system locale (`zh*` means Chinese, everything else English).

- `-h` / `--help` follow the currently configured language; `--help-cn` forces Chinese (`--help-zh` is
  the same thing) and `--help-en` forces English.
- The graphical interface can switch language at any time; the choice is written to `settings.ini` and
  outranks the registry value.

### Command line

```bat
pdf_compress.exe <input.pdf> <target MB> [options]
pdf_compress.exe <input.pdf> --dpi 150 [options]
```

```bash
./pdf_compress <input.pdf> <target MB> [options]
./pdf_compress <input.pdf> --dpi 150 [options]
```

Every option takes its value either as `--opt value` or as `--opt=value`. An unrecognised option is an
error, and the program exits with code 2 after telling you to use `--help`.

#### Options

| Option | Description |
|--------|-------------|
| `-o, --output-dir <dir>` | Output directory (default: next to the input file) |
| `--mode {auto,manual}` | `auto` = search automatically (default), `manual` = adjust step by step |
| `--strategy {closest,smaller}` | `closest`: within 5% of the target (default); `smaller`: below the target, within 10% |
| `--max-retries <n>` | Maximum render attempts in auto mode (default: 8; it normally hits the target within 3) |
| `--dpi <value>` | Render once at this fixed resolution (the target size may then be omitted) |
| `--max-dpi <value>` | Upper resolution bound for the search (default: 600) |
| `--min-dpi <value>` | Lower resolution bound for the search (default: 7.2) |
| `-j, --jobs <n>` | Worker processes (default: physical core count; `0` = auto) |
| `--force-render` | Render even when the lossless result is already smaller than the target (makes the file bigger, not better) |
| `--keep-page-box` | Keep the original page size (MediaBox) instead of the crop box |
| `--dry-run` | Show the plan only |
| `--gui` | Force the graphical interface |
| `--lang {zh,en}` | Language for this run only (not saved to settings) |
| `-h, --help` | Show help, using the currently configured language |
| `--help-cn` | Show Chinese help (`--help-zh` is the same thing) |
| `--help-en` | Show English help |
| `--install-menu` | Add the File Explorer context menu entry (Windows only; needs administrator rights; only applies to `.pdf` files) |
| `--uninstall-menu` | Remove the File Explorer context menu entry (Windows only) |
| `-V, --version` | Show the version |

Two further flags exist in the parser but are not part of the documented interface: `--quiet`
suppresses the progress chatter of `--install-menu` / `--uninstall-menu` (this is what the installer
calls: `--install-menu --quiet`), and `--worker` starts the internal render subprocess and is never
meant to be typed by hand.

### Strategies

| Strategy | Acceptance band | Behaviour |
|---|---|---|
| `closest` (default) | `[0.95 × target, 1.05 × target]` | The result may be above or below the target; the search aims straight at the target |
| `smaller` | `[0.90 × target, target)` — the upper bound is exclusive | The result must be strictly below the target. The search aims at the centre of the band (0.95 × target) so it does not get stuck just barely above it |

There is **no third strategy**: `--strategy` accepts only `closest` and `smaller`. `larger` and
`smaller` also appear in *manual mode*, but there they are the answers that mean "raise the
resolution/quality" and "lower the resolution/quality" for the next attempt, not strategies.

With `smaller`, when the best trial is above the target the tool writes the closest result that is still
below it and prints a note about the closer alternative that only `closest` would accept.

### Examples

```bat
rem Compress to roughly 5 MB (auto mode, closest strategy)
pdf_compress.exe mydoc.pdf 5

rem Strictly below 10 MB (within 10%)
pdf_compress.exe mydoc.pdf 10 --strategy smaller

rem Manual mode with a custom output directory
pdf_compress.exe mydoc.pdf 8 --mode manual -o .\output

rem Just 150 DPI, whatever the size
pdf_compress.exe mydoc.pdf --dpi 150

rem Raise the ceiling to 900 DPI when aiming for a larger file
pdf_compress.exe mydoc.pdf 300 --max-dpi 900

rem Render with four processes
pdf_compress.exe mydoc.pdf 20 -j 4

rem Only show the plan, do not write anything
pdf_compress.exe mydoc.pdf 2 --dry-run

rem Open the graphical interface (the same as passing no arguments)
pdf_compress.exe

rem Force the English help
pdf_compress.exe --help-en

rem Install / remove the Windows 11 context menu
pdf_compress.exe --install-menu
pdf_compress.exe --uninstall-menu
```

Linux/macOS equivalents (same options, `./pdf_compress` instead of `pdf_compress.exe`):

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

### Exit codes

| Code | Meaning |
|------|---------|
| `0` | Success (including "closest result output" when the target was not fully reachable, and `--dry-run`) |
| `1` | Failure (missing file, not a PDF, encrypted, could not write the output, no usable result, ...) |
| `2` | Bad arguments — an unknown option, a missing or invalid option value, or neither a target size nor `--dpi` |
| `130` | Interrupted with `Ctrl+C` (the best result produced so far was kept) |

### Output and status markers

Every status line starts with a bracketed marker, which makes logs easy to read and filter; the log
pane in the graphical interface shows the same markers.

| Marker | Meaning |
|--------|---------|
| `[task]` | Task parameters (input, target, strategy, workers) |
| `[lossless]` | Lossless optimisation phase |
| `[render]` | Render progress, and the parameters/result of each attempt |
| `[search]` | The predicted parameters for the next attempt |
| `[ok]` | Output produced as requested |
| `[result]` | Final size, error, compression ratio, number of renders |
| `[warning]` | Strategy not fully met, or a parameter was clamped |
| `[note]` | Informational hint |
| `[interrupted]` | Interrupted; the best result was kept |
| `[failed]` | Failure, exit code 1 |

The output file is named `<name>_compressed.pdf` and is written to the output directory (the input
file's own folder by default).

## Windows 11 File Explorer context menu (Windows only, `.pdf` only)

This is an **optional** feature, and it is driven by its own commands — it is deliberately absent from
the graphical interface:

```bat
pdf_compress.exe --install-menu
pdf_compress.exe --uninstall-menu
```

The setup program has a task for it that is **checked by default**.

The Windows 11 **top-level** menu (the one you see without clicking "Show more options") accepts exactly
one kind of registration: a native COM DLL implementing `IExplorerCommand`, placed inside an **MSIX
sparse package** and declared for `.pdf` through `windows.fileExplorerContextMenus`. The sources live in
`shell/`:

| File | Purpose |
|---|---|
| `shell/ExplorerCommand.cpp` | the COM server implementing `IExplorerCommand` |
| `shell/AppxManifest.xml` | the sparse package manifest: the COM surrogate server plus the `.pdf` context menu registration |
| `shell/build_shell_menu.bat` | builds the DLL with MSVC, packs and signs the `.msix` with `makeappx` / `signtool` |
| `shell/make_assets.py`, `shell/Resources/` | the package tile images |
| `shell/make_payload.py` | turns the `.msix` and `.cer` into `src/shell_payload.cpp` |

Selecting the entry opens the graphical interface with the file you right-clicked; selecting several
files at once brings all of them along. Another `HKCU\Software\Classes\SystemFileAssociations\.pdf\shell`
entry is registered as well, so Windows 10 and the "Show more options" submenu keep working.

- The first install needs administrator rights **only to trust the certificate**, and the program asks
  for elevation by itself; after that it runs unelevated. On a machine installed from the setup, the
  certificate is already in place, so `--install-menu --quiet` raises no UAC prompt.
- The build generates a **self-signed certificate at build time** (`CN=zyq1223334444`, 10 year
  validity, never committed); the installer imports only its **public half** into
  `Cert:\LocalMachine\TrustedPeople` and removes it again on uninstall. To be honest about it: **this is
  a security-relevant change** (a certificate is added to a machine-wide trust store) and it is the
  price of appearing in the top-level menu.
- The package is also **embedded inside the executable** as `src\shell_payload.cpp`, so the portable
  build can install the menu by itself with no extra files next to the exe. If
  `pdf_compress_shell.msix` and `pdf_compress_shell.cer` happen to sit next to the exe, those are used
  instead.
- On Linux/macOS the whole thing is unavailable: `--install-menu` reports that the context menu is only
  supported on Windows.
- If the entry does not appear immediately, restart File Explorer or sign out and back in — the shell
  caches verbs.

## Installer

```bat
build\build_installer.bat
```

Download and run `pdf_compress_setup_2.1.0.exe`, or build it yourself with the command above:

- installs to `C:\Program Files\pdf-compress` with Start Menu shortcuts (including a *Command-line help*
  shortcut that runs `pdf_compress.exe --help`) and a **proper uninstaller** (it also appears under
  Settings → Apps);
- the **language (中文 / English) chosen on the wizard's first page also decides the application's
  default language**: it is written to `HKLM\SOFTWARE\pdf-compress\Language` and drives the command line
  output, `-h`/`--help`, `--help-cn`/`--help-en` and the graphical interface;
- optionally adds the install folder to the system `PATH` (a task that is **checked by default**), so
  `pdf_compress` works from any terminal; the uninstaller **restores `PATH` byte-for-byte** — the
  original value is backed up in the registry before the edit;
- the "Compress with PDF Compressor" context menu task is **checked by default**; the installer first
  imports the certificate into the Trusted People store and then asks the program to register the
  package as the invoking user;
- a desktop shortcut task exists but is **unchecked** by default;
- uninstalling removes the context menu (including the certificate), the install folder's `PATH` entry,
  the Start Menu entries and `HKLM\SOFTWARE\pdf-compress`.

## Configuration

Three pieces of data, from three different sources:

| Where | Who writes it | What is in it |
|---|---|---|
| `%LOCALAPPDATA%\pdf-compress\settings.ini` | the graphical interface (and you, by hand) | language, theme and the last used form values |
| `HKLM\SOFTWARE\pdf-compress` | the installer; readable by everyone, writable only elevated | `Language`, `InstallDir`, `Version`, and `OriginalPath` (the `PATH` backup) |
| `HKCU\Software\pdf-compress` | the portable build, when `--install-menu` runs | `InstallDir`, `Language` |

On Linux/macOS the settings file is `$XDG_CONFIG_HOME/pdf-compress/settings.ini`, falling back to
`~/.config/pdf-compress/settings.ini` (and to a fixed directory under `$TMPDIR`/`/tmp` if even `HOME`
is unset). There is no registry off Windows.

`settings.ini` is a minimal `key=value` file (UTF-8, LF, `#` starts a comment), written atomically — a
temporary file is renamed into place, so a crash cannot leave half a settings file behind. Deleting it
returns the program to its factory state (the language is guessed from the system locale again), and a
broken file is treated as empty: unreadable settings never turn into a startup failure.

| Key | Meaning |
|---|---|
| `language` | `zh` or `en` |
| `theme` | `light` or `dark` |
| `target_size` | last target size in MB used in the window |
| `dpi` | last fixed DPI |
| `output_dir` | last output folder |
| `mode` | `auto` or `manual` |
| `strategy` | `closest` or `smaller` |
| `max_retries` | maximum render attempts |
| `max_dpi`, `min_dpi` | search ceiling and floor |
| `jobs` | worker processes (`0` = auto) |
| `force_render`, `keep_page_box`, `dry_run` | `1` / `0` for the three check boxes |

Precedence:

- **Language**: `--lang` → `settings.ini` → the registry (`HKCU` first, then `HKLM`) → the system
  locale.
- **Everything else**: command line options always win. `settings.ini` only supplies the graphical
  interface's initial values; it never changes the command line defaults.

## How it works

1. **Lossless optimisation** – MuPDF's document writer is run with garbage collection, stream
   deflation, sanitising and cleaning, the exact equivalent of PyMuPDF's
   `save(garbage=4, deflate=True, clean=True)`. The result is measured **in memory**, and if the gain is
   below 1% no copy is written at all (the original file simply becomes the lossless candidate).
   Image-only PDFs typically gain 0%; uncompressed text PDFs can gain a lot.

2. **Decision** – if the lossless result is **already smaller than the target**, the tool neither
   silently gives up nor silently inflates the file:
   - it states the situation clearly;
   - it explains that rendering further **only makes the file bigger and does not improve quality or
     sharpness** (resolution and detail are not gained);
   - in an interactive terminal it asks whether to continue; in a non-interactive run it keeps the
     lossless result unless `--force-render` is given. (In the graphical interface the same question is
     an inline prompt row, not a popup.)

3. **Render search** – if the lossless result is larger than the target:
   - one calibration render at 72 DPI;
   - the measured points are interpolated/extrapolated in log-log space along `size ∝ scale^p` to
     **jump to the target**;
   - JPEG quality (50–100, default 85) is only used as a secondary knob once the scale hits a bound;
   - the `smaller` strategy aims at the centre of the acceptance band (0.95 × target) so it does not get
     stuck just barely above the target;
   - if the target cannot be reached at all, the achievable range is reported and the search stops
     early.

4. **Multi-process rendering** – pages are split into contiguous chunks (at most one chunk per worker,
   no tiny tail chunks), rendered by `--worker` subprocesses in parallel and merged in order. Documents
   with ≤4 pages (or `-j 1`) use the sequential path.

Why the output does not overshoot the target by much:

- `closest` accepts ±5% and `smaller` never goes above the target at all, so the acceptance band itself
  is narrow;
- the search aims at the **centre** of that band rather than at its edge;
- the `size ∝ scale^p` interpolation is built from measured points of *this* document, and the measured
  slope is clamped to 1.2–3.0 so one freak measurement cannot throw the next guess far off;
- when the target turns out to be unreachable, the tool stops and reports the achievable range instead
  of burning attempts;
- and when the attempt budget runs out, the best trial — the one whose size is closest to the target — is
  still written out.

## Troubleshooting

**CMake says it cannot find MuPDF.**
CMake stops with a readable message when neither the headers nor the library are found. Fix it with one
of: `build\build_mupdf.bat` on Windows; the distribution package on Linux
(`sudo apt-get install -y libmupdf-dev`) or Homebrew on macOS (`brew install mupdf`); or point CMake at
your own build with `-DMUPDF_ROOT=<prefix>` (the prefix must contain `include/mupdf/fitz.h` and the
library). `build_exe.bat` prints the same command in its error paths. Note that a `third_party\mupdf`
holding only the headers is a partial copy — re-run the script.

**The context menu entry does not appear.**
It is optional and installed by a separate command: run `pdf_compress.exe --install-menu`. Then:

- restart File Explorer or sign out and back in — the shell caches verbs;
- the first install needs administrator rights to put the self-signed certificate into the Trusted People
  store; without them, registration fails and says so. Installing with the setup program avoids the
  prompt, because the installer trusts the certificate first;
- make sure the exe you run actually contains the context menu component. A build made without
  `shell\build_shell_menu.bat` (or one where that step failed) reports *"this build does not include the
  context menu"*;
- Windows 10 before 1809 does not support sparse packages; the classic `HKCU` entry registered alongside
  it still works and gives you the "Show more options" submenu.

**Antivirus software flags the executable.**
The executable is built locally with MSVC and is **not code-signed** (only the MSIX context menu package
is signed, and with a self-signed certificate), so heuristic scanners can flag a freshly compiled binary
they have never seen. It is a false positive from the scanner's point of view: the program is a plain
Win32 application that renders PDF pages and writes a new file, and everything it does is readable in
`src/`. If you would rather not take that on trust, build it yourself from this checkout
(`build\build_exe.bat`) and compare, or add an exclusion for the install folder. Be aware of the one
genuinely security-relevant action: `--install-menu` adds a self-signed certificate to the machine-wide
Trusted People store, which is exactly the kind of thing security software comments on.

**What happens if I give both `--dpi` and a target size?**
The fixed resolution wins: the tool renders **once** at that DPI, prints a note that it is ignoring the
target size, and writes the result. It does not search for the target at all.

**Where is the output file?**
In the output directory (`-o` / `--output-dir`, or the graphical interface's output folder), named
`<name>_compressed.pdf`; with no output directory given it lands next to the input file. Temporary files
go into a hidden per-run directory (`.pdf_compress_*`) created in the output directory, and they are
removed on normal completion, on failure and on interruption alike.

**`Ctrl+C` — what gets kept?**
The best result produced so far is written to the normal output path, every temporary file is cleaned up
and the exit code is 130.

**There is no graphical interface on my system.**
The window is Win32 + Direct2D, so it exists on Windows only. On Linux/macOS the build is the command
line tool; `--gui` reports that this build has no graphical interface and asks you to use the command
line.

## Project layout

Everything a build *produces* lives in one ignored folder, so a checkout stays clean:

```
CMakeLists.txt            CMake 3.16+, C++20, the MuPDF lookup, the targets and the tests
readme.md / readme.zh.md  these documents
LICENSE                   MIT

src/                      the program
├── main.cpp              entry point (wWinMain on Windows, main elsewhere)
├── cli.cpp / cli.hpp     argument parsing, the help texts, entry dispatch
├── options.cpp / .hpp    JobOptions, validation, worker count
├── engine.cpp / .hpp     the compression pipeline (no UI knowledge at all)
├── search.cpp / .hpp     the target-size search (pure maths, unit tested)
├── pdf.cpp / .hpp        the MuPDF wrapper: probe, lossless save, render, merge
├── worker.cpp / .hpp     the --worker render subprocess
├── process.cpp / .hpp    child process handling
├── config.cpp / .hpp     settings.ini, the registry, language choice
├── i18n.cpp / .hpp       language switching; Chinese is the source language
├── strings_en.cpp        the English translation table
├── strings_en_extra.cpp  the English strings the generator leaves out
├── filedialog.cpp / .hpp native file/folder dialogs per platform
├── shellmenu.cpp / .hpp  install/uninstall of the Explorer context menu
├── shell_payload.hpp / shell_payload_default.cpp   the embedded MSIX payload
├── platform_win.cpp / platform_posix.cpp           per-platform helpers
├── resource.h
└── gui/                  the Windows 11 style interface
    ├── window.cpp / .hpp the window, layout and interaction
    ├── widgets.cpp / .hpp  buttons, check boxes, drop-downs, the file list
    ├── theme.cpp / .hpp    the light/dark palettes
    ├── gui.cpp / .hpp      the GUI entry point
    └── gui_stub.cpp        the non-Windows stub (there is no GUI off Windows)

assets/                   the application icon and the script that draws it
shell/                    the COM DLL and the MSIX sparse package (Windows only)
build/                    the build scripts; all output in build/out/
├── package.bat           the three Windows release builds -> dist\windows\
├── build_exe.bat         the Windows executable (calls shell\build_shell_menu.bat first)
├── build_mupdf.bat       downloads/builds MuPDF into third_party\mupdf
├── build_installer.bat   stages the payload and compiles installer.iss
├── build_unix.sh         the Linux/macOS build -> dist/<platform>/
├── check_compile.bat     compile-only syntax check
├── installer.iss         the Inno Setup script
├── ChineseSimplified.isl the Simplified Chinese texts for the installer
├── app.rc                the icon and version resource
└── out/                  every build product (gitignored)
dist/                     the release folders (gitignored; written by package.bat and build_unix.sh)
tests/                    test_search.cpp, test_i18n.cpp
tools/                    fetch_mupdf.py, check_i18n.py, make_strings_en.py
third_party/mupdf/        the MuPDF build product (gitignored; build\build_mupdf.bat)
.github/workflows/build.yml  CI: Linux + both macOS architectures
```

`.gitignore` excludes only build products and runtime leftovers: `build/out/`, `dist/`, `third_party/`,
the generated `src/shell_payload.cpp`, CMake intermediates, object files and libraries, `__pycache__/`
(the Python helper scripts), the temp directory `.pdf_compress_*/` and the default output
`*_compressed.pdf`. The scripts themselves are tracked, so anyone can rebuild the binaries from a
checkout.

## Changelog

### v2.1 (C++20 rewrite)

- Rewritten in C++20 with CMake; MuPDF is used directly through its C API, and the compression pipeline
  keeps the step order and the search maths of the Python version.
- The graphical interface is now native Win32 + Direct2D instead of Tkinter; it is no longer an extra
  dependency, and it is not built on Linux/macOS (those builds are command line only).
- One self-contained executable: the MuPDF DLLs are staged next to it when MuPDF is a DLL build, and
  there is no Python runtime and no per-launch unpacking.
- The Windows 11 top-level context menu component is embedded into the executable (as
  `src/shell_payload.cpp`, generated at build time), so the portable build can install the menu on its
  own; the MSIX package and its certificate next to the exe take priority over the embedded copy.
- Two unit-test executables (`pdfc_search_tests`, `pdfc_i18n_tests`) plus the `tools/check_i18n.py`
  missing-translation check.
- The setup wizard's language page still decides the application's default language
  (`HKLM\SOFTWARE\pdf-compress\Language`), and `--lang`, `--help-cn` (`--help-zh`) and `--help-en`
  remain.

### v2.1 (Python implementation)

- Added the graphical interface (no arguments opens it), the Windows 11 top-level context menu, the
  installer's language page driving the application language, and `--lang` / `--help-cn` / `--help-en`.

### v2.0

- Fixed: redirected output raised a Unicode encoding error and reported a successful compression as a
  failure.
- Fixed: the search gave up whenever the lossless result was already smaller than the target.
- Fixed: an invalid character in manual mode re-rendered the whole document.
- Fixed: `scale` also changed the physical page size; it now only affects the DPI.
- Fixed: workers were spawned per logical core; now physical cores are used.
- Fixed: interrupting exited with code 0 (now 130).
- Added: a 600 DPI ceiling (`--max-dpi`), fixed-resolution rendering (`--dpi`), `-j/--jobs`,
  `--force-render`, `--keep-page-box`, a per-page pixel guard and render progress output.

## License

MIT — see [LICENSE](LICENSE). Copyright (c) 2026 zyq1223334444.
