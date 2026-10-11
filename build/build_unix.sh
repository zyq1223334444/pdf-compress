#!/usr/bin/env bash
# =====================================================================
#  PDF Compressor -- Linux / macOS build (C++ / CMake)
#
#  Run *inside the target OS* (a container, a real Mac, CI):
#      bash build/build_unix.sh
#
#  Produces, under <repo>/dist/<platform>/:
#      pdf-compress_<ver>_<os>_<arch>/   the folder build
#          pdf_compress                  the executable
#          readme.md, readme.zh.md, LICENSE
#          <libmupdf.so...>              any non-system shared library it
#                                        loads (a shared MuPDF build only)
#
#  No archive: Windows ships a zip because a Windows folder is awkward to hand
#  around, but on Linux and macOS the folder itself is the normal shape of a
#  release - tar it yourself if you want one.
#
#  ---------------------------------------------------------------------
#  Directory contract. This script lives in build/, keeps every intermediate
#  under build/out/, and writes the release folder to dist/. It resolves the
#  repository root itself (one level up), so it can be started from any
#  working directory.
#
#      build/          this script, installer.iss, the language file
#      build/out/      every CMake intermediate
#      dist/           the release folder (gitignored, like on Windows)
#      <repo root>     CMakeLists.txt, src/, readme*.md
#
#  ---------------------------------------------------------------------
#  Dependencies
#
#      Debian/Ubuntu: sudo apt-get install -y build-essential cmake libmupdf-dev
#      Fedora:        sudo dnf install -y gcc-c++ cmake mupdf-devel
#      Arch:          sudo pacman -S --needed base-devel cmake mupdf
#      macOS:         brew install cmake mupdf   (plus the Xcode command line tools)
#
#  MuPDF has to come from one of those packages (or from a prefix you point at
#  with MUPDF_ROOT); unlike the Python build there is no wheel that bundles the
#  engine. CMakeLists.txt stops with a readable message when it cannot find one.
#
#  ---------------------------------------------------------------------
#  Why CI builds these and not the Windows machine: a Linux binary must be
#  built on Linux and a macOS binary on macOS. See
#  .github/workflows/build.yml, which runs this very script on real machines
#  and attaches the results to a release when a tag is pushed.
# =====================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # .../build
ROOT="$(cd "$HERE/.." && pwd)"                         # repository root
BUILD="$HERE/out/cmake_unix"

cd "$ROOT"

# 版本号从 CMakeLists.txt 里读，和 Windows 的 package.bat 用同一个来源，
# 免得两边各写一份再慢慢对不上。
VERSION="$(sed -n 's/^project(pdf-compress VERSION \([0-9][0-9.]*\).*/\1/p' \
           "$ROOT/CMakeLists.txt" | head -1)"
VERSION="${VERSION:-0.0.0}"

if [ ! -f "$ROOT/CMakeLists.txt" ]; then
    echo "[ERR] CMakeLists.txt not found at $ROOT"
    exit 1
fi

if ! command -v cmake > /dev/null 2>&1; then
    echo "[ERR] cmake is not installed."
    echo "      Debian/Ubuntu: sudo apt-get install -y cmake"
    echo "      macOS:         brew install cmake"
    exit 1
fi

case "$(uname -s)" in
    Linux)  PLATFORM=linux ;;
    Darwin) PLATFORM=macos ;;
    *) echo "[ERR] unsupported OS: $(uname -s)"; exit 1 ;;
esac
case "$(uname -m)" in
    x86_64|amd64)  ARCH=x86_64 ;;
    arm64|aarch64) ARCH=arm64 ;;
    *) ARCH="$(uname -m)" ;;
esac
TAG="${PLATFORM}_${ARCH}"

# Optional: point CMake at a MuPDF prefix that is not in the default search path.
MUPDF_ARG=()
if [ -n "${MUPDF_ROOT:-}" ]; then
    MUPDF_ARG=(-DMUPDF_ROOT="$MUPDF_ROOT")
    echo "[*] MuPDF prefix : $MUPDF_ROOT"
fi

echo "[*] repo root    : $ROOT"
echo "[*] version      : $VERSION"
echo "[*] target       : $PLATFORM / $ARCH"
echo "[*] cmake        : $(cmake --version | head -1)"

# The release folder. Named the same way as the Windows one so that a user who
# downloads both can tell them apart at a glance.
OUT="$ROOT/dist/$PLATFORM"
STAGE="$OUT/pdf-compress_${VERSION}_${TAG}"

# A MuPDF that CMake cannot find is by far the most common failure here, so
# say what to install before CMake gets the chance to fail cryptically.
MUPDF_HINT=""
if [ ${#MUPDF_ARG[@]} -eq 0 ]; then
    if pkg-config --exists mupdf 2>/dev/null; then
        MUPDF_HINT="pkg-config: $(pkg-config --modversion mupdf)"
    elif [ -f /usr/include/mupdf/fitz.h ] || [ -f /usr/local/include/mupdf/fitz.h ]; then
        MUPDF_HINT="system headers found"
    else
        MUPDF_HINT="not found - install libmupdf-dev (apt) / mupdf-devel (dnf) / mupdf (brew)"
    fi
fi
echo "[*] MuPDF        : ${MUPDF_HINT:-looked up by CMake}"

# ---------------------------------------------------------------------
# 1. Configure and build with CMake.
# ---------------------------------------------------------------------
# A fresh configure each run: the repository is small and this avoids the
# classic "the cache still points at a MuPDF that is gone" trap.
rm -rf "$BUILD"
mkdir -p "$OUT"

echo "[*] configuring ..."
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release "${MUPDF_ARG[@]}"

echo "[*] building ..."
cmake --build "$BUILD" --config Release --parallel

# CMakeLists.txt defines the target "pdf-compress", so the file it produces is
# named with a hyphen. Everything user facing (documentation, the MSIX
# manifest, the Windows build) says pdf_compress; ship it under that name.
PRODUCED="$BUILD/pdf-compress"
if [ ! -x "$PRODUCED" ]; then
    PRODUCED="$BUILD/pdf_compress"
fi
if [ ! -x "$PRODUCED" ]; then
    echo "[ERR] the build reported success but no executable was produced in $BUILD"
    exit 1
fi

rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$PRODUCED" "$STAGE/pdf_compress"
chmod +x "$STAGE/pdf_compress"

# The same two documents and the licence the Windows folder build carries.
cp "$ROOT/readme.md" "$ROOT/readme.zh.md" "$ROOT/LICENSE" "$STAGE/"

# ---------------------------------------------------------------------
# 2. Copy the shared libraries the binary actually loads, if any.
#
#    On Linux and macOS the distribution packages build MuPDF as a shared
#    library; a MuPDF built as a static library leaves the executable
#    self-contained and everything below copies nothing. ldd/otool tells us
#    which of those libraries are not in a system location, which is exactly
#    the set a user would otherwise have to install by hand - so libraries
#    from /lib, /usr/lib and the multiarch directories are skipped. Copying
#    libc.so.6 into the folder would look thorough and help nobody.
#
#    The count is written to a file (not captured from stdout) so that the
#    function can be called normally instead of in a command substitution.
# ---------------------------------------------------------------------
COUNT_FILE="$BUILD/.bundled_count"

copy_deps() {
    local binary="$1"
    local libraries=""

    if [ "$PLATFORM" = "linux" ]; then
        libraries="$(ldd "$binary" 2>/dev/null | awk '/=> \//{print $3}' | sort -u \
            | grep -vE '^/(lib|usr/lib|lib64|usr/lib64)/' || true)"
    else
        # Homebrew libraries live under /opt/homebrew or /usr/local; skip the
        # ones every Mac already has in /usr/lib and /System.
        libraries="$(otool -L "$binary" 2>/dev/null \
            | awk 'NR>1 {print $1}' \
            | grep -E '^/opt/homebrew|^/usr/local' | sort -u || true)"
    fi

    local copied=0
    for library in $libraries; do
        [ -f "$library" ] || continue
        cp -f "$library" "$STAGE/" || true
        # Follow the symlink chain: libmupdf.so -> libmupdf.so.24.9 etc. Copy
        # the real file too, otherwise the loader cannot resolve the soname.
        local resolved
        resolved="$(readlink -f "$library" 2>/dev/null || true)"
        if [ -n "$resolved" ] && [ -f "$resolved" ] && [ "$resolved" != "$library" ]; then
            cp -f "$resolved" "$STAGE/" || true
        fi
        copied=$((copied + 1))
    done
    printf '%s' "$copied" > "$COUNT_FILE"
}

if [ "$PLATFORM" = "linux" ] && command -v ldd > /dev/null 2>&1; then
    copy_deps "$STAGE/pdf_compress"
    echo "[*] bundled $(cat "$COUNT_FILE") shared librar(y|ies) next to the binary"
elif [ "$PLATFORM" = "macos" ] && command -v otool > /dev/null 2>&1; then
    copy_deps "$STAGE/pdf_compress"
    echo "[*] bundled $(cat "$COUNT_FILE") shared librar(y|ies) next to the binary"
fi
rm -f "$COUNT_FILE"

# ---------------------------------------------------------------------
# 3. Smoke test: the binary must start and print its help.
#
#    Output goes to a file, not into a pipe to head, so a SIGPIPE can never be
#    mistaken for a crash (the exit code is checked).
# ---------------------------------------------------------------------
echo "[*] smoke test: --help"
HELP_FILE="$BUILD/help_smoke.txt"
if ! "$STAGE/pdf_compress" --help > "$HELP_FILE" 2>&1; then
    echo "[ERR] the freshly built binary failed to run:"
    sed 's/^/      /' "$HELP_FILE"
    exit 1
fi
if [ ! -s "$HELP_FILE" ]; then
    echo "[ERR] --help produced no output - the binary is probably broken."
    exit 1
fi
head -3 "$HELP_FILE" | sed 's/^/      /'
rm -f "$HELP_FILE"

echo
echo "[OK] release folder:"
ls -lh "$STAGE" | sed 's/^/     /'
echo
echo "     $STAGE"
echo
echo "     There is no graphical interface on Linux/macOS: this build is the"
echo "     command line tool only."
