@echo off
rem =====================================================================
rem  Rebuild pdf_compress.exe with Nuitka (Python -> C -> machine code).
rem
rem  Requirements:
rem    pip install nuitka
rem    pip install zstandard        (optional: compresses the onefile output)
rem    A C compiler: MSVC 14.3+ (Visual Studio 2022 Build Tools or newer).
rem    NOTE: Nuitka cannot use MinGW with Python 3.13 or newer - MSVC only.
rem
rem  Keep this file ASCII-only: cmd reads .bat files using the OEM code page,
rem  so UTF-8 text would be mis-parsed as commands.
rem
rem  Why the unusual flags below?  All of them were needed on a 4-core /
rem  16 GB machine, measured, not guessed:
rem
rem   * VSLANG=1033 - Nuitka's Scons backend decodes cl.exe output with the
rem     "mbcs" code page. With a localized (Chinese) Visual Studio it died with
rem     "UnicodeDecodeError: 'mbcs' codec can't decode bytes". English messages
rem     avoid that.
rem
rem   * --low-memory --lto=no - compiling PyMuPDF normally makes Nuitka emit
rem     module.pymupdf.mupdf.c: 122 MB / 2.35 million lines. With link time
rem     optimisation MSVC fails on it with
rem     "fatal error C1002: out of heap space" and "LNK1257".
rem
rem   * --nofollow-import-to=pymupdf + --no-deployment-flag=... +
rem     --include-data-files of pymupdf - even without LTO, Nuitka's optimiser
rem     oscillates for ~20 minutes over that huge module and then MSVC still
rem     has to swallow a 122 MB translation unit. Compiling only our own code
rem     and shipping PyMuPDF verbatim avoids both problems, and the build
rem     drops from ~50 minutes to ~3 minutes.
rem
rem   * msvcp140.dll and python3.dll - pymupdf's _mupdf.pyd / _extra.pyd /
rem     mupdfcpp64.dll link against the MSVC C++ runtime and the CPython
rem     stable-ABI forwarder. Nuitka ships the C runtime (vcruntime140.dll)
rem     but not those two, and without them the exe dies with
rem     "ImportError: DLL load failed while importing _extra".
rem =====================================================================
setlocal
set "HERE=%~dp0"
set "VSLANG=1033"

where python >nul 2>&1
if errorlevel 1 goto no_python

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

echo [*] site-packages : %SITE%
echo [*] python root    : %PYROOT%
echo [*] Building pdf_compress.exe with Nuitka (takes about 3 minutes)...
python -m nuitka --standalone --onefile --msvc=latest ^
    --low-memory --lto=no ^
    --output-dir="%HERE%build" ^
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
    "%HERE%pdf_compress.py"
if errorlevel 1 goto build_failed

copy /y "%HERE%build\pdf_compress.exe" "%HERE%pdf_compress.exe" >nul
if errorlevel 1 goto copy_failed

echo.
echo [OK] pdf_compress.exe rebuilt: "%HERE%pdf_compress.exe"
echo      (the intermediate folder "%HERE%build" can be deleted)
exit /b 0

:no_python
echo [ERR] python not found on PATH.
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
echo [ERR] Build finished but copying the exe failed.
exit /b 1
