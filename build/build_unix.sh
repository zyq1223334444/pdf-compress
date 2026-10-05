#!/usr/bin/env bash
# =====================================================================
#  PDF Compressor -- Linux / macOS build
#
#  Run *inside the target OS* (WSL, a container, a real Mac, CI):
#      bash build/build_unix.sh
#
#  Produces, all inside build/out/dist_unix/:
#      pdf_compress                      single file (onefile)
#      pdf_compress_<os>_<arch>          the same, renamed for the release
#      pdf_compress_standalone/          folder build
#      pdf_compress_<os>_<arch>.tar.gz   folder build, packed
#
#  ---------------------------------------------------------------------
#  Directory contract. This script lives in build/ and never writes outside
#  build/out/. It resolves the repository root itself (one level up), so it
#  can be started from any working directory.
#
#      build/         these scripts, installer.iss, the language file
#      build/out/     every artefact and every Nuitka intermediate
#      <repo root>    pdf_compress.py, requirements.txt, readme*.md
#
#  ---------------------------------------------------------------------
#  Why there is no cross-compiling
#
#  Nuitka compiles Python to C and then calls the platform's own compiler
#  and linker. A Linux binary must therefore be built on Linux and a macOS
#  binary on macOS; a Windows machine can produce neither. The GitHub
#  Actions workflow in .github/workflows/build.yml covers all three
#  platform/arch combinations on real machines - run it by hand, or push a
#  tag and the binaries are attached to that release automatically.
#
#  Requirements
#      Debian/Ubuntu: sudo apt-get install -y python3-venv python3-dev gcc g++ patchelf
#      macOS:         brew install python@3.13   (plus the Xcode command line tools)
#      then:          python3 -m venv .venv && .venv/bin/pip install nuitka zstandard pymupdf
#
#  A virtualenv at <repo root>/.venv is picked up automatically when present.
#  To use one that lives anywhere else, point PY at it explicitly:
#      PY=/root/venv/bin/python bash build/build_unix.sh
#
#  ---------------------------------------------------------------------
#  Why PyMuPDF is *not* compiled by Nuitka (same reasoning as build_exe.bat)
#
#  pymupdf/mupdf.py expands into a single ~122 MB / 2.35-million-line C file.
#  Nuitka's optimiser oscillates over it for ~20 minutes and gcc then has to
#  swallow that translation unit. Compiling only our own code and shipping
#  PyMuPDF verbatim is what keeps this build at minutes instead of an hour.
#  Its .so files carry RUNPATH=$ORIGIN (and @loader_path on macOS), so they
#  keep working as long as they stay in the same directory.
# =====================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # .../build
ROOT="$(cd "$HERE/.." && pwd)"                         # repository root
OUT="$HERE/out/dist_unix"
SOURCE="$ROOT/pdf_compress.py"

cd "$ROOT"

if [ ! -f "$SOURCE" ]; then
    echo "[ERR] pdf_compress.py not found at $SOURCE"
    exit 1
fi

PY="${PY:-python3}"
if [ -x "$ROOT/.venv/bin/python" ]; then
    PY="$ROOT/.venv/bin/python"
fi

# Fail with a readable message instead of letting the checks below traceback.
if ! "$PY" -c 'import nuitka' > /dev/null 2>&1; then
    echo "[ERR] nuitka is not installed for $PY"
    echo "      Run: $PY -m pip install nuitka zstandard"
    exit 1
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

echo "[*] repo root : $ROOT"
echo "[*] target    : $PLATFORM / $ARCH"
echo "[*] python    : $($PY -V 2>&1)"
echo "[*] nuitka    : $($PY -m nuitka --version 2>&1 | head -1)"

PKG="$("$PY" -c 'import os, pymupdf; print(os.path.dirname(pymupdf.__file__))' 2>/dev/null || true)"
if [ -z "$PKG" ] || [ ! -f "$PKG/mupdf.py" ]; then
    echo "[ERR] PyMuPDF not found for $PY"
    echo "      Run: $PY -m pip install pymupdf"
    exit 1
fi
echo "[*] pymupdf   : $PKG"

rm -rf "$OUT"
mkdir -p "$OUT"

# Only pass the data-file patterns that actually match something: Nuitka
# treats a non-matching --include-data-files glob as a FATAL error ("does not
# match any files"), and which extensions exist differs per OS
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
    "$SOURCE"

echo "[*] building the standalone folder ..."
"$PY" -m nuitka --standalone \
    "${COMMON[@]}" "${DATA_ARGS[@]}" \
    --output-dir="$OUT/standalone" \
    --output-filename=pdf_compress \
    "$SOURCE"

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
