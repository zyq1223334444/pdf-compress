#!/usr/bin/env bash
# =====================================================================
#  PDF Compressor -- build MuPDF from source on Linux / macOS
#
#  Run:      bash build/unix/build_mupdf_unix.sh [version]
#  Produces: build/out/mupdf-unix-prefix/
#                include/mupdf/...        the headers
#                lib/libmupdf.a           MuPDF itself
#                lib/libmupdf-third.a     zlib, freetype, harfbuzz, ...
#
#  Why this exists next to build\windows\build_mupdf.bat and not instead of it:
#  on Windows the target is a fixed MSVC project that always builds every
#  sub-library, so the .bat is enough. On Linux and macOS the easy route is
#  the distribution package, but those move at their own pace - Ubuntu 24.04
#  ships MuPDF 1.23, whose JPEG encoder still takes three arguments, and the
#  sources here need the four-argument one from 1.25 onwards. Building the
#  same 1.26.0 the Windows build uses keeps all three platforms on one API
#  and makes the result statically linked, so the release folder needs no
#  libmupdf.so next to the binary.
#
#  Depends on: gcc/g++ (or clang), make, curl, ar. Nothing else - MuPDF
#  bundles its own zlib, freetype, harfbuzz, openjpeg and jbig2dec.
#
#  Safe to re-run: an existing prefix is left alone unless you delete it.
# =====================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # .../build/unix
ROOT="$(cd "$HERE/../.." && pwd)"                      # repository root

VERSION="${1:-1.26.0}"
WORK="$HERE/../out/mupdf-unix"
PREFIX="$HERE/../out/mupdf-unix-prefix"
TARBALL="mupdf-${VERSION}-source.tar.gz"
URL="https://mupdf.com/downloads/archive/${TARBALL}"
SRC="$WORK/mupdf-${VERSION}-source"

# nproc is Linux/glibc; macOS spells it sysctl.
if command -v nproc > /dev/null 2>&1; then
    JOBS="$(nproc)"
elif command -v sysctl > /dev/null 2>&1; then
    JOBS="$(sysctl -n hw.ncpu)"
else
    JOBS=2
fi

if [ -f "$PREFIX/lib/libmupdf.a" ] && [ -f "$PREFIX/include/mupdf/fitz.h" ]; then
    echo "[*] MuPDF $VERSION is already built: $PREFIX"
    echo "    (delete that folder to force a rebuild)"
    exit 0
fi

for tool in make ar; do
    if ! command -v "$tool" > /dev/null 2>&1; then
        echo "[ERR] $tool is not installed."
        echo "      Debian/Ubuntu: sudo apt-get install -y build-essential curl"
        echo "      macOS:         xcode-select --install"
        exit 1
    fi
done

mkdir -p "$WORK"

# ---------------------------------------------------------------------
# 1. The source tarball.
# ---------------------------------------------------------------------
if [ ! -d "$SRC" ]; then
    if [ ! -f "$WORK/$TARBALL" ]; then
        echo "[*] downloading $URL"
        if command -v curl > /dev/null 2>&1; then
            curl -fL --retry 3 -o "$WORK/$TARBALL" "$URL"
        elif command -v wget > /dev/null 2>&1; then
            wget -O "$WORK/$TARBALL" "$URL"
        else
            echo "[ERR] neither curl nor wget is installed."
            exit 1
        fi
    fi
    echo "[*] extracting $TARBALL"
    tar -xzf "$WORK/$TARBALL" -C "$WORK"
fi

if [ ! -f "$SRC/Makefile" ]; then
    echo "[ERR] $SRC/Makefile is missing - the tarball did not unpack as expected."
    exit 1
fi

# ---------------------------------------------------------------------
# 2. Build. `build=release libs` is the static-library target; the OCR bits
#    (tesseract, leptonica) stay off because USE_TESSERACT defaults to no,
#    which is the difference between a three-minute build and a long one.
# ---------------------------------------------------------------------
echo "[*] building MuPDF $VERSION (jobs=$JOBS) ..."
cd "$SRC"
if [ ! -f "build/release/libmupdf.a" ] || [ ! -f "build/release/libmupdf-third.a" ]; then
    make -j"$JOBS" build=release libs
fi
ls -l build/release/libmupdf.a build/release/libmupdf-third.a

# ---------------------------------------------------------------------
# 3. Stage a prefix CMake can be pointed at with -DMUPDF_ROOT=<prefix>.
#    Both archives are copied; CMakeLists.txt links the -third one too when
#    it finds it next to the main library.
# ---------------------------------------------------------------------
echo "[*] staging the prefix: $PREFIX"
rm -rf "$PREFIX"
mkdir -p "$PREFIX/include" "$PREFIX/lib"
cp -r "$SRC/include/mupdf" "$PREFIX/include/"
cp "$SRC/build/release/libmupdf.a" "$SRC/build/release/libmupdf-third.a" "$PREFIX/lib/"

test -f "$PREFIX/include/mupdf/fitz.h"
test -f "$PREFIX/lib/libmupdf.a"
test -f "$PREFIX/lib/libmupdf-third.a"

echo
echo "[OK] MuPDF $VERSION prefix: $PREFIX"
echo "     build/unix/build_unix.sh picks it up with:"
echo "         MUPDF_ROOT=$PREFIX bash build/unix/build_unix.sh"
