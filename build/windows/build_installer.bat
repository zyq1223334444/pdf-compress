@echo off
rem ===========================================================================
rem  build\windows\build_installer.bat
rem
rem  Run:      build\windows\build_installer.bat
rem  Produces: build\out\dist_installer\pdf_compress_setup_2.1.0.exe
rem
rem  It packages what build\windows\build_exe.bat produced, never the cmake
rem  tree: the staged folder under build\out\pdf_compress_win64 is exactly the
rem  set of files the installed program needs (exe, MuPDF DLLs, the MSIX context
rem  menu package and its certificate, README, LICENSE). So run build_exe.bat
rem  first - this script checks for the payload and says so if it is missing.
rem
rem  Everything is relative to this folder:
rem      payload    ..\out\pdf_compress_win64
rem      script     ..\installer\installer.iss
rem      installer  ..\out\dist_installer
rem  installer.iss reaches back up to the repository root for README.md,
rem  README.zh-CN.md and LICENSE, which it ships next to the exe.
rem
rem  ---------------------------------------------------------------------
rem  Requirement: Inno Setup 6.5+ (tested with 6.7.3)
rem      winget install --id JRSoftware.InnoSetup -e
rem  The compiler is looked up per-user first and machine-wide afterwards,
rem  so both install flavours work.
rem
rem  Keep this file ASCII-only: cmd reads .bat files with the OEM code page.
rem ===========================================================================
setlocal EnableExtensions

set "HERE=%~dp0"
rem 这个脚本在 build\windows\ 里：仓库根是上两级，构建输出是上一级。
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
set "OUT=%HERE%..\out"
set "SRC_EXE=%OUT%\pdf_compress.exe"
set "STAGE=%OUT%\pdf_compress_win64"

if not exist "%SRC_EXE%" goto :no_src

rem ---------------------------------------------------------------------------
rem 1. Stage the payload.
rem ---------------------------------------------------------------------------
if exist "%STAGE%" rd /s /q "%STAGE%"
mkdir "%STAGE%"

copy /y "%SRC_EXE%" "%STAGE%\pdf_compress.exe" >nul
if errorlevel 1 goto :stage_failed

rem MuPDF DLLs, when third_party\mupdf is a DLL build. Without them the
rem installed program would not start. A static MuPDF build leaves bin\ empty;
rem the wildcard test keeps xcopy from complaining about it.
if exist "%ROOT%\third_party\mupdf\bin\*.dll" (
    echo [*] copying the MuPDF DLLs ...
    xcopy /y /q "%ROOT%\third_party\mupdf\bin\*.dll" "%STAGE%\" >nul
)

rem The context menu package plus its certificate go next to the exe: the
rem installer trusts the certificate from there and src\shellmenu.cpp prefers
rem these files over the copy embedded in the exe.
if exist "%OUT%\shell\pdf_compress_shell.msix" (
    copy /y "%OUT%\shell\pdf_compress_shell.msix" "%STAGE%\" >nul
    copy /y "%OUT%\shell\pdf_compress_shell.cer" "%STAGE%\" >nul
    echo [*] context menu package staged
) else (
    echo [WARN] "%OUT%\shell\pdf_compress_shell.msix" not found.
    echo        The installer will still build, but its context menu task will
    echo        do nothing. Run build\windows\build_shell_menu.bat and build\windows\build_exe.bat
    echo        again to include it.
)

echo [*] payload staged : %STAGE%

rem ---------------------------------------------------------------------------
rem 2. Locate ISCC.exe.
rem ---------------------------------------------------------------------------
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" goto :build
set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" goto :build
set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" goto :build
goto :no_iscc

:build
echo [*] compiler       : %ISCC%
echo [*] Compiling the installer ...
"%ISCC%" "%HERE%..\installer\installer.iss"
if errorlevel 1 goto :failed

echo.
echo [OK] installer(s) produced in "%OUT%\dist_installer":
dir /b "%OUT%\dist_installer\*.exe"
exit /b 0

rem ===========================================================================
rem  Error paths
rem ===========================================================================
:no_src
echo [ERR] the executable is missing:
echo       %SRC_EXE%
echo       Run build\windows\build_exe.bat first.
exit /b 1

:stage_failed
echo [ERR] could not stage the payload into "%STAGE%".
exit /b 1

:no_iscc
echo [ERR] ISCC.exe (Inno Setup 6) was not found.
echo       Looked in:
echo         %LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe
echo         %ProgramFiles(x86)%\Inno Setup 6\ISCC.exe
echo         %ProgramFiles%\Inno Setup 6\ISCC.exe
echo       Install it with: winget install --id JRSoftware.InnoSetup -e
exit /b 1

:failed
echo [ERR] Inno Setup compilation failed - see the messages above.
exit /b 1
