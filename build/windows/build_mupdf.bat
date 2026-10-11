@echo off
rem ===========================================================================
rem  build\windows\build_mupdf.bat
rem
rem  Puts a usable MuPDF into third_party\mupdf, where CMakeLists.txt looks for
rem  it automatically (it searches third_party/mupdf/include and
rem  third_party/mupdf/lib unless -DMUPDF_ROOT points somewhere else).
rem
rem  Run:      build\windows\build_mupdf.bat [version]
rem  Produces: third_party\mupdf\include\mupdf\*.h
rem            third_party\mupdf\lib\mupdf.lib   (+ third-party static libs)
rem            third_party\mupdf\bin\*.dll       (release builds are DLLs)
rem            third_party\mupdf\README-MUPDF.txt  how this copy was produced
rem
rem  Why this script is not run automatically from build_exe.bat: compiling
rem  MuPDF from source takes several minutes and only has to happen once per
rem  machine. build_exe.bat only checks that the result exists and prints the
rem  command to run if it does not.
rem
rem  Two routes, tried in this order:
rem
rem   A. vcpkg, if it is installed. This is by far the fastest and gets the
rem      correct set of third-party libraries that MuPDF's vcpkg port pins.
rem          vcpkg install mupdf:x64-windows
rem      and then the prefix installed\x64-windows is copied over here.
rem
rem   B. The source tarball from mupdf.com, built with the solution that ships
rem      inside it:
rem          https://mupdf.com/downloads/archive/mupdf-<version>-source.tar.gz
rem          platform\win32\mupdf.sln  (MSBuild, x64, Release)
rem      MuPDF's own win32 solution does not use vcpkg, so it expects its
rem      third-party dependencies (freeglut, harfbuzz, jbig2dec, ...) to be
rem      present under third_party\ in the extracted tree. That is how the
rem      upstream release is meant to be built and it is a well-trodden path.
rem
rem  Keep this file ASCII-only: cmd reads .bat files with the OEM code page.
rem ===========================================================================
setlocal EnableExtensions EnableDelayedExpansion

set "HERE=%~dp0"
rem 这个脚本在 build\windows\ 里，所以仓库根是上两级。
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
set "DEST=%ROOT%\third_party\mupdf"
set "WORK=%ROOT%\build\out\mupdf"
set "VERSION=%~1"
if "%VERSION%"=="" set "VERSION=1.26.0"
set "TARBALL=mupdf-%VERSION%-source.tar.gz"
set "URL=https://mupdf.com/downloads/archive/%TARBALL%"

set "VSLANG=1033"
set "PF86=%ProgramFiles(x86)%"

echo [*] repository root : %ROOT%
echo [*] MuPDF target    : %DEST%
echo [*] MuPDF version   : %VERSION%

rem ---------------------------------------------------------------------------
rem 0. Already done? Then do nothing - this script must be safe to re-run.
rem    The marker is the header CMakeLists.txt looks for plus the import library.
rem ---------------------------------------------------------------------------
if exist "%DEST%\include\mupdf\fitz.h" (
    if exist "%DEST%\lib\mupdf.lib" (
        echo [=] third_party\mupdf already contains a MuPDF build - nothing to do.
        echo     Delete "%DEST%" to force a rebuild.
        exit /b 0
    )
)
if exist "%DEST%\include\mupdf\fitz.h" (
    if exist "%DEST%\lib\libmupdf.lib" (
        echo [=] third_party\mupdf already contains a MuPDF build - nothing to do.
        exit /b 0
    )
)

if not exist "%WORK%" mkdir "%WORK%"

rem ---------------------------------------------------------------------------
rem 1. Route A - vcpkg.
rem ---------------------------------------------------------------------------
set "VCPKG="
where vcpkg >nul 2>&1
if not errorlevel 1 set "VCPKG=vcpkg"
if not defined VCPKG if defined VCPKG_ROOT if exist "%VCPKG_ROOT%\vcpkg.exe" set "VCPKG=%VCPKG_ROOT%\vcpkg.exe"

if not defined VCPKG goto :route_b
echo [*] vcpkg found    : %VCPKG%
echo [*] installing mupdf:x64-windows (this downloads and builds dependencies) ...

call "%VCPKG%" install mupdf:x64-windows
if errorlevel 1 (
    echo [WARN] vcpkg could not install mupdf - falling back to the source tarball.
    goto :route_b
)

rem Find the vcpkg prefix. A classic checkout has installed\x64-windows; a
rem manifest-mode project would have vcpkg_installed instead. Look for both.
set "PREFIX="
if defined VCPKG_ROOT if exist "%VCPKG_ROOT%\installed\x64-windows\include\mupdf\fitz.h" set "PREFIX=%VCPKG_ROOT%\installed\x64-windows"
if not defined PREFIX if exist "%WORK%\vcpkg_installed\x64-windows\include\mupdf\fitz.h" set "PREFIX=%WORK%\vcpkg_installed\x64-windows"

if not defined PREFIX (
    echo [WARN] vcpkg reported success but no prefix with include\mupdf\fitz.h was found.
    echo        Looked in "%VCPKG_ROOT%\installed\x64-windows" and
    echo        "%WORK%\vcpkg_installed\x64-windows".
    echo        Falling back to the source tarball.
    goto :route_b
)

echo [*] copying "%PREFIX%" into "%DEST%" ...
call :copy_tree "%PREFIX%" "vcpkg: mupdf:x64-windows (prefix %PREFIX%)"
if errorlevel 1 goto :err_copy
call :verify
if errorlevel 1 goto :err_verify
goto :success

rem ---------------------------------------------------------------------------
rem 2. Route B - source tarball.
rem ---------------------------------------------------------------------------
:route_b
echo.
echo [*] route B: MuPDF source tarball

if exist "%WORK%\%TARBALL%" (
    echo [=] "%WORK%\%TARBALL%" already downloaded - reusing it.
) else (
    echo [*] downloading %URL%
    echo     (if this fails, download it by hand into "%WORK%" and re-run)
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; try { Invoke-WebRequest -Uri '%URL%' -OutFile '%WORK%\%TARBALL%' -UseBasicParsing } catch { Write-Host $_.Exception.Message; exit 1 }"
    if errorlevel 1 goto :err_download
)
if not exist "%WORK%\%TARBALL%" goto :err_download

set "SRC=%WORK%\mupdf-%VERSION%-source"
if exist "%SRC%" (
    echo [=] "%SRC%" already extracted - reusing it.
) else (
    echo [*] extracting ...
    rem tar.exe (bsdtar) ships with Windows 10 1803+ and handles .tar.gz.
    where tar >nul 2>&1
    if errorlevel 1 (
        echo [ERR] tar.exe was not found on PATH.
        echo       Windows 10 1803 and newer include it. Otherwise extract
        echo       "%WORK%\%TARBALL%" by hand into "%WORK%" so that the folder
        echo       "%SRC%" appears, then re-run this script.
        exit /b 1
    )
    tar -xzf "%WORK%\%TARBALL%" -C "%WORK%"
    if errorlevel 1 goto :err_extract
)
if not exist "%SRC%" goto :err_extract

rem 2a. MSVC x64 environment.
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :err_vswhere
set "VSPATH="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%I"
if not defined VSPATH goto :err_msvc
echo [*] MSVC           : %VSPATH%
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >"%WORK%\vcvars64.log" 2>&1
if errorlevel 1 goto :err_vcvars

rem 2b. Find the solution.
set "SLN="
for %%F in ("%SRC%\platform\win32\mupdf.sln") do if exist "%%~fF" set "SLN=%%~fF"
if not defined SLN goto :err_nosln
echo [*] solution       : %SLN%

rem 2c. Toolset. MuPDF's win32 project files pin PlatformToolset v142 (VS2019).
rem     A machine that only has VS2022 (v143) fails with
rem         error MSB8020: cannot find the build tools for v142
rem     so retarget the whole solution to the toolset that is actually
rem     installed. Global properties from the command line win over the value
rem     in the .vcxproj, which is exactly what this needs. Set
rem     PDFC_MUPDF_TOOLSET to override the detection.
set "TOOLSET=%PDFC_MUPDF_TOOLSET%"
if not defined TOOLSET (
    set "VSMAJOR="
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -property installationVersion`) do set "VSMAJOR=%%I"
    for /f "tokens=1 delims=." %%I in ("!VSMAJOR!") do set "VSMAJOR=%%I"
    if "!VSMAJOR!"=="17" set "TOOLSET=v143"
    if "!VSMAJOR!"=="16" set "TOOLSET=v142"
)
if defined TOOLSET (
    echo [*] toolset        : !TOOLSET! (overrides the v142 pinned in the solution)
) else (
    echo [*] toolset        : the one pinned in the solution
)

rem 2d. Build. Only the library: this project has nothing to do with MuPDF's
rem     command line tools, and building the whole solution regularly fails at
rem     the very end while linking mutool.exe / mupdf-gl.exe -- link.exe gives
rem     up (error MSB6006, exit code -1) on their enormous command lines, which
rem     costs several minutes and changes nothing for us.
rem     libmupdf.vcxproj has ProjectReferences to every third-party library it
rem     needs, so targeting it builds exactly the same set of dependencies and
rem     stops one step earlier.
echo [*] building libmupdf (Release x64) - this takes several minutes ...

rem     _CL_ is read by cl.exe itself and appended to every compile in the
rem     build, which is the only way to inject a flag into a project whose
rem     .vcxproj this script does not own. It is needed here:
rem     thirdparty\zxing-cpp\*.cpp are UTF-8 without a BOM and contain a
rem     non-ASCII character, so cl.exe decodes them with the system code page
rem     (936 on a Chinese Windows) and dies with
rem         error C3688: invalid literal suffix
rem     /utf-8 tells cl.exe the sources are UTF-8, which is true for all of
rem     MuPDF. It also silences the C4819 warnings.
set "_CL_=/utf-8"

set "MSBUILD_FLAGS=/m /nologo /p:Configuration=Release /p:Platform=x64 /v:minimal"
if defined TOOLSET set "MSBUILD_FLAGS=%MSBUILD_FLAGS% /p:PlatformToolset=!TOOLSET!"

set "LIBPROJ=%SRC%\platform\win32\libmupdf.vcxproj"
if not exist "%LIBPROJ%" goto :err_nosln
msbuild "%LIBPROJ%" %MSBUILD_FLAGS%
if errorlevel 1 (
    echo [WARN] building libmupdf.vcxproj on its own failed - retrying through
    echo        the solution, which resolves the project order differently ...
    msbuild "%SLN%" /t:libmupdf %MSBUILD_FLAGS%
)
if errorlevel 1 goto :err_msbuild

rem 2e. Collect the results. MuPDF's win32 solution scatters its outputs; search
rem     for them instead of hard-coding one path that only holds for one version.
echo [*] looking for the built library ...
set "FOUND_LIB="
for /r "%SRC%" %%F in (mupdf.lib libmupdf.lib) do if not defined FOUND_LIB if exist "%%~fF" set "FOUND_LIB=%%~fF"
if not defined FOUND_LIB goto :err_nolib
echo [*] library        : %FOUND_LIB%

if exist "%DEST%" rd /s /q "%DEST%"
mkdir "%DEST%"
mkdir "%DEST%\lib"
mkdir "%DEST%\include"
mkdir "%DEST%\bin"

rem Headers: the release keeps the public ones in include\.
if not exist "%SRC%\include\mupdf\fitz.h" goto :err_noinclude
xcopy /e /i /y /q "%SRC%\include" "%DEST%\include" >nul
if errorlevel 1 goto :err_copy

rem The import library plus every other .lib the solution produced: MuPDF's
rem static build needs its third-party libs on the link line.
for /r "%SRC%" %%F in (*.lib) do copy /y "%%~fF" "%DEST%\lib\" >nul
copy /y "%FOUND_LIB%" "%DEST%\lib\mupdf.lib" >nul
if errorlevel 1 goto :err_copy

rem Release builds of the win32 solution are DLLs; without them the freshly
rem built pdf_compress.exe would not start. If none exist, the build was
rem static and there is nothing to copy.
for /r "%SRC%" %%F in (*.dll) do copy /y "%%~fF" "%DEST%\bin\" >nul

rem     No "|" anywhere in this string on purpose: cmd re-parses the argument of
rem     a "call", and a pipe character there gets taken as a real pipe.
call :write_readme "source tarball %TARBALL%, built with platform\win32\libmupdf.vcxproj, configuration Release, platform x64"
call :verify
if errorlevel 1 goto :err_verify
goto :success

rem ===========================================================================
rem  Helpers
rem ===========================================================================
:copy_tree
rem %1 = source prefix, %2 = description
if exist "%DEST%" rd /s /q "%DEST%"
mkdir "%DEST%"
if exist "%~1\include" (
    xcopy /e /i /y /q "%~1\include" "%DEST%\include" >nul
) else (
    echo [ERR] "%~1\include" does not exist.
    exit /b 1
)
if exist "%~1\lib" xcopy /e /i /y /q "%~1\lib" "%DEST%\lib" >nul
if exist "%~1\bin" xcopy /e /i /y /q "%~1\bin" "%DEST%\bin" >nul
call :write_readme "%~2"
exit /b 0

:write_readme
rem %1 = how this copy was produced
rem Delayed expansion (!VERSION!) is needed here: this label runs inside a
rem "call", where %VERSION% was already expanded when the call was parsed.
> "%DEST%\README-MUPDF.txt" echo This directory is a build product. It is in .gitignore.
>>"%DEST%\README-MUPDF.txt" echo Produced by build\build_mupdf.bat (MuPDF !VERSION!).
>>"%DEST%\README-MUPDF.txt" echo Method: %~1
>>"%DEST%\README-MUPDF.txt" echo.
>>"%DEST%\README-MUPDF.txt" echo CMakeLists.txt finds it through PATH_SUFFIXES include and lib.
>>"%DEST%\README-MUPDF.txt" echo Delete this directory to force a rebuild.
exit /b 0

:verify
if not exist "%DEST%\include\mupdf\fitz.h" (
    echo [ERR] "%DEST%\include\mupdf\fitz.h" is missing.
    exit /b 1
)
if not exist "%DEST%\lib\mupdf.lib" (
    if not exist "%DEST%\lib\libmupdf.lib" (
        echo [ERR] no mupdf.lib / libmupdf.lib under "%DEST%\lib".
        exit /b 1
    )
)
exit /b 0

:success
echo.
echo ===========================================================================
echo  BUILD OK - MuPDF %VERSION% is in third_party\mupdf
echo ===========================================================================
echo   headers : %DEST%\include\mupdf
echo   library : %DEST%\lib
echo.
echo   Next step: build\windows\build_exe.bat
exit /b 0

rem ===========================================================================
rem  Error paths
rem ===========================================================================
:err_download
echo [ERR] downloading "%URL%" failed.
echo       Download it in a browser and put the file here:
echo         %WORK%\%TARBALL%
echo       then re-run this script (it will skip the download).
exit /b 1

:err_extract
echo [ERR] extracting "%WORK%\%TARBALL%" failed, or the expected folder
echo       "%SRC%" did not appear.
echo       Extract it by hand into "%WORK%" and re-run this script.
exit /b 1

:err_vswhere
echo [ERR] vswhere.exe was not found at "%VSWHERE%".
echo       Install Visual Studio 2022 (or the Build Tools) with the
echo       "Desktop development with C++" workload, then re-run.
exit /b 1

:err_msvc
echo [ERR] no Visual Studio installation with the x64 C++ toolset was found.
exit /b 1

:err_vcvars
echo [ERR] vcvars64.bat failed. Output:
type "%WORK%\vcvars64.log"
exit /b 1

:err_nosln
echo [ERR] "%SRC%\platform\win32\mupdf.sln" does not exist.
echo       The source layout differs in this MuPDF version. Either build the
echo       solution that does exist by hand, or use vcpkg:
echo         vcpkg install mupdf:x64-windows
exit /b 1

:err_msbuild
echo [ERR] msbuild failed for "%SLN%".
echo       The win32 solution needs MuPDF's third-party dependencies present
echo       under third_party\ in the extracted tree (they are part of the
echo       source tarball; a git checkout would need them fetched separately).
echo       If the failure was MSB8020 (missing platform toolset), the
echo       solution pins v142 and this script retargets it automatically -
echo       set PDFC_MUPDF_TOOLSET=v143 (or v142) to choose by hand.
echo       Using vcpkg instead usually avoids both problems:
echo         vcpkg install mupdf:x64-windows
exit /b 1

:err_nolib
echo [ERR] the build finished but no mupdf.lib / libmupdf.lib was produced
echo       anywhere under "%SRC%".
echo       Check the msbuild output above for a warning about a skipped project.
exit /b 1

:err_noinclude
echo [ERR] "%SRC%\include\mupdf\fitz.h" is missing - this does not look like a
echo       complete MuPDF source tree.
exit /b 1

:err_copy
echo [ERR] copying the MuPDF files into "%DEST%" failed.
exit /b 1

:err_verify
echo [ERR] "%DEST%" is missing the headers or the library CMake looks for.
exit /b 1
