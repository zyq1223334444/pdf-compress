[🇨🇳 中文](README.zh-CN.md) · [⬇ Download](https://github.com/zyq1223334444/pdf-compress/releases)

# PDF Compressor

**Tell it how big you want the PDF to be — it finds the settings that land there.**

You give it a target size in MB, it gives you a file of about that size. A C++20 / CMake program
built on [MuPDF](https://mupdf.com/), with three faces:

| | |
|---|---|
| **Command line** | `pdf_compress input.pdf 5` — aim at 5 MB. Windows, Linux and macOS |
| **Graphical interface** | Windows only. Hand-drawn Win32 + Direct2D, no GUI toolkit, no runtime to install |
| **Explorer context menu** | Windows 11 top-level menu for `.pdf` files — not hidden under *Show more options* |

Two kinds of compression are combined: **lossless optimisation** (a cleaned-up, repacked copy) and
**lossy rendering** (every page re-rendered as JPEG at a resolution the search chooses). The tool
prefers the lossless result and only renders when it has to.

## Features

- **Lossless first** — unused objects are dropped and streams rebuilt. The gain is measured in memory,
  and anything below 1% is not written out at all.
- **Lossy rendering when needed** — pages become JPEG images; file size is controlled through resolution
  (DPI) and JPEG quality.
- **Smart parameter search** — one calibration render at 72 DPI, then the measured `size ∝ scale^p`
  relationship is used to jump at the target. It usually lands in **2–3 renders**.
- **It says when the target is impossible** — you get the range that *is* achievable instead of a pile
  of futile attempts.
- **Two strategies** — `closest` (within ±5% of the target, above or below) or `smaller` (strictly
  below, within 10%).
- **Manual mode** — step through the attempts yourself, with a hint about the DPI that would hit the
  target.
- **Multi-process rendering** — pages are split across the **physical** cores (hyper-threading is
  measurably slower here); `-j` overrides it.
- **Page geometry preserved** — DPI changes the pixel count, not the paper size.
- **Memory guard** — at most 40 Mpx per rendered page (≈120 MB per worker); bigger pages are scaled
  down with a warning.
- **Interrupt safe** — `Ctrl+C` keeps the best result produced so far, deletes every temporary file and
  exits with code 130.
- **Bilingual** — Chinese and English, for the window, the help and every message.
- **Zero installation** — the released executable is one self-contained file: MuPDF and the context
  menu payload are compiled in.

## Download

Grab what you need from the [Releases](https://github.com/zyq1223334444/pdf-compress/releases) page.
Binaries are built by CI and attached there — they are deliberately **not** committed to the repository.

| Platform | Asset | Notes |
|---|---|---|
| Windows | `pdf_compress_<ver>_win64.exe` | single file; run it, nothing to unpack |
| Windows | `pdf-compress-<ver>-win64.zip` | the same executable plus the two READMEs and the licence |
| Windows | `pdf_compress_setup_<ver>.exe` | installer: Start Menu entries, `PATH`, uninstaller, context menu |
| Linux | `pdf-compress_<ver>_linux_x86_64.tar.gz` | the command line tool |
| macOS | `pdf-compress_<ver>_macos_x86_64.tar.gz`, `..._macos_arm64.tar.gz` | Intel and Apple silicon, command line only |

The executable in the Windows packages already contains MuPDF and the context menu component, so it
runs on its own from anywhere. Windows SmartScreen will warn about an unsigned binary — see
[Troubleshooting](#troubleshooting).

## Quick start

```bat
rem Windows: aim at roughly 5 MB
pdf_compress.exe report.pdf 5

rem Windows: open the graphical interface (that is also what no arguments does)
pdf_compress.exe
pdf_compress.exe report.pdf invoice.pdf

rem Add the Explorer context menu (.pdf files only, asks for elevation once)
pdf_compress.exe --install-menu
```

```bash
# Linux / macOS: exactly the same options
./pdf_compress report.pdf 5
./pdf_compress --help
```

The output is written next to the input file as `<name>_compressed.pdf` unless `-o` says otherwise.

## Command line

```
pdf_compress <input.pdf> <target MB> [options]
pdf_compress <input.pdf> --dpi 150 [options]
```

Options take their value as `--opt value` or `--opt=value`. An unknown option is an error: the program
explains and exits with code 2.

| Option | Meaning |
|---|---|
| `-o, --output-dir <dir>` | output directory (default: next to the input file) |
| `--mode {auto,manual}` | `auto` = search automatically (default), `manual` = adjust step by step |
| `--strategy {closest,smaller}` | `closest`: nearest to the target (default); `smaller`: strictly below it |
| `--max-retries <n>` | maximum renders in auto mode (default 8; it normally hits the target in 3) |
| `--dpi <value>` | render once at this resolution; the target size may then be omitted |
| `--max-dpi <value>` | search ceiling (default 600) |
| `--min-dpi <value>` | search floor (default 7.2) |
| `-j, --jobs <n>` | worker processes (default: physical cores; `0` = auto) |
| `--force-render` | render even though the lossless result is already smaller than the target |
| `--keep-page-box` | keep the original page size (`MediaBox`) instead of the crop box |
| `--dry-run` | print the plan and write nothing |
| `--gui` | force the graphical interface |
| `--lang {zh,en}` | language for this run only (not saved) |
| `-h, --help` | help, in the currently configured language |
| `--help-cn`, `--help-en` | force Chinese (`--help-zh` is the same thing) or English help |
| `--install-menu` / `--uninstall-menu` | add / remove the Explorer context menu (Windows only, needs administrator rights) |
| `-V, --version` | version number |

Two more flags exist for internal use: `--quiet` suppresses the progress output of
`--install-menu` (the installer calls `--install-menu --quiet`), and `--worker` starts the render
subprocess — never type that one by hand.

### Strategies

| Strategy | Accepted range | Behaviour |
|---|---|---|
| `closest` (default) | `0.95 × target` … `1.05 × target` | may end up above or below; the search aims straight at the target |
| `smaller` | `0.90 × target` … below `target` | must stay strictly below; the search aims at the middle of the band so it does not get stuck barely above the target |

With `smaller`, if the best attempt is above the target, the closest result still below it is written
and a note mentions the nearer alternative that only `closest` would accept. (In *manual* mode
`larger`/`smaller` are answers to "what next?", not strategies.)

### Examples

```bat
pdf_compress.exe report.pdf 5                      rem about 5 MB
pdf_compress.exe report.pdf 10 --strategy smaller  rem strictly below 10 MB
pdf_compress.exe report.pdf 8 --mode manual -o out rem step through it yourself
pdf_compress.exe report.pdf --dpi 150              rem fixed resolution, size follows
pdf_compress.exe report.pdf 300 --max-dpi 900      rem allow a bigger search ceiling
pdf_compress.exe report.pdf 20 -j 4                rem four render processes
pdf_compress.exe report.pdf 2 --dry-run            rem show the plan only
```

### Exit codes

| Code | Meaning |
|---|---|
| `0` | success (also for `--dry-run`, and when the closest reachable result was written) |
| `1` | failure (missing file, not a PDF, encrypted, cannot write, no usable result, …) |
| `2` | bad arguments (unknown option, bad value, neither a target size nor `--dpi`) |
| `130` | interrupted with `Ctrl+C`; the best result so far was kept |

### Status markers

Every status line starts with a bracketed marker, so logs are easy to filter; the window's log pane
shows the same markers.

| Marker | Meaning |
|---|---|
| `[task]` | the parameters of this run (input, target, strategy, workers) |
| `[lossless]` | the lossless phase |
| `[render]` | render progress and the parameters/result of each attempt |
| `[search]` | the parameters predicted for the next attempt |
| `[ok]` / `[result]` | output written; final size, error, ratio, number of renders |
| `[warning]` / `[note]` | strategy not fully met, a value was clamped, or a hint |
| `[interrupted]` / `[failed]` | interrupted (best result kept) / failed |

## Graphical interface (Windows)

```
pdf_compress.exe                 open the window
pdf_compress.exe report.pdf      open it with this file queued
```

- **Files card** — *Add PDF files* uses the operating system's own dialog (`*.pdf`, multiple
  selection); *Remove selected* and *Clear* manage the queue; double-clicking a row opens its folder.
  The same file cannot be queued twice. Files are compressed one after another, each row carrying its
  own status and result.
- **Settings card** — every command line option is here: target size, fixed DPI, output folder with a
  folder picker, mode, strategy, maximum attempts, worker processes, search ceiling and floor, and the
  *force render*, *keep page size* and *plan only* check boxes.
- **Progress and log card** — progress bar, live log and a status line (*ready*, *rendering 3/12
  pages*, *cancelling…*, *done: 2 succeeded, 1 failed*). **Cancel** stops after the chunk being
  rendered and keeps the best result.
- Questions the command line would ask on stdin (*"the lossless result is already smaller than the
  target, render anyway?"*) become an inline prompt row with buttons, not a modal dialog.
- The header has **中文 / English** buttons and a light/dark theme toggle; both are remembered.
- Installing the context menu is deliberately command line only.

### Language

The language is chosen in this order:

1. `--lang` on the command line (this run only);
2. the choice made in the window, stored in `settings.ini`;
3. the registry — `HKCU\Software\pdf-compress\Language` for a portable install, otherwise
   `HKLM\SOFTWARE\pdf-compress\Language`, written by the installer's wizard;
4. the system locale (`zh*` → Chinese, everything else → English).

`-h`/`--help` follow the configured language; `--help-cn` and `--help-en` override it for one run.

## Explorer context menu (Windows only, `.pdf` files only)

```bat
pdf_compress.exe --install-menu      rem needs administrator rights the first time
pdf_compress.exe --uninstall-menu
```

The installer has a task for this, **checked by default**. Selecting the entry opens the window with
the file you right-clicked (several files at once bring all of them along).

The Windows 11 *top-level* menu accepts exactly one kind of registration: a native COM DLL
implementing `IExplorerCommand`, shipped inside an **MSIX sparse package** that declares
`windows.fileExplorerContextMenus` for `.pdf`. That is what `shell/` holds:

| File | Purpose |
|---|---|
| `shell/ExplorerCommand.cpp` | the COM server implementing `IExplorerCommand` |
| `shell/AppxManifest.xml` | the sparse package manifest (COM surrogate server + the `.pdf` menu) |
| `shell/make_assets.py`, `shell/Resources/` | the package tile images |
| `shell/make_payload.py` | turns the `.msix` and `.cer` into `src/shell_payload.cpp` |
| `build/windows/build_shell_menu.bat` | compiles the DLL, packs and signs the `.msix` |

A classic `HKCU\Software\Classes\SystemFileAssociations\.pdf\shell` verb is registered alongside it, so
Windows 10 and the *Show more options* submenu keep working.

- The **first** install needs administrator rights only to trust the certificate, and the program asks
  for elevation itself; afterwards it runs unelevated. On a machine installed from the setup, the
  certificate is already trusted and `--install-menu --quiet` raises no UAC prompt.
- The build creates a **self-signed certificate at build time** (`CN=zyq1223334444`, 10 years, never
  committed). The installer imports its **public half** into `Cert:\LocalMachine\TrustedPeople` and
  removes it again on uninstall. Being straight about it: this *is* a security-relevant change — a
  certificate lands in a machine-wide trust store — and it is the price of a top-level menu entry.
- The package is **embedded in the executable** as `src/shell_payload.cpp`, so the portable build can
  install the menu by itself. If `pdf_compress_shell.msix` and `.cer` sit next to the exe, those win.
- On Linux/macOS `--install-menu` reports that the context menu is Windows-only.
- If the entry does not appear, restart File Explorer or sign out and back in: the shell caches verbs.

## Installer (Windows)

Run `pdf_compress_setup_<ver>.exe`, or build it yourself with `build\windows\build_installer.bat`:

- installs into `C:\Program Files\pdf-compress`, with Start Menu entries (including a *Command-line
  help* shortcut that runs `pdf_compress.exe --help`) and a proper uninstaller that also shows up under
  Settings → Apps;
- the **language chosen on the wizard's first page becomes the application default** — it is written to
  `HKLM\SOFTWARE\pdf-compress\Language` and drives the command line output, the help and the window;
- can add the install folder to the system `PATH` (**checked by default**), so `pdf_compress` works from
  any terminal. The uninstaller restores `PATH` byte-for-byte from the backup it took before editing;
- the context menu task is **checked by default**; the installer trusts the certificate and then asks
  the program to register the package for the invoking user;
- the desktop shortcut task exists but is **unchecked**;
- uninstalling removes the context menu (and the certificate), the `PATH` entry, the Start Menu entries
  and `HKLM\SOFTWARE\pdf-compress`.

## Configuration

| Where | Written by | Contents |
|---|---|---|
| `%LOCALAPPDATA%\pdf-compress\settings.ini` | the window (and you) | language, theme, last used form values |
| `HKLM\SOFTWARE\pdf-compress` | the installer | `Language`, `InstallDir`, `Version`, `OriginalPath` (the `PATH` backup) |
| `HKCU\Software\pdf-compress` | a portable install running `--install-menu` | `InstallDir`, `Language` |

Off Windows the settings file is `$XDG_CONFIG_HOME/pdf-compress/settings.ini`, falling back to
`~/.config/pdf-compress/settings.ini` (and to a fixed directory under `$TMPDIR` if even `HOME` is
unset). There is no registry outside Windows.

`settings.ini` is a minimal `key=value` file (UTF-8, LF, `#` starts a comment) written atomically: a
temporary file is renamed into place, so a crash cannot leave half a file behind. Deleting it returns
the program to its default state, and an unreadable file is treated as empty — broken settings never
become a startup failure.

Keys: `language`, `theme`, `target_size`, `dpi`, `output_dir`, `mode`, `strategy`, `max_retries`,
`max_dpi`, `min_dpi`, `jobs`, and `force_render`, `keep_page_box`, `dry_run` as `1`/`0`.

Command line options always win over everything; `settings.ini` only supplies the window's initial
values.

## How it works

1. **Lossless optimisation.** MuPDF's document writer runs with garbage collection, stream deflation
   and sanitising. The result is measured *in memory*, and if the gain is below 1% no copy is written
   at all — the original file simply becomes the lossless candidate. Image-only PDFs typically gain
   nothing here; uncompressed text PDFs can gain a lot.

2. **Decision.** If the lossless result is *already smaller than the target*, the tool neither gives up
   silently nor inflates the file silently: it says so, explains that rendering further only makes the
   file larger without adding detail, and asks (interactive terminal, or an inline prompt row in the
   window). Non-interactive runs keep the lossless result unless `--force-render` is given.

3. **Render search.** Otherwise: one calibration render at 72 DPI, then interpolation and
   extrapolation in log-log space along `size ∝ scale^p` to jump at the target. JPEG quality (50–100,
   default 85) is the secondary knob once the scale hits a bound. `smaller` aims at 0.95 × target so it
   does not get stuck just above it. An unreachable target is reported instead of retried forever.

4. **Multi-process rendering.** Pages are split into contiguous chunks (at most one per worker, no
   tiny tail chunks), rendered by `--worker` subprocesses in parallel and merged in order. Documents
   with ≤4 pages, or `-j 1`, use the sequential path.

Why the result does not overshoot much: the acceptance band is narrow, the search aims at its
*centre*, the `size ∝ scale^p` fit is built from measurements of *this* document (with the slope
clamped to 1.2–3.0 so one freak measurement cannot throw the next guess away), an impossible target
stops the search early, and when the attempt budget runs out the best trial is still written.

## Building

| Component | Requirement |
|---|---|
| PDF engine | **MuPDF ≥ 1.25** — needed to build; the released binaries have it compiled in |
| Compiler (Windows) | **MSVC 2022** (Visual Studio 2022 or the Build Tools) with *Desktop development with C++* |
| Compiler (Linux/macOS) | **GCC** or **Clang** with C++20 support |
| Build system | **CMake ≥ 3.16** |
| Installer (optional) | **Inno Setup 6.5+** (tested with 6.7.3) |
| Context menu (optional, Windows) | Windows 10/11 SDK (`makeappx.exe`, `signtool.exe`) and Python (for `shell/*.py`) |

### Getting MuPDF

CMake looks for it as `-DMUPDF_ROOT=<prefix>` → the `MUPDF_ROOT` environment variable → the system
paths / `pkg-config` → `third_party/mupdf/` in the checkout.

| Platform | Command |
|---|---|
| Windows | `build\windows\build_mupdf.bat` → `third_party\mupdf` (vcpkg when installed, otherwise the source tarball from mupdf.com built with `platform\win32\libmupdf.vcxproj`) |
| Linux / macOS | `bash build/unix/build_mupdf_unix.sh` → the pinned 1.26.0 in `build/out/mupdf-unix-prefix`, statically linked |
| Debian/Ubuntu | `sudo apt-get install -y build-essential cmake libmupdf-dev` |
| Fedora | `sudo dnf install -y gcc-c++ cmake mupdf-devel` |
| Arch | `sudo pacman -S --needed base-devel cmake mupdf` |
| macOS | `brew install cmake mupdf` (plus the Xcode command line tools) |
| Your own build | `-DMUPDF_ROOT=<prefix>` (a prefix with `include/` and `lib/`) |

Both scripts take a version argument and are safe to re-run — an existing build is left alone.

> **MuPDF 1.25 or newer is required.** The distribution packages lag behind: Ubuntu 24.04 still ships
> 1.23, whose JPEG encoder takes three arguments where these sources pass four. That is why
> `build/unix/build_mupdf_unix.sh` exists: it builds the same 1.26.0 the Windows build uses, and links
> it statically so no `libmupdf.so` has to travel next to the binary.

### Windows

```bat
build\windows\build_mupdf.bat     rem once per machine: MuPDF -> third_party\mupdf
build\windows\build_exe.bat       rem the context menu package first, then CMake -> build\out\pdf_compress.exe

rem optional
build\windows\check_compile.bat   rem compile-only syntax check (needs the headers only)
build\windows\package.bat         rem all three release builds -> dist\windows\
build\windows\build_installer.bat rem just the installer
build\windows\build_shell_menu.bat rem just the context menu package (build_exe.bat calls it first)
```

Manual CMake works the same way if you prefer:

```bat
cmake -S . -B build\out\cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build\out\cmake --config Release
```

### Linux / macOS

```bash
bash build/unix/build_mupdf_unix.sh                                  # once: MuPDF -> build/out/mupdf-unix-prefix
MUPDF_ROOT="$PWD/build/out/mupdf-unix-prefix" bash build/unix/build_unix.sh
```

`build_unix.sh` finds the repository root itself, keeps every CMake intermediate under
`build/out/cmake_unix/`, ends with a smoke test (`--help` must run) and writes the release folder to
`dist/<platform>/pdf-compress_<ver>_<os>_<arch>/`. There is no archive: on Linux and macOS the folder
*is* the normal shape of a release. It also warns when the system MuPDF is older than the sources need.

> **There is no graphical interface on Linux/macOS** — the window is Win32 + Direct2D, and
> `src/gui/gui_stub.cpp` takes its place everywhere else. `--gui` says so instead of failing.

### Tests

CMake builds two test executables — `pdfc_search_tests` (the search maths, no MuPDF needed) and
`pdfc_i18n_tests` (the string tables) — registered as `search` and `i18n`:

```bat
cmake --build build\out\cmake --config Release
ctest --test-dir build\out\cmake -C Release
```

```bash
cmake --build build/out/cmake_unix
ctest --test-dir build/out/cmake_unix
```

`python tools/check_i18n.py` extracts every `tr("...")` literal from the sources and compares it with
the English table, so a missing translation cannot rot unnoticed.

### Packaging and releases

```bat
build\windows\package.bat
```

writes `dist\windows\` with the three Windows builds (single file, folder, folder zip) and the
installer. CI does the same for Linux and macOS — see `.github/workflows/build.yml`, which builds the
pinned MuPDF (cached per runner), runs `build/unix/build_unix.sh` on real machines and attaches the
result to the release when a tag such as `v2.1.0` is pushed. **`dist/` is gitignored**: release
artefacts are attached to GitHub Releases, never committed.

## Project layout

| Path | What lives there |
|---|---|
| `src/` | all C++ sources, plus `src/gui/` (Win32 + Direct2D) and `src/app.rc` (icon and version resource) |
| `shell/` | the Windows 11 context menu component: COM server, MSIX manifest, tile images, payload helper |
| `tests/` | the two test programs |
| `tools/` | Python helpers: i18n check, English table generator, header fetcher |
| `assets/` | the application icon and the script that draws it |
| `build/windows/` | `build_exe.bat`, `build_mupdf.bat`, `package.bat`, `build_installer.bat`, `check_compile.bat`, `build_shell_menu.bat` |
| `build/unix/` | `build_unix.sh`, `build_mupdf_unix.sh` |
| `build/installer/` | `installer.iss` and the Simplified Chinese language file |
| `build/out/` | every intermediate (gitignored) |
| `dist/` | the release folders (gitignored) |

## Troubleshooting

**CMake cannot find MuPDF.** It stops with a readable message when neither headers nor library are
found. Fix it with `build\windows\build_mupdf.bat` on Windows, `build/unix/build_mupdf_unix.sh` or a
distribution package on Linux/macOS, or point CMake at your own prefix with `-DMUPDF_ROOT=<prefix>`.
A `third_party\mupdf` holding only the headers is a partial copy — re-run the script.

**The context menu entry does not appear.** It is optional and installed separately:
`pdf_compress.exe --install-menu`. Then restart File Explorer or sign out and back in (the shell caches
verbs); make sure the first install got administrator rights for the certificate; and check that the
executable actually contains the component — a build made without
`build\windows\build_shell_menu.bat` reports *"this build does not include the context menu"*.

**Antivirus flags the executable.** It is a locally built, **unsigned** Win32 binary, so heuristic
scanners can flag a file they have never seen before. Everything it does is readable in `src/` — build
it yourself with `build\windows\build_exe.bat` if you would rather not take that on trust, or add an
exclusion for the install folder. The one genuinely security-relevant action is documented above:
`--install-menu` adds a self-signed certificate to the machine-wide Trusted People store.

**What if I pass both `--dpi` and a target size?** The fixed resolution wins: it renders once at that
DPI, notes that the target is being ignored, and writes the result without searching.

**Where does the output go?** `<name>_compressed.pdf` in the output directory (next to the input by
default). Temporary files live in a hidden per-run `.pdf_compress_*` directory and are removed on
success, failure and interruption alike.

**`Ctrl+C` — what is kept?** The best result so far is written to the normal output path, all
temporary files are removed, and the exit code is 130.

**Can I compress several files at once?** The command line takes one input per run; the window queues
as many as you like and works through them one after another. The context menu passes a whole
selection to the window.

## Changelog

- **v2.1 — the C++ rewrite (current).** One native executable instead of a Python runtime, a native
  Win32 + Direct2D window instead of Tkinter, a manifest-free installer, and the same parameter search
  ported line by line from the prototype.
- **v2.1 — Python implementation.** The prototype this repository grew out of. It no longer exists in
  the tree; the commit that replaced it (*"feat!: replace the Python implementation with the C++
  rewrite"*) still holds it in the history, for example
  `git show <that commit>^:pdfcomp/core.py`.
- **v2.0** — the first version with the target-size search.

## License

[MIT](LICENSE) © 2026 zyq1223334444

MuPDF, the PDF engine, is © Artifex Software and dual-licensed under the
[AGPL](https://www.gnu.org/licenses/agpl-3.0.html) or a commercial licence. The released binaries link
it statically; if the AGPL is a problem for your use, build from source against a MuPDF you are
licensed for.
