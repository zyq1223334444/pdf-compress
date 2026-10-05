@echo off
rem =====================================================================
rem  pdf_compress.exe -- single-file (onefile) Windows build
rem
rem  Run:      build\build_exe.bat
rem  Produces: build\out\pdf_compress.exe
rem
rem  ---------------------------------------------------------------------
rem  Directory contract. Nothing is ever written outside build\out\.
rem
rem      build\         these scripts, installer.iss, the language file
rem      build\out\     every artefact and every Nuitka intermediate
rem      <repo root>    pdf_compress.py, requirements.txt, readme*.md
rem
rem  The source therefore sits one level above this script: we cd into the
rem  repository root first (Nuitka drops its cache next to the working
rem  directory) and keep every path of our own absolute.
rem
rem  Nuitka compiles Python to C and then calls the platform's own linker,
rem  so a build always happens on the target OS. This script is Windows
rem  only; Linux and macOS are handled by build_unix.sh in this folder.
rem
rem  ---------------------------------------------------------------------
rem  Requirements
rem
rem      pip install nuitka zstandard pymupdf
rem
rem      A C compiler: MSVC 14.3+ (Visual Studio 2022 Build Tools or newer).
rem      Nuitka cannot use MinGW together with Python 3.13 or newer - MSVC
rem      only.
rem
rem  Keep this file ASCII-only: cmd reads .bat files with the OEM code page,
rem  so UTF-8 text in here would be parsed as commands.
rem
rem  ---------------------------------------------------------------------
rem  Why the flags below look unusual. Every one of them was measured on a
rem  4-core / 16 GB machine - none of them is decoration.
rem
rem   VSLANG=1033
rem     Nuitka's Scons backend decodes cl.exe output using the "mbcs" code
rem     page, so a localized (Chinese) Visual Studio aborts the build with
rem     "UnicodeDecodeError: 'mbcs' codec can't decode bytes". Forcing
rem     English compiler messages sidesteps it.
rem
rem   --low-memory --lto=no
rem     Letting Nuitka compile PyMuPDF makes it emit module.pymupdf.mupdf.c:
rem     a single 122 MB / 2.35-million-line translation unit. With link time
rem     optimisation MSVC dies on it with
rem     "fatal error C1002: out of heap space" and "LNK1257".
rem
rem   --nofollow-import-to=pymupdf
rem   --no-deployment-flag=excluded-module-usage
rem   --include-data-files=<pymupdf>\*.py=pymupdf/   (plus *.pyd and *.dll)
rem     Turning LTO off is not enough on its own: the optimiser oscillates
rem     over that module for ~20 minutes and MSVC still has to swallow the
rem     122 MB unit. Compiling only this project's own code and shipping
rem     PyMuPDF verbatim avoids both problems, and takes the build from
rem     ~50 minutes down to ~3 minutes with identical behaviour.
rem
rem   msvcp140.dll / python3.dll
rem     PyMuPDF's _mupdf.pyd, _extra.pyd and mupdfcpp64.dll link against the
rem     MSVC C++ runtime and against the CPython stable-ABI forwarder.
rem     Nuitka ships the C runtime (vcruntime140.dll) but not those two, and
rem     without them the exe dies with
rem     "ImportError: DLL load failed while importing _extra".
rem =====================================================================
setlocal
set "HERE=%~dp0"
cd /d "%HERE%.." || goto no_root
set "ROOT=%CD%"
set "OUT=%HERE%out"
set "VSLANG=1033"

where python >nul 2>&1
if errorlevel 1 goto no_python

if not exist "%ROOT%\pdf_compress.py" goto no_source

rem locate site-packages and the Python installation
set "SITE="
for /f "delims=" %%i in ('python -c "import sysconfig;print(sysconfig.get_paths()['purelib'])"') do set "SITE=%%i"
if not defined SITE goto no_python
for /f "delims=" %%i in ('python -c "import sys;print(sys.base_prefix)"') do set "PYROOT=%%i"

set "PKG=%SITE%\pymupdf"
if not exist "%PKG%\mupdf.py" goto no_pymupdf

set "MSVCP=%SystemRoot%\System32\msvcp140.dll"
if not exist "%MSVCP%" goto no_msvcp
if not exist "%PYROOT%\python3.dll" goto no_python3

echo [*] repo root     : %ROOT%
echo [*] output        : %OUT%
echo [*] site-packages : %SITE%
echo [*] python root   : %PYROOT%
echo [*] Building pdf_compress.exe with Nuitka (takes about 3 minutes) ...
python -m nuitka --standalone --onefile --msvc=latest ^
    --low-memory --lto=no ^
    --output-dir="%OUT%\nuitka_onefile" ^
    --output-filename=pdf_compress.exe ^
    --nofollow-import-to=pymupdf ^
    --no-deployment-flag=excluded-module-usage ^
    --include-data-files="%PKG%\*.py=pymupdf/" ^
    --include-data-files="%PKG%\*.pyd=pymupdf/" ^
    --include-data-files="%PKG%\*.dll=pymupdf/" ^
    --include-data-files="%MSVCP%=msvcp140.dll" ^
    --include-data-files="%PYROOT%\python3.dll=python3.dll" ^
    --windows-console-mode=force ^
    --company-name="PDF Compressor" ^
    --product-name="PDF Compressor" ^
    --file-version=2.0.0.0 --product-version=2.0.0.0 ^
    --file-description="PDF compressor: aim a PDF at a target size" ^
    "%ROOT%\pdf_compress.py"
if errorlevel 1 goto build_failed

copy /y "%OUT%\nuitka_onefile\pdf_compress.exe" "%OUT%\pdf_compress.exe" >nul
if errorlevel 1 goto copy_failed

echo.
echo [OK] single file: "%OUT%\pdf_compress.exe"
echo      (the intermediate folder "%OUT%\nuitka_onefile" can be deleted)
exit /b 0

:no_root
echo [ERR] Could not enter the repository root from "%HERE%".
exit /b 1

:no_python
echo [ERR] python not found on PATH.
exit /b 1

:no_source
echo [ERR] pdf_compress.py not found at "%ROOT%".
exit /b 1

:no_pymupdf
echo [ERR] PyMuPDF not found in site-packages. Run: pip install pymupdf
exit /b 1

:no_msvcp
echo [ERR] msvcp140.dll not found in %SystemRoot%\System32
echo       Install the Microsoft Visual C++ Redistributable.
exit /b 1

:no_python3
echo [ERR] python3.dll not found in %PYROOT%
exit /b 1

:build_failed
echo [ERR] Nuitka build failed. Check that a C compiler (MSVC 14.3+) is installed;
echo       Nuitka cannot use MinGW together with Python 3.13+.
exit /b 1

:copy_failed
echo [ERR] Build finished but copying the exe into "%OUT%" failed.
exit /b 1
