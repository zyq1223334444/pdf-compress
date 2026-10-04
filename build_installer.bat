@echo off
rem =====================================================================
rem  Build the Windows installer (setup .exe) for PDF Compressor.
rem
rem  It packages the *standalone folder* build, so run
rem  build_exe_standalone.bat first (this script checks and tells you).
rem
rem  Requirement: Inno Setup 6.5+
rem      winget install --id JRSoftware.InnoSetup -e
rem  The compiler is looked up in the per-user location first, then the
rem  machine-wide one, so both install flavours work.
rem
rem  Keep this file ASCII-only: cmd reads .bat files using the OEM code page.
rem =====================================================================
setlocal
set "HERE=%~dp0"
set "SRC=%HERE%build_standalone\pdf_compress_standalone_win64"

if not exist "%SRC%\pdf_compress.exe" goto no_src

set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" goto build
set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" goto build
set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" goto build
goto no_iscc

:build
echo [*] payload : %SRC%
echo [*] compiler: %ISCC%
echo [*] Compiling the installer ...
"%ISCC%" "%HERE%installer.iss"
if errorlevel 1 goto failed

echo.
echo [OK] installer(s) produced in "%HERE%dist_installer":
dir /b "%HERE%dist_installer\*.exe"
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
