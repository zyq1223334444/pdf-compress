@echo off
rem =====================================================================
rem  pdf_compress -- standalone *folder* Windows build, packed into a zip
rem
rem  Run:      build\build_exe_standalone.bat
rem  Produces: build\out\pdf_compress_standalone_win64\pdf_compress.exe
rem            build\out\pdf_compress_standalone_win64.zip
rem
rem  ---------------------------------------------------------------------
rem  Why a second script at all?
rem
rem  Nuitka's --onefile wraps the real program in a self-extracting
rem  bootstrap that starts the actual process as a child. A console Ctrl+C
rem  is delivered to both; the bootstrap dies first with 0xC000013A
rem  (STATUS_CONTROL_C_EXIT), and that is the code the parent shell sees.
rem  The documented "Ctrl+C -> 130" is lost, even though the program itself
rem  interrupts cleanly and keeps its best result.
rem
rem  This build has no bootstrap, so the exit code is exactly 130 and the
rem  startup does not have to unpack anything. Trade-off: the result is a
rem  folder instead of a single file. Ship both and let people choose.
rem
rem  ---------------------------------------------------------------------
rem  Layout: we always work from the repository root (one level up) and
rem  write only inside build\out\. See build_exe.bat for the full directory
rem  contract and for the reasons behind the VSLANG / PyMuPDF / MSVC flags -
rem  read that one first, they apply here unchanged.
rem
rem  Requires: python, nuitka, pymupdf and MSVC 14.3+.
rem  Keep this file ASCII-only: cmd reads .bat files with the OEM code page.
rem =====================================================================
setlocal
set "HERE=%~dp0"
cd /d "%HERE%.." || goto no_root
set "ROOT=%CD%"
set "OUT=%HERE%out"
set "OUTDIR=%OUT%\nuitka_standalone"
set "DIST=%OUTDIR%\pdf_compress.dist"
set "STAGE=%OUT%\pdf_compress_standalone_win64"
set "ZIP=%OUT%\pdf_compress_standalone_win64.zip"
set "VSLANG=1033"

where python >nul 2>&1
if errorlevel 1 goto no_python

if not exist "%ROOT%\pdf_compress.py" goto no_source

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
echo [*] output        : %STAGE%
echo [*] Building the standalone folder with Nuitka (takes about 3 minutes) ...
python -m nuitka --standalone --msvc=latest ^
    --assume-yes-for-downloads ^
    --low-memory --lto=no ^
    --output-dir="%OUTDIR%" ^
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

if not exist "%DIST%\pdf_compress.exe" goto no_dist

rem Stage a properly named top-level folder, otherwise unzipping would
rem scatter the exe, PyMuPDF and the runtime DLLs straight into whatever
rem folder the user happens to be in.
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"
xcopy /e /i /y /q "%DIST%\*" "%STAGE%\" >nul
if errorlevel 1 goto stage_failed

if exist "%ZIP%" del /f /q "%ZIP%"
echo [*] Zipping "%STAGE%" ...
powershell -NoProfile -Command "Compress-Archive -Path '%STAGE%' -DestinationPath '%ZIP%' -CompressionLevel Optimal -Force"
if errorlevel 1 goto zip_failed

echo.
echo [OK] standalone exe : "%STAGE%\pdf_compress.exe"
echo [OK] zip for release: "%ZIP%"  (contains the folder as its root)
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

:no_dist
echo [ERR] Build reported success but "%DIST%\pdf_compress.exe" is missing.
exit /b 1

:stage_failed
echo [ERR] Could not copy the dist folder into "%STAGE%".
exit /b 1

:zip_failed
echo [ERR] Build OK but zipping failed.
exit /b 1
