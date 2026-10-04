#!/usr/bin/env bash
# =====================================================================
#  Build the Linux / macOS binaries for PDF Compressor with Nuitka.
#
#  Run this *inside* the target OS (WSL, a container, a Mac, CI):
#      bash build_unix.sh
#
#  Nuitka compiles Python to C and then calls the platform's own linker, so
#  there is no cross-compiling: a Linux binary must be built on Linux, a
#  macOS binary on macOS. The GitHub Actions workflow in
#  .github/workflows/build.yml does all three platform/arch combinations.
#
#  Requirements:
#    Debian/Ubuntu:  sudo apt-get install -y python3-venv python3-dev gcc g++ patchelf
#    macOS:          brew install python@3.13   (Xcode command line tools)
#    then:           python3 -m venv .venv && .venv/bin/pip install nuitka zstandard pymupdf
#
#  Why PyMuPDF is *not* compiled (same reasoning as build_exe.bat on Windows):
#  pymupdf/mupdf.py expands into a single ~122 MB / 2.35-million-line C file;
#  Nuitka's optimiser oscillates on it for ~20 minutes and gcc then has to
#  swallow that translation unit. Compiling only our own code and shipping
#  PyMuPDF verbatim is what makes this build take minutes instead of an hour.
#  The .so files carry RUNPATH=$ORIGIN (and the macOS ones @loader_path), so
#  they keep working as long as they stay in the same directory.
#
#  Produces:
#      dist_unix/pdf_compress                          single file (onefile)
#      dist_unix/pdf_compress_<os>_<arch>              same, renamed for release
#      dist_unix/pdf_compress_standalone/              folder build
#      dist_unix/pdf_compress_<os>_<arch>.tar.gz       folder build, packed
# =====================================================================
set -euo pipefail
cd "$(dirname "$0")"

PY="${PY:-python3}"
if [ -x ".venv/bin/python" ]; then
    PY=".venv/bin/python"
fi

case "$(uname -s)" in
    Linux)  PLATFORM=linux ;;
    Darwin) PLATFORM=macos ;;
    *) echo "[ERR] unsupported OS: $(uname -s)"; exit 1 ;;
esac
case "$(uname -m)" in
    x86_64|amd64) ARCH=x86_64 ;;
    arm64|aarch64) ARCH=arm64 ;;
    *) ARCH="$(uname -m)" ;;
esac
TAG="${PLATFORM}_${ARCH}"

echo "[*] target : $PLATFORM / $ARCH"
echo "[*] python : $($PY -V 2>&1)"
echo "[*] nuitka : $($PY -m nuitka --version 2>&1 | head -1)"

PKG="$("$PY" -c 'import os, pymupdf; print(os.path.dirname(pymupdf.__file__))')"
if [ ! -f "$PKG/mupdf.py" ]; then
    echo "[ERR] PyMuPDF not found (looked at $PKG). Run: pip install pymupdf"
    exit 1
fi
echo "[*] pymupdf: $PKG"

OUT="dist_unix"
rm -rf "$OUT"
mkdir -p "$OUT"

# Only pass the data-file patterns that actually match something: Nuitka treats
# a non-matching --include-data-files glob as a FATAL error ("does not match any
# files"), and which extensions exist differs per OS
# (Linux: .so + .so.28.2, macOS: .so + .dylib, Windows: .pyd + .dll).
DATA_ARGS=()
for pat in '*.py' '*.pyd' '*.so' '*.so.*' '*.dylib' '*.dll' 'py.typed'; do
    if compgen -G "$PKG/$pat" > /dev/null; then
        DATA_ARGS+=("--include-data-files=$PKG/$pat=pymupdf/")
    fi
done
echo "[*] pymupdf data patterns: ${#DATA_ARGS[@]}"

COMMON=(
    --assume-yes-for-downloads
    --nofollow-import-to=pymupdf
    --no-deployment-flag=excluded-module-usage
    --company-name="zyq1223334444"
    --product-name="PDF Compressor"
    --product-version=2.0.0
    --file-version=2.0.0
    --file-description="PDF compressor: aim a PDF at a target size"
)

echo "[*] building the single-file (onefile) binary ..."
"$PY" -m nuitka --standalone --onefile \
    "${COMMON[@]}" "${DATA_ARGS[@]}" \
    --output-dir="$OUT" \
    --output-filename=pdf_compress \
    pdf_compress.py

echo "[*] building the standalone folder ..."
"$PY" -m nuitka --standalone \
    "${COMMON[@]}" "${DATA_ARGS[@]}" \
    --output-dir="$OUT/standalone" \
    --output-filename=pdf_compress \
    pdf_compress.py

STAGE="$OUT/pdf_compress_standalone"
rm -rf "$STAGE"
mv "$OUT/standalone/pdf_compress.dist" "$STAGE"
rm -rf "$OUT/standalone"

cp "$OUT/pdf_compress" "$OUT/pdf_compress_${TAG}"
chmod +x "$OUT/pdf_compress" "$OUT/pdf_compress_${TAG}" "$STAGE/pdf_compress"

echo "[*] packing the folder build ..."
tar -C "$OUT" -czf "$OUT/pdf_compress_${TAG}.tar.gz" pdf_compress_standalone

echo
echo "[OK] artifacts:"
ls -lh "$OUT" | sed 's/^/     /'
echo
echo "     $OUT/pdf_compress_${TAG}                 (single file)"
echo "     $OUT/pdf_compress_${TAG}.tar.gz          (folder, packed)"
