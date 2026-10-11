@echo off
rem =====================================================================
rem  Compile-only syntax check for the C++ sources.
rem
rem  Why /c (compile without linking): linking the real program needs the
rem  MuPDF library, but checking API usage only needs the headers, which
rem  tools\fetch_mupdf.py downloads into third_party\mupdf\include. This
rem  script exists to flush out syntax, type and API-signature mistakes
rem  before the full dependency stack is in place.
rem
rem  Usage:  build\windows\check_compile.bat [sources...]
rem          with no arguments it checks every source file.
rem
rem  Keep this file ASCII-only: cmd reads .bat with the OEM code page.
rem =====================================================================
setlocal
set "HERE=%~dp0"
rem build\windows\ 的上两级才是仓库根。
cd /d "%HERE%..\.." || exit /b 1
set "ROOT=%CD%"
set "OBJ=%TEMP%\pdfc_obj"
if not exist "%OBJ%" mkdir "%OBJ%"

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [ERR] MSVC not found - check the vcvars64.bat path in this script.
    exit /b 1
)

set "SOURCES=%*"
if "%SOURCES%"=="" set "SOURCES=src\common.cpp src\i18n.cpp src\strings_en.cpp src\strings_en_extra.cpp src\shell_payload_default.cpp src\config.cpp src\options.cpp src\cli.cpp src\search.cpp src\engine.cpp src\process.cpp src\worker.cpp src\filedialog.cpp src\shellmenu.cpp src\platform_win.cpp src\main.cpp src\pdf.cpp src\gui\theme.cpp src\gui\widgets.cpp src\gui\window.cpp src\gui\gui.cpp tests\test_search.cpp tests\test_i18n.cpp"

echo [*] checking: %SOURCES%
cl /c /nologo /std:c++20 /EHsc /utf-8 /W4 /permissive- ^
   /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS ^
   /I src /I third_party\mupdf\include /Fo"%OBJ%\\" %SOURCES%
if errorlevel 1 (
    echo [ERR] compile check failed.
    exit /b 1
)
echo [OK] all files compiled cleanly.
exit /b 0
