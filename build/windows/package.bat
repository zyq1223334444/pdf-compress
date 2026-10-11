@echo off
rem ===========================================================================
rem  build\windows\package.bat
rem
rem  Run:      build\windows\package.bat
rem  Needs:    build\out\pdf_compress.exe                     (build_exe.bat)
rem            build\out\dist_installer\pdf_compress_setup_<ver>.exe
rem            (build_installer.bat - this script runs it when it is missing)
rem
rem  Produces, all under <repo>\dist\windows\:
rem
rem      pdf_compress_<ver>_win64.exe      the single file build
rem      pdf-compress-<ver>-win64\         the folder build (exe + docs)
rem      pdf-compress-<ver>-win64.zip      the folder build, packed
rem      pdf_compress_setup_<ver>.exe      the installer
rem
rem  Why three Windows builds: the executable already contains MuPDF and the
rem  context menu payload, so it runs on its own. Some people still want a
rem  folder they can unpack anywhere, and some want an installer that writes
rem  the Start Menu entries and registers the Explorer menu for them.
rem
rem  dist\ is gitignored. Release artefacts do not belong in the repository -
rem  they are attached to a GitHub release instead.
rem
rem  Keep this file ASCII-only: cmd reads .bat files with the OEM code page.
rem ===========================================================================
setlocal EnableExtensions

set "HERE=%~dp0"
rem 这个脚本在 build\windows\ 里：仓库根是上两级，构建输出是上一级。
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
set "OUT=%HERE%..\out"
set "DIST=%ROOT%\dist\windows"

rem ---------------------------------------------------------------------------
rem 0. Version, read from CMakeLists.txt so the two can never drift apart.
rem ---------------------------------------------------------------------------
set "VERSION="
for /f "tokens=3" %%V in ('findstr /r /c:"^project(pdf-compress VERSION" "%ROOT%\CMakeLists.txt"') do set "VERSION=%%V"
if not defined VERSION goto :err_version

set "FOLDER=pdf-compress-%VERSION%-win64"
set "SINGLE=pdf_compress_%VERSION%_win64.exe"
set "ZIP=pdf-compress-%VERSION%-win64.zip"
set "SETUP=pdf_compress_setup_%VERSION%.exe"

if not exist "%OUT%\pdf_compress.exe" goto :err_no_exe

echo [*] repository root : %ROOT%
echo [*] version         : %VERSION%
echo [*] output          : %DIST%

rem ---------------------------------------------------------------------------
rem 1. Clean slate, then the single file build.
rem ---------------------------------------------------------------------------
if exist "%DIST%" rd /s /q "%DIST%"
mkdir "%DIST%"
if errorlevel 1 goto :err_mkdir

copy /y "%OUT%\pdf_compress.exe" "%DIST%\%SINGLE%" >nul
if errorlevel 1 goto :err_copy

rem ---------------------------------------------------------------------------
rem 2. The folder build: the same executable plus the documents a user needs,
rem    plus the context menu package. The package is embedded in the exe, but
rem    keeping the two files next to it means "pdf_compress.exe --install-menu"
rem    never has to unpack anything to a temporary directory.
rem ---------------------------------------------------------------------------
set "STAGE=%DIST%\%FOLDER%"
mkdir "%STAGE%"
copy /y "%OUT%\pdf_compress.exe" "%STAGE%\pdf_compress.exe" >nul
if errorlevel 1 goto :err_copy

copy /y "%ROOT%\README.md"      "%STAGE%\README.md"      >nul
copy /y "%ROOT%\README.zh-CN.md" "%STAGE%\README.zh-CN.md" >nul
copy /y "%ROOT%\LICENSE"        "%STAGE%\LICENSE"        >nul

rem A DLL build of MuPDF needs its DLLs next to the executable. The wildcard
rem test keeps xcopy from printing "File not found" on a static build.
if exist "%OUT%\*.dll" (
    echo [*] copying the MuPDF DLLs ...
    xcopy /y /q "%OUT%\*.dll" "%STAGE%\" >nul
)
if exist "%OUT%\shell\pdf_compress_shell.msix" (
    copy /y "%OUT%\shell\pdf_compress_shell.msix" "%STAGE%\" >nul
    copy /y "%OUT%\shell\pdf_compress_shell.cer" "%STAGE%\" >nul
    echo [*] context menu package included in the folder build
) else (
    echo [WARN] build\out\shell\pdf_compress_shell.msix not found - the folder
    echo        build has no context menu package of its own. The one embedded
    echo        in the executable still works.
)

rem ---------------------------------------------------------------------------
rem 3. Pack the folder build. tar.exe ships with Windows 10 1803 and later and
rem    picks the archive format from the extension, so no extra tool is needed.
rem ---------------------------------------------------------------------------
echo [*] packing the folder build ...
tar.exe -a -c -f "%DIST%\%ZIP%" -C "%DIST%" "%FOLDER%"
if errorlevel 1 goto :err_zip

rem ---------------------------------------------------------------------------
rem 4. The installer. build_installer.bat stages its own payload and skips the
rem    build when the file is already there, so this is cheap on a re-run.
rem ---------------------------------------------------------------------------
if not exist "%OUT%\dist_installer\%SETUP%" (
    echo [*] installer missing - running build_installer.bat ...
    call "%HERE%build_installer.bat"
    if errorlevel 1 goto :err_installer
)
if not exist "%OUT%\dist_installer\%SETUP%" goto :err_installer

copy /y "%OUT%\dist_installer\%SETUP%" "%DIST%\%SETUP%" >nul
if errorlevel 1 goto :err_copy

rem ---------------------------------------------------------------------------
rem 5. Report.
rem ---------------------------------------------------------------------------
echo.
echo ===========================================================================
echo  PACKAGING OK - dist\windows
echo ===========================================================================
call :size "%DIST%\%SINGLE%"
call :size "%DIST%\%ZIP%"
call :size "%DIST%\%SETUP%"
echo.
echo   folder build      : %DIST%\%FOLDER%
echo   folder build, exe : %DIST%\%FOLDER%\pdf_compress.exe
echo.
exit /b 0

:size
for %%F in ("%~1") do echo   %%~nxF  (%%~zF bytes)
exit /b 0

rem ===========================================================================
rem  Error paths
rem ===========================================================================
:err_version
echo [ERR] could not read the version from:
echo       "%ROOT%\CMakeLists.txt"
echo       Expected a line like: project(pdf-compress VERSION 2.1.0 LANGUAGES CXX)
exit /b 1

:err_no_exe
echo [ERR] the executable is missing:
echo       %OUT%\pdf_compress.exe
echo       Run build\build_exe.bat first.
exit /b 1

:err_mkdir
echo [ERR] could not create "%DIST%".
exit /b 1

:err_copy
echo [ERR] copying into "%DIST%" failed.
exit /b 1

:err_zip
echo [ERR] tar.exe could not create "%DIST%\%ZIP%".
echo       Windows 10 1803 or newer ships tar.exe in %%SystemRoot%%\System32.
exit /b 1

:err_installer
echo [ERR] "%OUT%\dist_installer\%SETUP%" was not produced.
echo       Run build\windows\build_installer.bat and read its messages.
exit /b 1
