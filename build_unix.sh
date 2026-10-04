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
#  .github/workflows/build.yml does all three platforms for you.
#
#  Requirements:
#    Debian/Ubuntu:  sudo apt-get install -y python3-venv python3-dev gcc g++ patchelf
#    macOS:          brew install python@3.13   (Xcode command line tools)
#    then:           python3 -m venv .venv && .venv/bin/pip install nuitka zstandard pymupdf
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

OUT="dist_unix"
rm -rf "$OUT"
mkdir -p "$OUT"

COMMON=(
    --assume-yes-for-downloads
    --company-name="zyq1223334444"
    --product-name="PDF Compressor"
    --product-version=2.0.0
    --file-version=2.0.0
    --file-description="PDF compressor: aim a PDF at a target size"
)

echo "[*] building the single-file (onefile) binary ..."
"$PY" -m nuitka --standalone --onefile \
    "${COMMON[@]}" \
    --output-dir="$OUT" \
    --output-filename=pdf_compress \
    pdf_compress.py

echo "[*] building the standalone folder ..."
"$PY" -m nuitka --standalone \
    "${COMMON[@]}" \
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
