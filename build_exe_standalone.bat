@echo off
rem =====================================================================
rem  Build the *standalone folder* variant of pdf_compress and zip it.
rem
rem  Why a second script?  Nuitka's --onefile wraps the real program in a
rem  self-extracting bootstrap (a parent process) that starts a child. A console
rem  Ctrl+C is delivered to both, the bootstrap dies first with
rem  0xC000013A (STATUS_CONTROL_C_EXIT), and that is the exit code the shell
rem  sees - the documented "Ctrl+C -> 130" is lost even though the program
rem  itself interrupts cleanly and keeps its best result.
rem
rem  The standalone build has no bootstrap, so the exit code is exactly 130
rem  and startup does not unpack anything.
rem
rem  Trade-off: the result is a folder, not a single file. Ship both.
rem
rem  All the PyMuPDF / MSVC / encoding reasons behind the flags below are
rem  documented in build_exe.bat - read that first.
rem
rem  Keep this file ASCII-only: cmd reads .bat files using the OEM code page.
rem =====================================================================
setlocal
set "HERE=%~dp0"
set "VSLANG=1033"
set "OUTDIR=%HERE%build_standalone"
set "DIST=%OUTDIR%\pdf_compress.dist"
set "STAGE=%OUTDIR%\pdf_compress_standalone_win64"
set "ZIP=%HERE%pdf_compress_standalone_win64.zip"

where python >nul 2>&1
if errorlevel 1 goto no_python

set "SITE="
for /f "delims=" %%i in ('python -c "import sysconfig;print(sysconfig.get_paths()['purelib'])"') do set "SITE=%%i"
if not defined SITE goto no_python
for /f "delims=" %%i in ('python -c "import sys;print(sys.base_prefix)"') do set "PYROOT=%%i"

set "PKG=%SITE%\pymupdf"
if not exist "%PKG%\mupdf.py" goto no_pymupdf

set "MSVCP=%SystemRoot%\System32\msvcp140.dll"
if not exist "%MSVCP%" goto no_msvcp
if not exist "%PYROOT%\python3.dll" goto no_python3
if not exist "%HERE%pdf_compress.py" goto no_source

echo [*] site-packages : %SITE%
echo [*] Building standalone folder with Nuitka (takes about 3 minutes)...
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
    "%HERE%pdf_compress.py"
if errorlevel 1 goto build_failed

if not exist "%DIST%\pdf_compress.exe" goto no_dist

rem Stage a properly named top-level folder so unzipping does not scatter the
rem exe, PyMuPDF and the runtime DLLs straight into the current folder.
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
echo [OK] zip for release: "%ZIP%"  (contains the folder "%STAGE%" as its root)
exit /b 0

:no_python
echo [ERR] python not found on PATH.
exit /b 1

:no_source
echo [ERR] pdf_compress.py not found next to this script.
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
