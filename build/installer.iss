; =====================================================================
;  Inno Setup script for PDF Compressor (C++ / CMake build)
;
;  Normally you do not call this file directly - build_installer.bat does,
;  because it also stages the payload and locates ISCC.exe:
;      build\build_installer.bat
;
;  To compile it by hand:
;      "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" build\installer.iss
;
;  Relative paths are resolved against the folder holding this script
;  (build\), so every reference to a repository-root file starts with "..".
;
;      ..\readme.md, ..\readme.zh.md, ..\LICENSE    shipped next to the exe
;      out\pdf_compress_win64                       the payload, staged by
;                                                   build_installer.bat:
;                                                   pdf_compress.exe, the MuPDF
;                                                   DLLs and the context menu
;                                                   package (msix + cer)
;      out\dist_installer                           the compiled setup .exe
;
;  ChineseSimplified.isl sits next to this file and provides the Simplified
;  Chinese texts; the compiler's own Default.isl provides English.
;
;  ---------------------------------------------------------------------
;  Two things this installer decides for the application itself:
;
;   * The language picked on the first wizard page is written to
;     HKLM\SOFTWARE\pdf-compress\Language. The program reads it back and uses
;     it as the default for --help, for every command line message and for
;     the graphical interface (the graphical interface can still switch
;     language itself).
;   * The "Explorer context menu" task (on by default) registers the Windows
;     11 top-level context menu for .pdf files. It needs the self-signed
;     certificate in the machine's Trusted People store, which is why the
;     [Run] section imports it first and then asks the program to register
;     the package as the invoking user.
;
;  Requires Inno Setup 6.5+ (tested with 6.7.3).
;  Keep this file UTF-8: Inno Setup reads .iss as UTF-8 (with or without BOM).
; =====================================================================

#define AppName        "PDF Compressor"
#define AppShortName   "pdf-compress"
#define AppVersion     "2.1.0"
#define AppPublisher   "zyq1223334444"
#define AppURL         "https://github.com/zyq1223334444/pdf-compress"
#define ExeName        "pdf_compress.exe"
#define SourceDir      "out\pdf_compress_win64"

[Setup]
; AppId identifies the application for upgrades/uninstall; never change it.
AppId={{C508F3A0-0A92-4986-A608-3F939B2E1F74}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\{#AppShortName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=out\dist_installer
OutputBaseFilename=pdf_compress_setup_{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\{#ExeName}
UninstallDisplayName={#AppName} {#AppVersion}
ChangesEnvironment=yes
SetupLogging=yes

[Languages]
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
chinesesimplified.CreateDesktopIcon=创建桌面快捷方式
chinesesimplified.AdditionalIcons=附加图标：
chinesesimplified.ModifyPath=把安装目录加入系统 PATH（可在任意终端直接运行 pdf_compress）
chinesesimplified.PathGroup=环境变量：
chinesesimplified.HelpShortcut=命令行帮助
chinesesimplified.ShellMenuGroup=资源管理器集成：
chinesesimplified.ShellMenuTask=添加“用 PDF Compressor 压缩”右键菜单（只对 .pdf 生效，Windows 11 显示在一级菜单）
chinesesimplified.ShellMenuInstall=正在注册资源管理器右键菜单...
chinesesimplified.RemovingShellMenu=正在移除资源管理器右键菜单...
english.CreateDesktopIcon=Create a desktop shortcut
english.AdditionalIcons=Additional icons:
english.ModifyPath=Add the install folder to the system PATH (run pdf_compress from any terminal)
english.PathGroup=Environment:
english.HelpShortcut=Command-line help
english.ShellMenuGroup=File Explorer integration:
english.ShellMenuTask=Add the "Compress with PDF Compressor" context menu (only for .pdf; shown in the Windows 11 top-level menu)
english.ShellMenuInstall=Registering the File Explorer context menu...
english.RemovingShellMenu=Removing the File Explorer context menu...

[Tasks]
; 默认勾选：右键菜单是这个版本的主要功能
Name: "shellmenu"; Description: "{cm:ShellMenuTask}"; GroupDescription: "{cm:ShellMenuGroup}"
Name: "modifypath"; Description: "{cm:ModifyPath}"; GroupDescription: "{cm:PathGroup}"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\readme.md"; DestDir: "{app}"; DestName: "readme.md"; Flags: ignoreversion
Source: "..\readme.zh.md"; DestDir: "{app}"; DestName: "readme.zh.md"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#ExeName}"
Name: "{group}\{cm:HelpShortcut}"; Filename: "{sys}\cmd.exe"; Parameters: "/k ""{app}\{#ExeName}"" --help"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#ExeName}"; Tasks: desktopicon

[Run]
; 1) 先把自签证书放进"受信任人"：这一步需要管理员权限，安装程序本身就有。
;    skipifdoesntexist：没有带右键菜单组件的构建（没跑过 build_shell_menu.bat）
;    不会因为这个文件缺失而让整台安装失败。
Filename: "{sys}\certutil.exe"; Parameters: "-addstore -f TrustedPeople ""{app}\pdf_compress_shell.cer"""; Flags: runhidden waituntilterminated skipifdoesntexist; Tasks: shellmenu; StatusMsg: "{cm:ShellMenuInstall}"
; 2) 再让程序自己注册稀疏包。它必须按当前用户注册（Appx 包是按用户的），
;    所以这一步降回启动安装程序的那个用户去跑；证书已经装好，不会再弹 UAC。
Filename: "{app}\{#ExeName}"; Parameters: "--install-menu --quiet"; Flags: runhidden waituntilterminated skipifdoesntexist; Tasks: shellmenu; StatusMsg: "{cm:ShellMenuInstall}"
Filename: "{sys}\cmd.exe"; Parameters: "/k ""{app}\{#ExeName}"" --help"; Description: "{cm:HelpShortcut}"; Flags: postinstall nowait skipifsilent unchecked

[UninstallRun]
; 卸载程序本身就是管理员权限，所以这里只能跑一次，拿不到"启动安装程序的那个
; 用户"的上下文。注意：Inno 的 runasoriginaluser 只对 [Run] 有效，
; [UninstallRun] 里加了会被编译器直接拒绝（项目 A 已经踩过这个坑），
; 所以这里不用它。为了不留下哪位用户的注册，--uninstall-menu 在管理员身份下
; 会额外做一次全用户清理（Get-AppxPackage -AllUsers ... | Remove-AppxPackage
; -AllUsers）；最后再把证书从"受信任人"里摘掉。
Filename: "{app}\{#ExeName}"; Parameters: "--uninstall-menu --quiet"; Flags: runhidden waituntilterminated skipifdoesntexist; RunOnceId: "RemoveShellMenu"
Filename: "{sys}\certutil.exe"; Parameters: "-delstore TrustedPeople CN=zyq1223334444"; Flags: runhidden waituntilterminated skipifdoesntexist; RunOnceId: "RemoveShellCertificate"

[Code]
const
  EnvironmentKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';
  AppKey         = 'SOFTWARE\{#AppShortName}';
  BackupValue    = 'OriginalPath';

{ 记录安装时选择的语言。程序把它当作默认语言：--help 用它、命令行输出用它、
  图形界面也用它（图形界面里仍然可以随时切换，那会写到用户自己的设置文件里，
  优先级高于这里）。 }
procedure WriteAppSettings;
var
  Lang: String;
begin
  if ActiveLanguage = 'english' then
    Lang := 'en'
  else
    Lang := 'zh';
  RegWriteStringValue(HKEY_LOCAL_MACHINE, AppKey, 'Language', Lang);
  RegWriteStringValue(HKEY_LOCAL_MACHINE, AppKey, 'InstallDir', ExpandConstant('{app}'));
  RegWriteStringValue(HKEY_LOCAL_MACHINE, AppKey, 'Version', '{#AppVersion}');
end;

{ 把安装目录加进系统 PATH。加之前先把原始值备份进注册表：
  用户的 PATH 可能以 ';' 结尾、也可能含空项（;;），这些退化写法下
  光靠字符串手术无法保证卸载时逐字节还原，所以直接存原值，卸载时精确恢复。 }
procedure EnvAddPath(Path: string);
var
  Paths, UpPaths, UpPath: string;
begin
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', Paths) then
    Paths := '';
  UpPaths := Uppercase(Paths);
  UpPath := Uppercase(Path);
  if (UpPaths = UpPath) or (Pos(';' + UpPath + ';', ';' + UpPaths + ';') > 0) then
    exit;                                    { 已经在 PATH 里，别重复加 }
  if not RegValueExists(HKEY_LOCAL_MACHINE, AppKey, BackupValue) then
    RegWriteStringValue(HKEY_LOCAL_MACHINE, AppKey, BackupValue, Paths);
  { 只在确实缺分隔符时才补 ';'，避免自己制造出 ';;' }
  if (Paths <> '') and (Paths[Length(Paths)] <> ';') then
    Paths := Paths + ';';
  Paths := Paths + Path;
  if not RegWriteStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', Paths) then
    MsgBox('无法写入系统 PATH，请手动把以下目录加入 PATH：' + #13#10 + Path, mbError, MB_OK);
end;

procedure EnvRemovePath(Path: string);
var
  Paths, Original, UpPaths, UpPath: string;
  P: Integer;
  Changed: Boolean;
begin
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', Paths) then
    exit;
  UpPaths := Uppercase(Paths);
  UpPath := Uppercase(Path);
  Changed := False;

  { 1) 安装时备份过原值，而且此后没人改过 PATH —— 逐字节还原 }
  if RegQueryStringValue(HKEY_LOCAL_MACHINE, AppKey, BackupValue, Original) then
    if (Paths = Original + ';' + Path) or (Paths = Original + Path) then
    begin
      Paths := Original;
      Changed := True;
    end;

  { 2) 别人动过 PATH：退化成只摘掉自己那一项 }
  if not Changed then
  begin
    if UpPaths = UpPath then
    begin
      Paths := '';
      Changed := True;
    end
    else if Pos(';' + UpPath + ';', UpPaths) > 0 then        { 夹在中间 }
    begin
      P := Pos(';' + UpPath + ';', UpPaths);
      Delete(Paths, P, Length(Path) + 1);                    { 删 ";Path"，保留后面的 ';' }
      Changed := True;
    end
    else if Pos(UpPath + ';', UpPaths) = 1 then              { 在最开头 }
    begin
      Delete(Paths, 1, Length(Path) + 1);                    { 删 "Path;" }
      Changed := True;
    end
    else if Pos(';' + UpPath, UpPaths) = Length(Paths) - Length(Path) then  { 在最末尾 }
    begin
      Delete(Paths, Length(Paths) - Length(Path), Length(Path) + 1);
      Changed := True;
    end;
  end;

  if Changed then
  begin
    RegWriteStringValue(HKEY_LOCAL_MACHINE, EnvironmentKey, 'Path', Paths);
    RegDeleteValue(HKEY_LOCAL_MACHINE, AppKey, BackupValue);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    WriteAppSettings;
    if WizardIsTaskSelected('modifypath') then
      EnvAddPath(ExpandConstant('{app}'));
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    EnvRemovePath(ExpandConstant('{app}'));
    { PATH 的备份值就在这个键下面，所以一定要等 EnvRemovePath 用完再删 }
    RegDeleteKeyIncludingSubkeys(HKEY_LOCAL_MACHINE, AppKey);
  end;
end;
