@echo off
rem ===========================================================================
rem  build\windows\build_exe.bat
rem
rem  Run:      build\windows\build_exe.bat
rem  Produces: build\out\pdf_compress.exe        the program
rem            build\out\shell\*.msix / *.cer     (if the shell step ran)
rem
rem  Everything this script writes lives under build\out\, which is gitignored.
rem
rem  Order of operations, and why:
rem
rem   1. The Windows 11 context menu package is built FIRST. It produces
rem      src\shell_payload.cpp, and the executable embeds that file, so building
rem      the exe first would embed a stale (or missing) payload. A failure here
rem      is only a warning: you then get an exe whose --install-menu says the
rem      build has no context menu component.
rem   2. cmake configures with -DMUPDF_ROOT when a local build is present.
rem      CMakeLists.txt would find third_party/mupdf on its own, but passing the
rem      path explicitly makes the configure log say which MuPDF was used.
rem   3. cmake --build builds Release. The application icon arrives through
rem      src\app.rc, which CMakeLists.txt compiles via rc.exe.
rem
rem  Requirements:
rem    * MuPDF       - run build\windows\build_mupdf.bat first (or install vcpkg mupdf)
rem    * MSVC 14.3+  - Visual Studio 2022 or the Build Tools
rem    * CMake 3.16+ - https://cmake.org/download/ (or: winget install Kitware.CMake)
rem
rem  Keep this file ASCII-only: cmd reads .bat files with the OEM code page.
rem ===========================================================================
setlocal EnableExtensions

set "HERE=%~dp0"
rem 这个脚本在 build\windows\ 里，所以仓库根是上两级、构建输出是上一级。
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
set "OUT=%HERE%..\out"
set "BUILD=%OUT%\cmake"
set "PF86=%ProgramFiles(x86)%"

rem Visual Studio may be localized; keep the tool output in English so that the
rem error paths below stay readable and comparable between machines.
set "VSLANG=1033"

echo [*] repository root : %ROOT%
echo [*] output          : %OUT%

if not exist "%OUT%" mkdir "%OUT%"

rem ---------------------------------------------------------------------------
rem 1. Context menu package (optional). It regenerates src\shell_payload.cpp.
rem ---------------------------------------------------------------------------
if exist "%HERE%build_shell_menu.bat" (
    echo [*] building the Explorer context menu package ...
    call "%HERE%build_shell_menu.bat"
    if errorlevel 1 (
        echo [WARN] build_shell_menu.bat failed - the executable will be
        echo        built WITHOUT an embedded context menu. Deleting the stale
        echo        src\shell_payload.cpp so the fallback definitions are used.
        if exist "%ROOT%\src\shell_payload.cpp" del /f /q "%ROOT%\src\shell_payload.cpp" >nul 2>&1
    )
) else (
    echo [WARN] build_shell_menu.bat not found next to this script - building
    echo        without an embedded context menu.
)

rem ---------------------------------------------------------------------------
rem 2. Toolchain checks: first Visual Studio (everything else hangs off it),
rem    then CMake.
rem ---------------------------------------------------------------------------
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :err_vswhere
set "VSPATH="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%I"
if not defined VSPATH goto :err_msvc
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" goto :err_msvc
echo [*] MSVC           : %VSPATH%

rem The CMake generator name embeds the Visual Studio year, so it has to follow
rem what is actually installed. Hard-coding "Visual Studio 17 2022" fails
rem outright on a machine that only has VS 2026 ("could not find any instance of
rem Visual Studio 2022"), which is exactly what happened here.
rem vswhere reports installationVersion as 17.x.y / 18.x.y; the major number is
rem what the generator name is keyed on. -prerelease keeps this consistent with
rem the CMake lookup below, which also has to look at Insiders installs.
set "VSVER="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion`) do set "VSVER=%%I"
set "VSMAJOR="
for /f "tokens=1 delims=." %%V in ("%VSVER%") do set "VSMAJOR=%%V"
set "VSGEN="
if "%VSMAJOR%"=="18" set "VSGEN=Visual Studio 18 2026"
if "%VSMAJOR%"=="17" set "VSGEN=Visual Studio 17 2022"
if "%VSMAJOR%"=="16" set "VSGEN=Visual Studio 16 2019"
if not defined VSGEN goto :err_vsgen
echo [*] generator      : %VSGEN%

rem CMake lives in four different places depending on how it was installed,
rem and "not on PATH" is the normal case on a machine that only has the Visual
rem Studio Build Tools - VS ships its own copy and does not put it on PATH.
rem   1. PATH
rem   2. Visual Studio's bundled copy (what the VS installer gives you)
rem   3. the standalone installer's per-machine location
rem   4. the standalone installer's per-user location
set "CMAKE="
for %%C in (cmake.exe) do if not defined CMAKE set "CMAKE=%%~$PATH:C"
rem Visual Studio ships CMake under Common7\IDE\CommonExtensions, but only in
rem the "IDE" editions - the plain Build Tools do not have it, and a machine can
rem easily have both (that is exactly this one: BuildTools 2022 plus an Insiders
rem install). So ask vswhere for *every* installation, not just the latest.
rem -prerelease matters: without it vswhere hides preview/Insiders installs, and
rem on this machine the only CMake around is the one inside VS Insiders.
if not defined CMAKE (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -all -prerelease -products * -property installationPath`) do (
        if not defined CMAKE if exist "%%I\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE=%%I\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
)
if not defined CMAKE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE=%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE if exist "%ProgramFiles(x86)%\CMake\bin\cmake.exe" set "CMAKE=%ProgramFiles(x86)%\CMake\bin\cmake.exe"
if not defined CMAKE if exist "%LOCALAPPDATA%\Programs\CMake\bin\cmake.exe" set "CMAKE=%LOCALAPPDATA%\Programs\CMake\bin\cmake.exe"
if not defined CMAKE goto :err_cmake
rem Ask whichever cmake we ended up with; that covers "found but broken" too.
"%CMAKE%" --version >nul 2>&1
if errorlevel 1 goto :err_cmake
echo [*] CMake          : %CMAKE%

rem ---------------------------------------------------------------------------
rem 3. MuPDF. CMake stops with a readable message if it is missing; this check
rem    exists to say which command to run before that happens.
rem ---------------------------------------------------------------------------
set "MUPDF_ARG="
rem Two checks on purpose: third_party\mupdf can hold just the headers (the CMake
rem configure would then fail later, at link time, with a much less obvious
rem message). Both the header and the library have to be there to use it.
if not exist "%ROOT%\third_party\mupdf\include\mupdf\fitz.h" goto :mupdf_system
if not exist "%ROOT%\third_party\mupdf\lib\mupdf.lib" goto :mupdf_incomplete
set "MUPDF_ARG=-DMUPDF_ROOT=%ROOT%\third_party\mupdf"
echo [*] MuPDF          : %ROOT%\third_party\mupdf
goto :mupdf_done

:mupdf_incomplete
echo [NOTE] third_party\mupdf has the headers but no lib\mupdf.lib - the tree
echo        under it is a partial copy. Re-run: build\windows\build_mupdf.bat
goto :mupdf_done

:mupdf_system
echo [NOTE] third_party\mupdf is empty. CMake will look for a MuPDF on the
echo        system (vcpkg / a manual -DMUPDF_ROOT). If it is not found:
echo          build\windows\build_mupdf.bat

:mupdf_done

rem ---------------------------------------------------------------------------
rem 4. Configure and build.
rem
rem    -UMUPDF_LIBRARY -UMUPDF_INCLUDE_DIR: find_path/find_library put their result
rem    into the cache, so a tree configured once keeps that answer forever. Move
rem    or rebuild MuPDF and the next configure happily links the *old* library -
rem    the worst version of this being a leftover stub .lib that links cleanly and
rem    produces a program that crashes. Dropping just those two cache entries
rem    makes CMake look the library up again while keeping every compiled object.
rem ---------------------------------------------------------------------------
echo [*] configuring with the %VSGEN% generator ...
"%CMAKE%" -S "%ROOT%" -B "%BUILD%" -G "%VSGEN%" -A x64 ^
    -UMUPDF_LIBRARY -UMUPDF_INCLUDE_DIR %MUPDF_ARG%
if errorlevel 1 goto :err_configure

echo [*] building Release ...
"%CMAKE%" --build "%BUILD%" --config Release --parallel
if errorlevel 1 goto :err_build

set "BUILT=%BUILD%\Release\pdf-compress.exe"
if not exist "%BUILT%" set "BUILT=%BUILD%\Release\pdf_compress.exe"
if not exist "%BUILT%" goto :err_nobinary
echo [*] built binary   : %BUILT%

rem ---------------------------------------------------------------------------
rem 5. Copy the result into place.
rem
rem    The file is already called pdf_compress.exe: CMakeLists.txt sets
rem    OUTPUT_NAME on the "pdf-compress" target precisely so that the build
rem    product matches the name shell\AppxManifest.xml, the Start Menu shortcut
rem    and the documentation all expect. The one thing left to do here is move
rem    it out of the CMake tree into build\out\, where build\windows\build_installer.bat and
rem    the context-menu packaging look for it.
rem
rem    The application icon is NOT handled here either: it comes from
rem    src\app.rc, which CMakeLists.txt adds to the target (MSVC's CMake runs
rem    rc.exe itself).
rem ---------------------------------------------------------------------------
set "TARGET=%OUT%\pdf_compress.exe"

if exist "%TARGET%" del /f /q "%TARGET%" >nul 2>&1
copy /y "%BUILT%" "%TARGET%" >nul
if errorlevel 1 goto :err_copy

rem ---------------------------------------------------------------------------
rem 6. MuPDF DLLs.
rem
rem    A DLL build of MuPDF needs its DLLs next to the executable, both to run
rem    at all and for the MSIX sparse package, which resolves pdf_compress.exe
rem    from this directory as its ExternalLocation. A static MuPDF build leaves
rem    bin\ empty, and xcopy would then print "File not found - *.dll" at the
rem    user for no reason - hence the wildcard test.
rem ---------------------------------------------------------------------------
if exist "%ROOT%\third_party\mupdf\bin\*.dll" (
    echo [*] staging the MuPDF DLLs ...
    xcopy /y /q "%ROOT%\third_party\mupdf\bin\*.dll" "%OUT%\" >nul
)

echo.
echo ===========================================================================
echo  BUILD OK
echo ===========================================================================
for %%F in ("%TARGET%") do echo   %%~fF  (%%~zF bytes)
echo.
echo   Run it              : "%TARGET%" --help
echo   Install the menu    : "%TARGET%" --install-menu
echo   Build the installer : build\windows\build_installer.bat
exit /b 0

rem ===========================================================================
rem  Error paths
rem ===========================================================================
:err_cmake
echo [ERR] cmake was not found.
echo       Looked on PATH, in Visual Studio's own copy under "%VSPATH%",
echo       and in the standalone installer's usual locations.
echo       Install it with: winget install Kitware.CMake
echo       or from https://cmake.org/download/  (3.16 or newer).
exit /b 1

:err_vswhere
echo [ERR] vswhere.exe was not found at:
echo       "%VSWHERE%"
echo       Install Visual Studio 2022 (or the Build Tools) with the
echo       "Desktop development with C++" workload.
exit /b 1

:err_msvc
echo [ERR] no Visual Studio installation with the x64 C++ toolset was found.
echo       Install the "Desktop development with C++" workload, then re-run.
exit /b 1

:err_vsgen
echo [ERR] no CMake generator is known for this Visual Studio.
echo       installationVersion reported: "%VSVER%" (major "%VSMAJOR%")
echo       Handled: 16 (2019), 17 (2022), 18 (2026).
echo       For a newer Visual Studio add one line next to the other VSGEN
echo       assignments in this script, then re-run.
exit /b 1

:err_configure
echo [ERR] CMake configure failed - see the messages above.
echo       If it says "Cannot find MuPDF", run:
echo         build\windows\build_mupdf.bat
exit /b 1

:err_build
echo [ERR] the build failed - see the compiler output above.
exit /b 1

:err_nobinary
echo [ERR] the build reported success but no executable was produced.
echo       Expected "%BUILD%\Release\pdf-compress.exe".
exit /b 1

:err_copy
echo [ERR] copying the executable to "%TARGET%" failed.
exit /b 1
