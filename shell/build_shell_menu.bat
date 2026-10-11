@echo off
rem ===========================================================================
rem  shell\build_shell_menu.bat
rem
rem  Builds the Windows 11 *top-level* File Explorer context menu integration:
rem
rem    1. ExplorerCommand.dll      x64 in-proc COM server (IExplorerCommand)
rem    2. pdf_compress_shell.msix  sparse / external-location identity package
rem    3. pdf_compress_shell.cer   public code-signing certificate
rem    4. src\shell_payload.cpp    the two binaries above, hex encoded, ready to
rem                                be embedded into pdf_compress.exe and into
rem                                ExplorerCommand.dll's host process
rem
rem  Why a sparse package: the Windows 11 modern menu only shows commands that
rem  belong to an application with package identity, and the documented way for
rem  an unpackaged Win32 app to get identity is an MSIX registered with
rem  "Add-AppxPackage -ExternalLocation". The manifest registers the DLL as a
rem  COM surrogate server (windows.comServer) - not as InprocServer32 - because
rem  only the packaged registration is visible to File Explorer.
rem
rem  Unlike the Python sibling project, the payload is NOT base64+zlib: it is a
rem  plain hexadecimal string (see shell\make_payload.py for the reasoning).
rem  That keeps the decoder in src\shellmenu.cpp a table lookup with no extra
rem  dependency, and it keeps this generated C++ file pure ASCII.
rem
rem  NOTE: this file is ASCII only on purpose. cmd.exe reads .bat files with the
rem  OEM code page, so non-ASCII characters would be mangled on this machine.
rem ===========================================================================

setlocal EnableExtensions

rem The build machine may be running a localized Visual Studio. VSLANG=1033
rem makes cl/link print English, which keeps the error paths below readable and
rem makes the build logs comparable between machines.
set "VSLANG=1033"

set "PF86=%ProgramFiles(x86)%"
set "SHELL_DIR=%~dp0"
for %%I in ("%~dp0..") do set "ROOT=%%~fI"

set "OUT=%ROOT%\build\out\shell"
set "OBJ=%OUT%\obj"
set "STAGE=%OUT%\staging"
set "DLL=%OUT%\ExplorerCommand.dll"
set "MSIX=%OUT%\pdf_compress_shell.msix"
set "CER=%OUT%\pdf_compress_shell.cer"
set "PFX=%OUT%\pdf_compress_shell.pfx"
set "PFXPW=PdfCompressShellTemp1"
set "SRC_DIR=%ROOT%\src"
set "PAYLOAD=%SRC_DIR%\shell_payload.cpp"
set "PAYLOAD_SCRIPT=%SHELL_DIR%make_payload.py"
set "RES_SRC=%SHELL_DIR%Resources"
set "RES_ASSETS=%ROOT%\assets"
set "ICO=%RES_ASSETS%\pdf_compress.ico"

echo [*] repository root : %ROOT%
echo [*] build output    : %OUT%

if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"
del /f /q "%PFX%" >nul 2>&1

rem ---------------------------------------------------------------------------
rem 1. MSVC x64 environment (vswhere -> vcvars64.bat)
rem ---------------------------------------------------------------------------
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :err_vswhere

set "VSPATH="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%I"
if not defined VSPATH goto :err_msvc
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" goto :err_msvc

echo [*] MSVC           : %VSPATH%
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >"%OBJ%\vcvars64.log" 2>&1
if errorlevel 1 goto :err_vcvars

rem ---------------------------------------------------------------------------
rem 2. Module definition file: export the two COM entry points explicitly.
rem    (The CLSID itself comes from __declspec(uuid(...)) in ExplorerCommand.cpp
rem    plus the single #include <initguid.h>; the .def only fixes the exports.)
rem ---------------------------------------------------------------------------
set "DEF=%OBJ%\ExplorerCommand.def"
> "%DEF%" echo LIBRARY ExplorerCommand
>>"%DEF%" echo EXPORTS
>>"%DEF%" echo     DllGetClassObject PRIVATE
>>"%DEF%" echo     DllCanUnloadNow PRIVATE

rem ---------------------------------------------------------------------------
rem 3. Optional icon resource (id 101) taken from assets\pdf_compress.ico.
rem    That file is checked into this repository, but treat it as optional: the
rem    DLL then simply carries no icon resource.
rem ---------------------------------------------------------------------------
set "RES_ARG="
if exist "%ICO%" (
    echo [*] icon resource  : %ICO%
    copy /y "%ICO%" "%OBJ%\pdf_compress.ico" >nul
    > "%OBJ%\ExplorerCommand.rc" echo 101 ICON "pdf_compress.ico"
    set "RES_ARG=ExplorerCommand.res"
    set "RES_NEEDED=1"
) else (
    echo [NOTE] "%ICO%" not found - building ExplorerCommand.dll without an icon resource.
)

rem ---------------------------------------------------------------------------
rem 4. Compile. All intermediates (.obj/.res/.lib/.exp/.pdb) stay in obj\.
rem    /I"%SRC_DIR%" is what lets the generated shell_payload.cpp find
rem    shell_payload.hpp.
rem ---------------------------------------------------------------------------
cd /d "%OBJ%"

if defined RES_NEEDED (
    rc /nologo /fo "%OBJ%\ExplorerCommand.res" "%OBJ%\ExplorerCommand.rc"
    if errorlevel 1 goto :err_compile
)

cl /nologo /LD /O2 /MT /EHsc /utf-8 /DUNICODE /D_UNICODE /W4 /I"%SRC_DIR%" "%SHELL_DIR%ExplorerCommand.cpp" %RES_ARG% /link /OUT:"%DLL%" /DEF:"%DEF%" /PDB:"%OBJ%\ExplorerCommand.pdb" /IMPLIB:"%OBJ%\ExplorerCommand.lib" /INCREMENTAL:NO ole32.lib oleaut32.lib shell32.lib shlwapi.lib user32.lib advapi32.lib
if errorlevel 1 goto :err_compile
if not exist "%DLL%" goto :err_compile

echo [*] checking exports with dumpbin ...
dumpbin /exports "%DLL%" > "%OBJ%\dumpbin_exports.txt"
if errorlevel 1 goto :err_dumpbin
findstr /c:"DllGetClassObject" "%OBJ%\dumpbin_exports.txt" >nul
if errorlevel 1 goto :err_exports
findstr /c:"DllCanUnloadNow" "%OBJ%\dumpbin_exports.txt" >nul
if errorlevel 1 goto :err_exports

rem ---------------------------------------------------------------------------
rem 5. MSIX tile images. shell\Resources is the checked-in copy; assets\ wins if
rem    the other worker already produced matching PNGs.
rem ---------------------------------------------------------------------------
if not exist "%RES_SRC%\StoreLogo.png" if not exist "%RES_ASSETS%\StoreLogo.png" (
    echo [*] tile images missing, generating them with Pillow ...
    python "%SHELL_DIR%make_assets.py" --out "%RES_SRC%"
)

if exist "%STAGE%" rd /s /q "%STAGE%"
mkdir "%STAGE%"
mkdir "%STAGE%\Resources"

call :copy_res StoreLogo.png
if errorlevel 1 goto :err_res
call :copy_res Square150x150Logo.png
if errorlevel 1 goto :err_res
call :copy_res Square44x44Logo.png
if errorlevel 1 goto :err_res

copy /y "%SHELL_DIR%AppxManifest.xml" "%STAGE%\AppxManifest.xml" >nul
if errorlevel 1 goto :err_stage
copy /y "%DLL%" "%STAGE%\ExplorerCommand.dll" >nul
if errorlevel 1 goto :err_stage

rem ---------------------------------------------------------------------------
rem 6. Windows SDK tools (makeappx / signtool).
rem ---------------------------------------------------------------------------
set "SDKBIN="
for /f "delims=" %%I in ('dir /b /ad /o-n "%PF86%\Windows Kits\10\bin\10.*" 2^>nul') do if not defined SDKBIN set "SDKBIN=%PF86%\Windows Kits\10\bin\%%I\x64"
if not defined SDKBIN goto :err_sdk
if not exist "%SDKBIN%\makeappx.exe" goto :err_sdk
if not exist "%SDKBIN%\signtool.exe" goto :err_sdk
echo [*] Windows SDK    : %SDKBIN%

rem ---------------------------------------------------------------------------
rem 7. Self-signed code-signing certificate, created at build time so that no
rem    private key is ever committed. The .pfx is deleted again after signing.
rem    Identity/@Publisher in AppxManifest.xml must equal the subject below.
rem
rem    The public certificate is also installed into TrustedPeople: the package
rem    cannot be registered without it (Add-AppxPackage would fail with
rem    CERT_E_UNTRUSTEDROOT) and "signtool verify /pa" cannot succeed either.
rem    Cert:\LocalMachine\TrustedPeople is the store that /pa needs, but it
rem    requires an elevated prompt; the per-user store is the fallback. Stale
rem    certificates with the same subject are removed first so that repeated
rem    builds stay idempotent.
rem ---------------------------------------------------------------------------
echo [*] creating self-signed certificate (CN=zyq1223334444) ...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; Get-ChildItem -Path Cert:\CurrentUser\My,Cert:\LocalMachine\TrustedPeople,Cert:\CurrentUser\TrustedPeople -ErrorAction SilentlyContinue | Where-Object { $_.Subject -eq 'CN=zyq1223334444' } | Remove-Item -Force -ErrorAction SilentlyContinue; $cert = New-SelfSignedCertificate -Type Custom -Subject 'CN=zyq1223334444' -KeyUsage DigitalSignature -FriendlyName 'PDF Compressor shell extension' -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddYears(10) -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3','2.5.29.19={text}'); $pw = ConvertTo-SecureString -String '%PFXPW%' -Force -AsPlainText; Export-PfxCertificate -Cert $cert -FilePath '%PFX%' -Password $pw | Out-Null; Export-Certificate -Cert $cert -FilePath '%CER%' -Type CERT | Out-Null; $store = 'Cert:\CurrentUser\TrustedPeople'; try { Import-Certificate -FilePath '%CER%' -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople' | Out-Null; $store = 'Cert:\LocalMachine\TrustedPeople' } catch { Import-Certificate -FilePath '%CER%' -CertStoreLocation 'Cert:\CurrentUser\TrustedPeople' | Out-Null }; Write-Host ('[+] certificate thumbprint : ' + $cert.Thumbprint); Write-Host ('[+] public cert trusted in : ' + $store)"
if errorlevel 1 goto :err_cert
if not exist "%PFX%" goto :err_cert
if not exist "%CER%" goto :err_cert

rem ---------------------------------------------------------------------------
rem 8. Pack and sign.
rem    /nv is required: pdf_compress.exe is resolved outside of the package
rem    (uap10:AllowExternalContent), so it cannot be validated at pack time.
rem ---------------------------------------------------------------------------
echo [*] packing the sparse package ...
"%SDKBIN%\makeappx.exe" pack /o /nv /d "%STAGE%" /p "%MSIX%"
if errorlevel 1 goto :err_pack
if not exist "%MSIX%" goto :err_pack

echo [*] signing the package ...
"%SDKBIN%\signtool.exe" sign /fd SHA256 /f "%PFX%" /p "%PFXPW%" "%MSIX%"
if errorlevel 1 goto :err_sign

echo [*] verifying the signature ...
"%SDKBIN%\signtool.exe" verify /pa /v "%MSIX%"
if errorlevel 1 goto :err_verify

del /f /q "%PFX%" >nul 2>&1

rem ---------------------------------------------------------------------------
rem 9. Embed the results as hex strings into src\shell_payload.cpp.
rem    That file is compiled into pdf_compress.exe (see CMakeLists.txt) and the
rem    definitions it provides are declared by src\shell_payload.hpp.
rem ---------------------------------------------------------------------------
echo [*] writing %PAYLOAD% ...
python "%PAYLOAD_SCRIPT%" "%MSIX%" "%CER%" "%PAYLOAD%"
if errorlevel 1 goto :err_payload
if not exist "%PAYLOAD%" goto :err_payload

:success
echo.
echo ===========================================================================
echo  BUILD OK - Windows 11 top-level context menu for .pdf files
echo ===========================================================================
for %%F in ("%DLL%" "%MSIX%" "%CER%" "%PAYLOAD%") do echo   %%~fF  (%%~zF bytes)
echo.
echo   Next steps:
echo     1. Trust pdf_compress_shell.cer in Cert:\LocalMachine\TrustedPeople
echo        (this build already did it for the current machine), otherwise
echo        Add-AppxPackage fails with CERT_E_UNTRUSTEDROOT.
echo     2. Rebuild the executable so src\shell_payload.cpp is compiled in:
echo        build\build_exe.bat   (build_exe.bat calls this script first, so
echo        normally you do not run this one by hand at all).
echo     3. Register with:
echo        pdf_compress.exe --install-menu
echo     4. Restart File Explorer (or sign out/in) so the shell reloads the verb.
exit /b 0

rem ===========================================================================
rem  Error paths - every one of them exits with code 1 and says what to do.
rem ===========================================================================
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

:err_vcvars
echo [ERR] vcvars64.bat failed. Output:
type "%OBJ%\vcvars64.log"
exit /b 1

:err_compile
echo [ERR] building ExplorerCommand.dll failed (see the cl/link output above).
exit /b 1

:err_dumpbin
echo [ERR] dumpbin /exports failed on "%DLL%".
exit /b 1

:err_exports
echo [ERR] "%DLL%" does not export both DllGetClassObject and DllCanUnloadNow.
echo       Check ExplorerCommand.def and the [ERR]-free compile output above.
exit /b 1

:err_res
echo [ERR] an MSIX tile image is missing (see the message above).
echo       Regenerate the assets with:
echo         python shell\make_assets.py --out shell\Resources
exit /b 1

:err_stage
echo [ERR] could not populate the staging directory "%STAGE%".
exit /b 1

:err_sdk
echo [ERR] makeappx.exe / signtool.exe were not found under
echo       "%PF86%\Windows Kits\10\bin\10.*\x64".
echo       Install the Windows 10/11 SDK.
exit /b 1

:err_cert
echo [ERR] creating the self-signed certificate or exporting the .cer/.pfx failed.
echo       Run the PowerShell command shown above manually to see the error.
exit /b 1

:err_pack
echo [ERR] makeappx pack failed.
echo       Typical causes: a schema error in shell\AppxManifest.xml, or a file
echo       missing from the staging directory "%STAGE%".
exit /b 1

:err_sign
del /f /q "%PFX%" >nul 2>&1
echo [ERR] signtool sign failed for "%MSIX%".
exit /b 1

:err_verify
del /f /q "%PFX%" >nul 2>&1
echo [ERR] signtool verify failed for "%MSIX%".
echo       "verify /pa" needs the public certificate in
echo       Cert:\LocalMachine\TrustedPeople, which needs an elevated prompt:
echo         Import-Certificate -FilePath "%CER%" -CertStoreLocation Cert:\LocalMachine\TrustedPeople
exit /b 1

:err_payload
del /f /q "%PFX%" >nul 2>&1
echo [ERR] writing "%PAYLOAD%" failed.
echo       Make sure "python" is on PATH (python --version) and that
echo       shell\make_payload.py is present next to this script.
echo       Without src\shell_payload.cpp the program still builds, but
echo       --install-menu will report that this build has no context menu.
exit /b 1

rem ===========================================================================
rem  Helper: copy one tile image into the staging directory.
rem ===========================================================================
:copy_res
if exist "%RES_ASSETS%\%~1" (
    copy /y "%RES_ASSETS%\%~1" "%STAGE%\Resources\%~1" >nul
    exit /b 0
)
if exist "%RES_SRC%\%~1" (
    copy /y "%RES_SRC%\%~1" "%STAGE%\Resources\%~1" >nul
    exit /b 0
)
echo [ERR] tile image "%~1" is missing from both
echo       "%RES_SRC%" and "%RES_ASSETS%".
exit /b 1
