@echo off
rem =====================================================================
rem  PDF Compressor -- Windows installer (setup .exe)
rem
rem  Run:      build\build_installer.bat
rem  Produces: build\out\dist_installer\pdf_compress_setup_2.0.0.exe
rem
rem  It packages the *standalone folder* build, never the onefile exe: the
rem  folder build returns the exact exit code 130 on Ctrl+C and starts about
rem  six times faster. So run build_exe_standalone.bat first - this script
rem  checks for the payload and tells you if it is missing.
rem
rem  Everything is relative to this folder:
rem      payload    build\out\pdf_compress_standalone_win64
rem      script     build\installer.iss
rem      installer  build\out\dist_installer
rem  installer.iss reaches back up to the repository root for readme.md,
rem  readme.zh.md and LICENSE, which it ships next to the exe.
rem
rem  ---------------------------------------------------------------------
rem  Requirement: Inno Setup 6.5+ (tested with 6.7.3)
rem      winget install --id JRSoftware.InnoSetup -e
rem  The compiler is looked up per-user first and machine-wide afterwards,
rem  so both install flavours work.
rem
rem  Keep this file ASCII-only: cmd reads .bat files with the OEM code page.
rem =====================================================================
setlocal
set "HERE=%~dp0"
set "OUT=%HERE%out"
set "SRC=%OUT%\pdf_compress_standalone_win64"

if not exist "%SRC%\pdf_compress.exe" goto no_src

set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" goto build
set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" goto build
set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" goto build
goto no_iscc

:build
echo [*] payload  : %SRC%
echo [*] compiler : %ISCC%
echo [*] Compiling the installer ...
"%ISCC%" "%HERE%installer.iss"
if errorlevel 1 goto failed

echo.
echo [OK] installer(s) produced in "%OUT%\dist_installer":
dir /b "%OUT%\dist_installer\*.exe"
exit /b 0

:no_src
echo [ERR] The standalone folder build is missing:
echo       %SRC%
echo       Run build_exe_standalone.bat first.
exit /b 1

:no_iscc
echo [ERR] ISCC.exe (Inno Setup 6) was not found.
echo       Install it with: winget install --id JRSoftware.InnoSetup -e
exit /b 1

:failed
echo [ERR] Inno Setup compilation failed - see the messages above.
exit /b 1
